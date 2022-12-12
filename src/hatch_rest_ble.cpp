#include <Arduino.h>
#include <BLEDevice.h>

#define BUTTON_BUILTIN 0

unsigned int lastButtonState = 1;

unsigned int ledState = 0;
const long ledBlinkDurationMillis = 100;
unsigned long ledLastBlinkTimeMillis = 0;

// The device we want to connect to.
static BLEAddress deviceAddress("ef:d8:7b:5c:59:92");
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

  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
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

  unsigned long currentTimeMillis = millis();

  // TODO: only do this while still scanning
  // TODO: switch to steady on once connected
  if (currentTimeMillis - ledLastBlinkTimeMillis > ledBlinkDurationMillis) {
    ledState = 1 - ledState;
    digitalWrite(LED_BUILTIN, ledState);
    ledLastBlinkTimeMillis = currentTimeMillis;
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
  } else if (!connected && doScan){
    BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  }
}
