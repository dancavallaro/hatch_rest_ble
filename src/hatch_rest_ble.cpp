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

    BLERemoteCharacteristic* remoteCharacteristic = remoteService->getCharacteristic(characteristic);
    if (remoteCharacteristic == nullptr) {
        Serial.println("Failed to find our characteristic UUID");
        client->disconnect();
        return nullptr;
    }

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
            // Sometimes it seems to get stuck in an infinite loop here where the client
            // will never connect. Constructing a new client seems to fix it.
            BLEDevice::deleteClient(client);
            client = BLEDevice::createClient();
        } else if (!client->isConnected()) {
            Serial.println("Connection appeared to be successful but now we're disconnected");
        } else {
            Serial.println("Connected to device!");
            break;
        }

        // If we failed to connect or disconnected, just keep trying after a short delay.
        delay(1000);
    }
}

void disconnectFromHatch() {
    Serial.println("Disconnecting from Hatch...");
    client->disconnect();
}

struct HatchFeedback {
    time_t time;
    struct {
        int r, g, b, brightness;
    } color;
    struct {
        int track, volume;
    } audio;
    bool power;
};

int percent(int num) {
    return round(100 * num / 255);
}

HatchFeedback decodeFeedback(const uint8_t *p) {
    HatchFeedback feedback{};

    // Time is preceded by 0x54
    time_t time = (p[1] << 24) | (p[2] << 16) | (p[3] << 8) | p[4];
    feedback.time = time;

    // Color is preceded by 0x43
    feedback.color.r = p[6];
    feedback.color.g = p[7];
    feedback.color.b = p[8];
    feedback.color.brightness = percent(p[9]);

    // Audio is preceded by 0x53
    feedback.audio.track = p[11];
    feedback.audio.volume = percent(p[12]);

    // Power is preceded by 0x50
    uint8_t powerData = p[14];
    feedback.power = (powerData != 0) && (powerData & 0xc0) == 0;

    return feedback;
}

char hexChars[] = {
        '0', '1', '2', '3',
        '4', '5', '6', '7',
        '8', '9', 'A', 'B',
        'C', 'D', 'E', 'F'
};

void getHexString(const uint8_t* feedback, char* output) {
    for (int i = 0; i < 15; i++) {
        int val = feedback[i];
        output[2*i] = hexChars[val/16];
        output[2*i+1] = hexChars[val%16];
    }
    output[30] = '\0';
}

bool getFeedback(bool verbose) {
    BLERemoteCharacteristic* remoteCharacteristic = getCharacteristic(feedbackServiceUUID, feedbackCharUUID);
    if (remoteCharacteristic == nullptr) {
        Serial.println("Couldn't connect to Hatch!");
        return false;
    }
    const uint8_t* rawFeedback = remoteCharacteristic->readValue().data();
    HatchFeedback feedback = decodeFeedback(rawFeedback);

    char feedbackStr[31];
    getHexString(rawFeedback, feedbackStr);
    Serial.printf("Hatch is currently %s, (feedback: %s)\n",
                  feedback.power ? "ON" : "OFF", feedbackStr);

    if (verbose) {
        Serial.printf("Current time is: %s\n", std::to_string(feedback.time).c_str());
        Serial.printf("Color: red=%d, green=%d, blue=%d, brightness=%d%%\n",
                      feedback.color.r, feedback.color.g, feedback.color.b, feedback.color.brightness);
        Serial.printf("Audio: track=%d, volume=%d%%\n", feedback.audio.track, feedback.audio.volume);
    }

    return feedback.power;
}

void setDeviceStateActually(const std::string& command) {
    BLERemoteCharacteristic* remoteCharacteristic = getCharacteristic(serviceUUID, charUUID);
    if (remoteCharacteristic == nullptr) {
        Serial.println("Couldn't connect to Hatch!");
        return;
    }

    if (command == POWER_ON && getFeedback(false)) {
        Serial.println("Hatch is already on, won't send power on command");
        return;
    }

    Serial.printf("Sending command %s\n", command.c_str());
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
    Serial.printf("Will send command %s\n", command.c_str());
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
    } else if (message[0] == 'S' && (message[1] == 'I' || message[1] == 'C' ||
            message[1] == 'N' || message[1] == 'V' || message[1] == 'P')) {
        // SI = power, SC = color, SN = track, SV = volume, SP = favorite
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
    Serial.printf("Connected to broker and subscribed to topic %s\n", MQTT_CONTROL_TOPIC);
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
        getFeedback(true);
        shouldGetFeedback = false;
    }

    if (changeDeviceState || shouldGetFeedback) {
        disconnectFromHatch();
    }
}
