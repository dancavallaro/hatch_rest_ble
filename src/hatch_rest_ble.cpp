#include <Arduino.h>
#include <NimBLEDevice.h>
#include "common.h"

#define BUTTON_BUILTIN 39

#define ALEXA_DEVICE_NAME "hatch"

// Unique ID to use for the Alexa device. Don't change this!
// This ID allow Alexa to uniquely identify the device, including
// when the name changes. This will also allow this firmware to run on
// a different controller but appear as the same device to Alexa.
#define ALEXA_DEVICE_UNIQUE_ID "e0:4c:a9:40:33:c2:62:03-00"

// Identifiers for connecting to the Hatch over BLE
static BLEAddress deviceAddress("ef:d8:7b:5c:59:92", 1);
static BLEUUID serviceUUID("02240001-5efd-47eb-9c1a-de53f7a2b232");
static BLEUUID    charUUID("02240002-5efd-47eb-9c1a-de53f7a2b232");

// Commands for controlling the Hatch over BLE
static std::string POWER_OFF = "SI00";
static std::string POWER_ON = "SI01";
static std::string SET_FAVORITE = "SP02";

unsigned int lastButtonState = 1;
unsigned long buttonPressStartTime = 0;
// A short press must be shorter than this threshold and turns the device on.
const unsigned int shortPressMillis = 1000;
// A long hold must be longer than this threshold and turns the device off.
const unsigned int longHoldMillis = 2000;

bool changeDeviceState = false;
bool newDeviceState = false;

static NimBLEClient* client = nullptr;
static BLERemoteCharacteristic* remoteCharacteristic;

const char *MQTT_CONTROL_TOPIC = "nursery/hatch";
const char *MQTT_STATE_TOPIC = "nursery/hatch/state";

bool connectToHatch() {
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

  BLERemoteService* remoteService = client->getService(serviceUUID);
  if (remoteService == nullptr) {
    Serial.println("Failed to find our service UUID");
    client->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  remoteCharacteristic = remoteService->getCharacteristic(charUUID);
  if (remoteCharacteristic == nullptr) {
    Serial.println("Failed to find our characteristic UUID");
    client->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  Serial.println("Ready to control device!");
  return true;
}

void disconnectFromHatch() {
  Serial.println("Disconnecting from Hatch...");
  client->disconnect();
}

void setDeviceState(bool state) {
  Serial.printf("Will set state to %s\r\n", state ? "ON" : "OFF");
  changeDeviceState = true;
  newDeviceState = state;
}

void setDeviceStateActually(bool state) {
  if (!connectToHatch()) {
    Serial.println("Couldn't connect to Hatch!");
    return;
  }

  if (state) {
    Serial.println("Turning Hatch Rest on...");
    // When we tap the touch ring on the device to turn it on and off, it doesn't
    // *actually* cycle the power, it's really just cycling through the presets.
    // one preset is the ocean sound, and the other preset has 0 volume and changes
    // the track to "none". This means that if the device was last turned off by
    // physically touching the ring, simply sending a power on command won't start
    // playing sound, since it's still set to the no-sound preset. So when turning
    // it on we send the "set favorite" command to make sure that the right track is
    // playing, regardless of how it was last turned off. Note that when turning 
    // it off we *can* simply send the power off command, and that'll work regardless
    // of how it was turned on.
    remoteCharacteristic->writeValue(SET_FAVORITE);
    remoteCharacteristic->writeValue(POWER_ON);
  } else {
    Serial.println("Turning Hatch Rest off...");
    remoteCharacteristic->writeValue(POWER_OFF);
  }

  disconnectFromHatch();

  const char *newState = state ? "ON" : "OFF";
  mqttClient()->publish(MQTT_STATE_TOPIC, newState);
}

void mqttMessageReceivedCallback(char* topic, byte* payload, unsigned int length) {
  char response[length+1];
  
  for (int i = 0; i < length; i++) {
    response[i] = (char)payload[i];
  }
  response[length] = '\0';

  Serial.printf("[MQTT] Message arrived on topic [%s]: [%s]\r\n", topic, response);

  if (strcmp(response, "ON") == 0) {
    setDeviceState(true);
  } else if (strcmp(response, "OFF") == 0) {
    setDeviceState(false);
  } else {
    Serial.println("Invalid MQTT command");
  }
}

void mqttPostConnectCallback(PubSubClient* client) {
  client->subscribe(MQTT_CONTROL_TOPIC);
  Serial.printf("Connected to broker and subscribed to topic %s\r\n", MQTT_CONTROL_TOPIC);
}

void alexaAddDevices(fauxmoESP* fauxmo) {
  unsigned char deviceId = fauxmo->addDevice(ALEXA_DEVICE_NAME);
  fauxmo->setDeviceUniqueId(deviceId, ALEXA_DEVICE_UNIQUE_ID);
}

void alexaOnSetState(unsigned char device_id, const char * device_name, bool state, unsigned char value) {
  Serial.printf("[Alexa] Device #%d (%s) state: %s\r\n", device_id, device_name, state ? "ON" : "OFF");
  
  if (strcmp(device_name, ALEXA_DEVICE_NAME) == 0) {
    setDeviceState(state);
  }
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
    setDeviceStateActually(newDeviceState);
    changeDeviceState = false;
  }
}
