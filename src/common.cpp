#include <Arduino.h>
#include "common.h"

#ifdef ENABLE_WIFI
#include <WiFi.h>

void connectWifi() {
  Serial.printf("Connecting to wifi network %s...\r\n", WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }

  Serial.printf("Connected to wifi with IP address: %s\r\n", WiFi.localIP().toString());
}
#endif

#ifdef ENABLE_OTA
#include <ArduinoOTA.h>

void setupOta() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA
    .onStart([]() {
      Serial.println("Starting OTA update");
    })
    .onEnd([]() {
      Serial.println("\nFinished OTA update");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("OTA update failed with error [%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
      else Serial.println("(unknown)");
    });
  ArduinoOTA.begin();

  Serial.println("Done initializing OTA");
}
#endif

#ifdef ENABLE_MQTT
#include <PubSubClient.h>

unsigned long lastMqttConnectionAttempt = 0;

WiFiClient wifiClient;
PubSubClient _mqttClient(wifiClient);

PubSubClient* mqttClient() {
  return &_mqttClient;
}

void connectToMqtt() {
  lastMqttConnectionAttempt = millis();
  Serial.println("Trying to connect to MQTT broker");

  if (_mqttClient.connect(MQTT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
    mqttPostConnectCallback(&_mqttClient);
  } else {
    Serial.println("Couldn't connect to MQTT broker!");
  }
}

void setupMqtt() {
  _mqttClient.setServer(MQTT_BROKER_HOSTNAME, 1883);
  _mqttClient.setCallback(mqttMessageReceivedCallback);
  connectToMqtt();
}
#endif

#ifdef ENABLE_ALEXA
#include <fauxmoESP.h>

fauxmoESP fauxmo;

void setupAlexa() {
  Serial.println("Setting up Alexa devices");

  alexaAddDevices(&fauxmo);
  fauxmo.setPort(80);
  fauxmo.enable(true);
  fauxmo.onSetState(alexaOnSetState);

  Serial.println("Done setting up Alexa devices");
}
#endif

void setup() {
  Serial.begin(115200);

  #ifdef ENABLE_WIFI
  connectWifi();
  #endif

  #ifdef ENABLE_OTA
  setupOta();
  #endif

  #ifdef ENABLE_MQTT
  setupMqtt();
  #endif

  #ifdef ENABLE_ALEXA
  setupAlexa();
  #endif

  doSetup();
}

void loop() {
  const unsigned long currentMillis = millis();

  #ifdef ENABLE_OTA
  ArduinoOTA.handle();
  #endif

  #ifdef ENABLE_MQTT
  if (!_mqttClient.connected()) {
    // Wait a second in between attempts to connect to broker.
    if (currentMillis - lastMqttConnectionAttempt > 1000) {
      connectToMqtt();
    }
  }
  _mqttClient.loop();
  #endif

  #ifdef ENABLE_ALEXA
  fauxmo.handle();
  #endif
  
  doLoop(currentMillis);
}