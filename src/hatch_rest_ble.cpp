#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <fauxmoESP.h>

#define BUTTON_BUILTIN 0

#define WIFI_SSID "dtcnet"
#define WIFI_PASSWORD "danandlauren"

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
unsigned long buttonPressStartTime = -1;
// A short press must be shorter than this threshold and turns the device on.
const unsigned int shortPressMillis = 1000;
// A long hold must be longer than this threshold and turns the device off.
const unsigned int longHoldMillis = 2000;
// A long hold longer than this threadhold will cause the ESP32 to disconnect
// from BLE and not reconnect until a reset.
const unsigned int veryLongHoldMillis = 5000;

bool changeDeviceState = false;
bool newDeviceState = false;
bool longHoldTriggered = false;

static boolean connected = false;
static boolean shouldConnect = true;
static NimBLEClient* client = nullptr;
static BLERemoteCharacteristic* remoteCharacteristic;

const unsigned int connectionAttemptInterval = 5000;
unsigned long lastConnectionAttempt = 0;

fauxmoESP fauxmo;

void disconnected(String message) {
  Serial.printf("Connection to Hatch Rest lost! (%s)\r\n", message.c_str());
  digitalWrite(LED_BUILTIN, 0);
  connected = false;
}

class HatchRestClientCallbacks : public BLEClientCallbacks {
  void onDisconnect(BLEClient* client) {
    disconnected("via callback");
  }
};

void connectWifi() {
  Serial.printf("Connecting to wifi network %s...\r\n", WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }

  Serial.printf("Connected to wifi with IP address: %s\r\n", WiFi.localIP().toString());
}

void connectToHatchRest() {
  if (client == nullptr) {
    client = BLEDevice::createClient();
  }

  Serial.println("Attempting to connect to Hatch Rest...");

  if (!client->connect(deviceAddress)) {
    Serial.println("Failed to connect to device!");
    return;
  } else if (!client->isConnected()) {
    Serial.println("Connection appeared to be successful but now we're disconnected");
    return;
  } else {
    Serial.println("Connected to device!");
  }

  BLERemoteService* remoteService = client->getService(serviceUUID);
  if (remoteService == nullptr) {
    Serial.println("Failed to find our service UUID");
    client->disconnect();
    return;
  }
  Serial.println(" - Found our service");

  remoteCharacteristic = remoteService->getCharacteristic(charUUID);
  if (remoteCharacteristic == nullptr) {
    Serial.println("Failed to find our characteristic UUID");
    client->disconnect();
    return;
  }
  Serial.println(" - Found our characteristic");

  Serial.println("Ready to control device!");
  connected = true;
  digitalWrite(LED_BUILTIN, 1);
}

void setDeviceState(bool state) {
  changeDeviceState = true;
  newDeviceState = state;
}

void setDeviceStateActually(bool state) {
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
}

void setupAlexaDevice() {
  Serial.println("Setting up Alexa device");

  unsigned char deviceId = fauxmo.addDevice(ALEXA_DEVICE_NAME);
  fauxmo.setDeviceUniqueId(deviceId, ALEXA_DEVICE_UNIQUE_ID);

  fauxmo.setPort(80);
  fauxmo.enable(true);

  fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
    Serial.printf("Device #%d (%s) state: %s\r\n", device_id, device_name, state ? "ON" : "OFF");
    
    if (strcmp(device_name, ALEXA_DEVICE_NAME) == 0) {
      setDeviceState(state);
    }
  });

  Serial.println("Done setting up Alexa device");
}

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_BUILTIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 0);

  BLEDevice::init("HatchRestClient");

  connectWifi();
  setupAlexaDevice();
}

void loop() {
  fauxmo.handle();

  unsigned int buttonState = digitalRead(BUTTON_BUILTIN);
  const unsigned long currentMillis = millis();

  if (buttonState != lastButtonState) {
    lastButtonState = buttonState;

    if (buttonState == 0) {
      // The button is active low, so this is the beginning of a press/hold.
      buttonPressStartTime = currentMillis;
      longHoldTriggered = false;
    } else if (buttonState == 1) {
      // If the button was released within the threshold, turn on the device.
      if (currentMillis - buttonPressStartTime < shortPressMillis) {
        setDeviceState(true);
      }

      // Reset the button press timer when the button is released.
      buttonPressStartTime = -1;
    }
  }

  if (buttonPressStartTime != -1) {
    const unsigned long buttonPressMillis = currentMillis - buttonPressStartTime;

    if (!longHoldTriggered && buttonPressMillis > longHoldMillis) {
      setDeviceState(false);
      longHoldTriggered = true;
    } else if (buttonPressMillis > veryLongHoldMillis) {
      Serial.println("Disconnecting from BLE and will not reconnect until reset");
      client->disconnect();
      disconnected("via manual button press");

      // Never try reconnecting (until a reset).
      shouldConnect = false;

      // Only reset the button press when a very long hold is triggered.
      // When a long hold is triggered, we want to keep the timer running
      // in case it turns into a very long hold.
      buttonPressStartTime = -1;
    }
  }

  if (connected) {
    if (!client->isConnected()) {
      disconnected("via detection");
    } else if (changeDeviceState) {
      setDeviceStateActually(newDeviceState);
      changeDeviceState = false;
    }
  } else if (shouldConnect) {
    if (lastConnectionAttempt == 0 || 
        currentMillis - lastConnectionAttempt > connectionAttemptInterval) {
      lastConnectionAttempt = currentMillis;
      connectToHatchRest();
    }
  }
}
