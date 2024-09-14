#include <Arduino.h>
#include <NimBLEDevice.h>
#include <common.h>

// Identifiers for connecting to the Hatch over BLE
static BLEAddress deviceAddress("ef:d8:7b:5c:59:92", 1);
static BLEUUID serviceUUID("02240001-5efd-47eb-9c1a-de53f7a2b232");
static BLEUUID    charUUID("02240002-5efd-47eb-9c1a-de53f7a2b232");
static BLEUUID feedbackServiceUUID("02260001-5efd-47eb-9c1a-de53f7a2b232");
static BLEUUID    feedbackCharUUID("02260002-5efd-47eb-9c1a-de53f7a2b232");

// No song and no light ("off")
static std::string TURN_OFF = "SP05";
// Ocean sound with red light
static std::string TURN_ON = "SP03";
// Turn the power on
static std::string POWER_ON = "SI01";

unsigned int lastMqttStateUpdateMillis = 0;
const unsigned int stateUpdateIntervalMillis = 60 * 1000;

bool changeDeviceState = false;
bool shouldGetFeedback = false;
std::string newDeviceState;

static NimBLEClient* client = nullptr;

const char *MQTT_CONTROL_TOPIC = "nursery/hatch";

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

char hexChars[] = {
        '0', '1', '2', '3',
        '4', '5', '6', '7',
        '8', '9', 'A', 'B',
        'C', 'D', 'E', 'F'
};

void displayFeedback(const char* feedback, char* output) {
    for (int i = 0; i < 15; i++) {
        int val = (unsigned char)feedback[i];
        output[2*i] = hexChars[val/16];
        output[2*i+1] = hexChars[val%16];
    }
    output[30] = '\0';
}

bool getFeedback() {
    BLERemoteCharacteristic* remoteCharacteristic = getCharacteristic(feedbackServiceUUID, feedbackCharUUID);
    if (remoteCharacteristic == nullptr) {
        Serial.println("Couldn't connect to Hatch!");
        return false;
    }
    const char* feedback = remoteCharacteristic->readValue().c_str();

    bool powerState = decodePowerState(feedback);
    char feedbackStr[31];
    displayFeedback(feedback, feedbackStr);
    Serial.printf("Hatch is currently %s (feedback: %s)\r\n", powerState ? "ON" : "OFF", feedbackStr);

    return powerState;
}

void setDeviceStateActually(const std::string& command) {
    BLERemoteCharacteristic* remoteCharacteristic = getCharacteristic(serviceUUID, charUUID);
    if (remoteCharacteristic == nullptr) {
        Serial.println("Couldn't connect to Hatch!");
        return;
    }

    if (command == POWER_ON && getFeedback()) {
        Serial.println("Hatch is already on, won't send power on command");
        return;
    }

    Serial.printf("Sending command %s\r\n", command.c_str());
    remoteCharacteristic->writeValue(command);
}

void mqttPublishState() {
    if (millis() - lastMqttStateUpdateMillis > stateUpdateIntervalMillis) {
        lastMqttStateUpdateMillis = millis();
    } else {
        return;
    }
}

void setDeviceState(const std::string& command) {
    Serial.printf("Will send command %s\r\n", command.c_str());
    changeDeviceState = true;
    newDeviceState = command;
}

void mqttMessageReceivedCallback(char* topic, char* message) {
    if (strcmp(message, "ON") == 0) {
        Serial.println("Turning white noise and red light on");
        setDeviceState(TURN_ON);
    } else if (strcmp(message, "OFF") == 0) {
        Serial.println("Turning sound and light off");
        setDeviceState(TURN_OFF);
    } else if (strncmp(message, "SP", 2) == 0 || strncmp(message, "SI", 2) == 0) {
        std::string command = message;
        setDeviceState(command);
    } else if (strcmp(message, "feedback") == 0) {
        shouldGetFeedback = true;
    } else {
        Serial.println("Invalid MQTT command");
    }
}

void mqttPostConnectCallback(PubSubClient* client) {
    client->subscribe(MQTT_CONTROL_TOPIC);
    Serial.printf("Connected to broker and subscribed to topic %s\r\n", MQTT_CONTROL_TOPIC);
}

void doSetup() {
    BLEDevice::init("HatchRestClient");
}

void doLoop(unsigned long currentMillis) {
    if (changeDeviceState || shouldGetFeedback) {
        connectToHatch();
    }

    if (changeDeviceState) {
        setDeviceStateActually(newDeviceState);
        changeDeviceState = false;
        newDeviceState = "";
    }

    if (shouldGetFeedback) {
        getFeedback();
        shouldGetFeedback = false;
    }

    if (changeDeviceState || shouldGetFeedback) {
        disconnectFromHatch();
    }
}
