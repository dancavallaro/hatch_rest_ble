#ifdef ENABLE_OTA
#ifndef ENABLE_WIFI
#define ENABLE_WIFI
#endif
#endif

#ifdef ENABLE_MQTT
#ifndef ENABLE_WIFI
#define ENABLE_WIFI
#endif
#endif

#ifdef ENABLE_ALEXA
#ifndef ENABLE_WIFI
#define ENABLE_WIFI
#endif
#endif

#ifdef ENABLE_WIFI
#define WIFI_SSID "dtcnet"
#define WIFI_PASSWORD "danandlauren"
#endif

#ifdef ENABLE_MQTT
#include <PubSubClient.h>

#define MQTT_BROKER_HOSTNAME "rpi.local"

void mqttMessageReceivedCallback(char* topic, byte* payload, unsigned int length);
void mqttPostConnectCallback(PubSubClient* client);
PubSubClient* mqttClient();
#endif

#ifdef ENABLE_ALEXA
#include <fauxmoESP.h>

void alexaAddDevices(fauxmoESP* fauxmo);
void alexaOnSetState(unsigned char device_id, const char * device_name, bool state, unsigned char value);
#endif

void doSetup();
void doLoop(unsigned long currentMillis);