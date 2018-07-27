#include <Arduino.h>
#include <string>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "Bleeper.h"
#include <SimpleDHT.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

int pinDHT11 = 4;
SimpleDHT11 dht11;

class WifiConfig : public Configuration
{
public:
  persistentStringVar(ssid, "ssid");
  persistentStringVar(password, "password");
};

class Config : public RootConfiguration
{
public:
  stringVar(name, "home-control");
  subconfig(WifiConfig, wifi);
};

Config C;

char nodeid[10] = "";
int r1 = sprintf(nodeid, "%08X", ESP.getChipId());

char node_heart_topic[30] = "";
int r7 = sprintf(node_heart_topic, "esp/%08X/heart", ESP.getChipId());
char node_status_topic[30] = "";
int r2 = sprintf(node_status_topic, "esp/%08X/ison", ESP.getChipId());
char node_switch_control_topic[30] = "";
int r4 = sprintf(node_switch_control_topic, "esp/%08X/ac/control", ESP.getChipId());
char node_switch_state_topic[30] = "";
int r6 = sprintf(node_switch_state_topic, "esp/%08X/ac/state", ESP.getChipId());
///////////////////////////
WiFiClient espClient;
PubSubClient client(espClient);
void callback(char *topic, byte *payload, unsigned int length)
{
  String _topic = topic;
  Serial.print("msg from topic: ");
  Serial.print(topic);
  Serial.print(" payload: ");
  String msg = "";
  for (int i = 0; i < length; i++)
  {
    msg += (char)payload[i];
  }
  Serial.println(msg);
  if (_topic == node_switch_control_topic)
  {
    Serial.print("switch control  ");
    
  }
  else if (_topic == node_heart_topic)
  {
    int newval = msg.toInt() + 8;
    char *_newval;
    sprintf(_newval, "%d", newval);
    client.publish(node_status_topic, _newval);
  }
}

boolean reconnect()
{
  if (client.connect(nodeid, "", ""))
  {
    client.subscribe(node_heart_topic);
    client.publish(node_status_topic, "online");
    Serial.println(node_status_topic);
    client.subscribe(node_switch_control_topic);
    Serial.println(node_switch_control_topic);
  }
  return client.connected();
}

long lastReconnectAttempt = 0;
int count = 0;

void setup()
{
  Serial.begin(115200);
  Serial.printf("Flash real id:");
  Serial.println(nodeid);
  Bleeper
      .verbose()
      .configuration
      .set(&C)
      .done()
      .configurationInterface
      .addDefaultWebServer()
      .done()
      .connection
      .setSingleConnectionFromPriorityList({new Wifi(&C.wifi.ssid, &C.wifi.password),
                                            new AP()})
      .done()
      .storage
      .setDefault()
      .done()
      .init();
  client.setServer("apcan.cn", 1883);
  client.setCallback(callback);
}

float hnow = 0;
float tnow = 0;

void loop()
{
  Bleeper.handle();
  if (!client.connected())
  {
    long now = millis();
    if (now - lastReconnectAttempt > 5000)
    {
      Serial.println("mqtt now start to connecting.");
      lastReconnectAttempt = now;
      Serial.print(".");
      if (reconnect())
      {
        lastReconnectAttempt = 0;
      }
    }
  }
  else
  {
    client.loop();
    long now = millis();
    byte temperature = 0;
    byte humidity = 0;
    dht11.read(pinDHT11, &temperature, &humidity, NULL);
    Serial.print((int)temperature);
    Serial.print(" *C, ");
    Serial.print((int)humidity);
    Serial.println(" H");
  }
}
