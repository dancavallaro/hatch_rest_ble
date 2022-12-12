#include <Arduino.h>
#include <BLEDevice.h>

#define BUTTON_BUILTIN 0

static String POWER_OFF = "SI00";
static String POWER_ON = "SI01";

unsigned int lastButtonState = 1;

// The device we want to connect to.
static BLEAddress deviceAddress("ef:d8:7b:5c:59:92");
static esp_ble_addr_type_t addressType = BLE_ADDR_TYPE_RANDOM;
// The remote service we wish to connect to.
static BLEUUID serviceUUID("02240001-5efd-47eb-9c1a-de53f7a2b232");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("02240002-5efd-47eb-9c1a-de53f7a2b232");

static BLEClient* client = nullptr;
static boolean connected = false;
// TODO: can i read the current state of this? 
// TODO: er, i guess this just goes away when i integrate with alexa, don't need the button
static boolean deviceOn = false;
static BLERemoteCharacteristic* remoteCharacteristic;

const unsigned int connectionAttemptInterval = 5000;
unsigned long lastConnectionAttempt = -1;

void disconnected(String message) {
  Serial.printf("Connection to Hatch Rest lost! (%s)\r\n", message);
  digitalWrite(LED_BUILTIN, 0);
  connected = false;
}

class HatchRestClientCallbacks : public BLEClientCallbacks {
  // We don't use this callback but it needs to be defined
  void onConnect(BLEClient* client) { }

  void onDisconnect(BLEClient* client) {
    disconnected("via callback");
  }
};

void connectToHatchRest() {
  if (client != nullptr) {
    delete client;
  }

  client = BLEDevice::createClient();
  Serial.println("Attempting to connect to Hatch Rest...");

  if (!client->connect(deviceAddress, addressType)) {
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

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_BUILTIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 0);

  BLEDevice::init("HatchRestClient");
}

void loop() {
  bool togglePower = false;
  unsigned int buttonState = digitalRead(BUTTON_BUILTIN);

  if (buttonState != lastButtonState) {
    lastButtonState = buttonState;

    // The button is active low, and I want to toggle power when the button is
    // released, so we'll change the mode when the button state comes back high.
    if (buttonState == 1) {
      togglePower = true;
    }
  }

  if (connected) {
    if (!client->isConnected()) {
      disconnected("via detection");
    } else if (togglePower) {
      if (deviceOn) {
        // If it's on, turn it off
        Serial.println("Turning Hatch Rest off...");
        remoteCharacteristic->writeValue(POWER_OFF.c_str(), POWER_OFF.length());
      } else {
        // If it's off, turn it on
        Serial.println("Turning Hatch Rest on...");
        remoteCharacteristic->writeValue(POWER_ON.c_str(), POWER_ON.length());
      }

      deviceOn = !deviceOn;
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
