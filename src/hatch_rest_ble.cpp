#include <Arduino.h>
#include <BLEDevice.h>

#define BUTTON_BUILTIN 0

unsigned int lastButtonState = 1;

// The device we want to connect to.
static BLEAddress deviceAddress("ef:d8:7b:5c:59:92");
static esp_ble_addr_type_t addressType = BLE_ADDR_TYPE_RANDOM;
// The remote service we wish to connect to.
static BLEUUID serviceUUID("02240001-5efd-47eb-9c1a-de53f7a2b232");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("02240002-5efd-47eb-9c1a-de53f7a2b232");

static String POWER_OFF = "SI00";
static String POWER_ON = "SI01";

static boolean doConnect = false;
static boolean connected = false;
static boolean deviceOn = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

bool connectToServer() {
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remote BLE Server.
    pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");
    pClient->setMTU(517); //set client to request maximum MTU from server (default is 23 otherwise)
  
    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");

    connected = true;
    return true;
}

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    if (advertisedDevice.getAddress().equals(deviceAddress)) {
      Serial.println("Found device with the expected MAC address!");
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    }
  }
};


void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");

  pinMode(BUTTON_BUILTIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 0);

  BLEDevice::init("HatchRestClient");
}


// This is the Arduino main loop function.
void loop() {
  bool togglePower = false;
  unsigned int buttonState = digitalRead(BUTTON_BUILTIN);

  if (buttonState != lastButtonState) {
    lastButtonState = buttonState;

    // The button is active low, and I want to change the mode when the button is
    // released, so we'll change the mode when the button state comes back high.
    if (buttonState == 1) {
      togglePower = true;
    }
  }

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  if (connected && togglePower) {
    if (deviceOn) {
      // If it's on, turn it off
      Serial.println("(NOT!) Turning Hatch Rest off...");
      // TODO: uncomment
      //pRemoteCharacteristic->writeValue(POWER_OFF.c_str(), POWER_OFF.length());
    } else {
      // If it's off, turn it on
      Serial.println("(NOT!) Turning Hatch Rest on...");
      // TODO: uncomment
      //pRemoteCharacteristic->writeValue(POWER_ON.c_str(), POWER_ON.length());
    }

    deviceOn = !deviceOn;
  } else if (!connected) {
    // TODO: refactor this code into a separate method
    BLEClient* client = BLEDevice::createClient();

    if (!client->connect(deviceAddress, addressType)) {
      // TODO: do something here so that we'll try again in a bit
      Serial.println("Failed to connect to device!");
    } else if (client->isConnected()) {
      // TODO: do stuff here
      Serial.println("Connected to device!");
      connected = true;
      digitalWrite(LED_BUILTIN, 1);
    } else {
      // TODO: do something here too
      Serial.println("Connection appeared to be successful but now we're disconnected");
    }
  }
}
