#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <fauxmoESP.h>

#define BUTTON_BUILTIN 0

#define WIFI_SSID "dtcnet"
#define WIFI_PASSWORD "danandlauren"

#define ALEXA_DEVICE_NAME "hatch"

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
const unsigned int shortPressMillis = 1000;
const unsigned int longHoldMillis = 2000;

bool changeDeviceState = false;
bool newDeviceState = false;

static boolean connected = false;
static BLERemoteCharacteristic* remoteCharacteristic;

const unsigned int connectionAttemptInterval = 5000;
unsigned long lastConnectionAttempt = -1;

fauxmoESP fauxmo;

void disconnected(String message) {
  Serial.printf("Connection to Hatch Rest lost! (%s)\r\n", message);
  digitalWrite(LED_BUILTIN, 0);
  connected = false;
}

class HatchRestClientCallbacks : public BLEClientCallbacks {
  void onDisconnect(BLEClient* client) {
    disconnected("via callback");
  }

  // We don't use this callback but it needs to be defined
  void onConnect(BLEClient* client) { }
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
  NimBLEClient* client = BLEDevice::createClient();
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

  fauxmo.addDevice(ALEXA_DEVICE_NAME);
  fauxmo.setPort(80);
  fauxmo.enable(true);

  fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
    if (strcmp(device_name, ALEXA_DEVICE_NAME) == 0) {
      Serial.printf("Device #%d (%s) state: %s\r\n", device_id, device_name, state ? "ON" : "OFF");
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
  const unsigned int currentMillis = millis();

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

      buttonPressStartTime = -1;
    }
  }

  if (buttonPressStartTime != -1 && 
      currentMillis - buttonPressStartTime > longHoldMillis) {
    setDeviceState(false);
    // Consider the button press over once it's triggered a long hold.
    buttonPressStartTime = -1;
  }

  if (connected) {
    if (!remoteCharacteristic->getRemoteService()->getClient()->isConnected()) {
      disconnected("via detection");
    } else if (changeDeviceState) {
      setDeviceStateActually(newDeviceState);
      changeDeviceState = false;
    }
  } else {
    unsigned long currentTimeMillis = millis();

    if (lastConnectionAttempt == -1 || 
        currentTimeMillis - lastConnectionAttempt > connectionAttemptInterval) {
      lastConnectionAttempt = currentTimeMillis;
      connectToHatchRest();
    }
  }
}
