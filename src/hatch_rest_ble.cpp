#include <Arduino.h>
#include <NimBLEDevice.h>
#include <common.h>

#define BUTTON_BUILTIN 39

// Identifiers for connecting to the Hatch over BLE
static BLEAddress deviceAddress("ef:d8:7b:5c:59:92", 1);
static BLEUUID serviceUUID("02240001-5efd-47eb-9c1a-de53f7a2b232");
static BLEUUID    charUUID("02240002-5efd-47eb-9c1a-de53f7a2b232");
static BLEUUID feedbackServiceUUID("02260001-5efd-47eb-9c1a-de53f7a2b232");
static BLEUUID    feedbackCharUUID("02260002-5efd-47eb-9c1a-de53f7a2b232");

// Play Favorite #1, which is no song and no color ("fake off").
static std::string TURN_OFF = "SP01";
// Play Favorite #2, the white noise/ocean sound with red light.
static std::string TURN_ON = "SP02";
// Turn the power on
static std::string POWER_ON = "SI01";

unsigned int lastButtonState = 1;
unsigned long buttonPressStartTime = 0;
// A short press must be shorter than this threshold and turns the device on.
const unsigned int shortPressMillis = 1000;
// A long hold must be longer than this threshold and turns the device off.
const unsigned int longHoldMillis = 2000;

unsigned int lastMqttStateUpdateMillis = 0;
const unsigned int stateUpdateIntervalMillis = 60 * 1000;

bool changeDeviceState = false;
bool newDeviceState = false;

static NimBLEClient* client = nullptr;

const char *MQTT_CONTROL_TOPIC = "nursery/hatch";
const char *MQTT_STATE_TOPIC = "nursery/hatch/state";

BLERemoteCharacteristic* getCharacteristic(BLEUUID service, BLEUUID characteristic) {
    BLERemoteService* remoteService = client->getService(service);
    if (remoteService == nullptr) {
        Serial.println("Failed to find our service UUID");
        client->disconnect();
        return nullptr;
    }
    Serial.println(" - Found our service");

    BLERemoteCharacteristic* remoteCharacteristic = remoteService->getCharacteristic(characteristic);
    if (remoteCharacteristic == nullptr) {
        Serial.println("Failed to find our characteristic UUID");
        client->disconnect();
        return nullptr;
    }
    Serial.println(" - Found our characteristic");

    return remoteCharacteristic;
}

void connectToHatch() {
    if (client == nullptr) {
        client = BLEDevice::createClient();
    }

    Serial.println("Attempting to connect to Hatch Rest...");

    for (;;) {
        if (!client->connect(deviceAddress)) {
            Serial.println("Failed to connect to device!");
        } else if (!client->isConnected()) {
            Serial.println("Connection appeared to be successful but now we're disconnected");
        } else {
            Serial.println("Connected to device!");
            break;
        }

        // If we failed to connect or disconnected, just keep trying after a short delay.
        delay(10);
    }

    Serial.println("Ready to control device!");
}

void disconnectFromHatch() {
    Serial.println("Disconnecting from Hatch...");
    client->disconnect();
}

void mqttPublishState(bool state) {
    const char *stateStr = state ? "ON" : "OFF";
    mqttClient()->publish(MQTT_STATE_TOPIC, stateStr);
}

bool decodePowerState(const char* feedback) {
    while (*feedback) {
        if (*feedback == 0x54) {
            feedback += 5;
        } else if (*feedback == 0x43) {
            feedback += 5;
        } else if (*feedback == 0x53) {
            feedback += 3;
        } else if (*feedback == 0x50) {
            char data = *(feedback + 1);
            return (data != 0) && (data & 0xc0) == 0;
        }
    }
    return false;
}

void setDeviceStateActually(bool state, bool powerOn) {
    connectToHatch();

    BLERemoteCharacteristic* remoteCharacteristic = getCharacteristic(serviceUUID, charUUID);
    if (remoteCharacteristic == nullptr) {
        Serial.println("Couldn't connect to Hatch!");
        return;
    }

    if (state) {
        Serial.println("Turning on white noise and light...");
        remoteCharacteristic->writeValue(TURN_ON);
    } else {
        Serial.println("Turning off noise and light...");
        remoteCharacteristic->writeValue(TURN_OFF);
    }

    if (powerOn) {
        Serial.println("Turning power on");
        remoteCharacteristic->writeValue(POWER_ON);
    }

    disconnectFromHatch();
    mqttPublishState(state);
}

void mqttPublishState() {
    if (millis() - lastMqttStateUpdateMillis > stateUpdateIntervalMillis) {
        lastMqttStateUpdateMillis = millis();
    } else {
        return;
    }

    connectToHatch();

    BLERemoteCharacteristic* remoteCharacteristic = getCharacteristic(feedbackServiceUUID, feedbackCharUUID);
    if (remoteCharacteristic == nullptr) {
        Serial.println("Couldn't connect to Hatch!");
        return;
    }

    const char* feedback = remoteCharacteristic->readValue().c_str();
    bool powerState = decodePowerState(feedback);
    Serial.printf("Hatch is currently %s\r\n", powerState ? "ON" : "OFF");
    disconnectFromHatch();
    if (!powerState) {
        Serial.println("Turning power on");
        // Turn the power on, but keep it in "off" mode.
        setDeviceStateActually(false, true);
    }
    mqttPublishState(powerState);
}

void setDeviceState(bool state) {
    Serial.printf("Will set state to %s\r\n", state ? "ON" : "OFF");
    changeDeviceState = true;
    newDeviceState = state;
}

void mqttMessageReceivedCallback(char* topic, char* message) {
    if (strcmp(message, "ON") == 0) {
        setDeviceState(true);
    } else if (strcmp(message, "OFF") == 0) {
        setDeviceState(false);
    } else {
        Serial.println("Invalid MQTT command");
    }
}

void mqttPostConnectCallback(PubSubClient* client) {
    client->subscribe(MQTT_CONTROL_TOPIC);
    Serial.printf("Connected to broker and subscribed to topic %s\r\n", MQTT_CONTROL_TOPIC);
}

void doSetup() {
    pinMode(BUTTON_BUILTIN, INPUT);

    BLEDevice::init("HatchRestClient");
}

void doLoop(unsigned long currentMillis) {
    unsigned int buttonState = digitalRead(BUTTON_BUILTIN);

    if (buttonState != lastButtonState) {
        lastButtonState = buttonState;

        if (buttonState == 0) {
            // The button is active low, so this is the beginning of a press/hold.
            buttonPressStartTime = currentMillis;
        } else if (buttonState == 1) {
            // If the button was released within the threshold, turn on the device.
            if (currentMillis - buttonPressStartTime < shortPressMillis) {
                setDeviceState(true);
            }

            // Reset the button press timer when the button is released.
            buttonPressStartTime = 0;
        }
    }

    if (buttonPressStartTime != 0 && currentMillis - buttonPressStartTime > longHoldMillis) {
        // Once the button has been held longer than the threshold, turn off the device.
        setDeviceState(false);
        // Consider the button press over once it's triggered a long hold.
        buttonPressStartTime = 0;
    }

    if (changeDeviceState) {
        setDeviceStateActually(newDeviceState, false);
        changeDeviceState = false;
    }
}
