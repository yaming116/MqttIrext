#include <Arduino.h>
#include <string>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "Bleeper.h"
#include <ESP8266WiFi.h>
#include <SimpleDHT.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>

SimpleDHT11 dht11;

const int RX = 2;
const int TX = 3;
const int DHT11 = 4;

SoftwareSerial swSer(RX, TX, false, 256);
byte open[5] = {0xA1, 0xF1,0x01,0xFB,0x08};

class WifiConfig : public Configuration
{
public:
  persistentStringVar(ssid, "ssid");
  persistentStringVar(password, "password");
};

class MqttConfig: public Configuration {
public:
  persistentStringVar(host, "apcan.cn");
  persistentIntVar(port, 1883);
  persistentStringVar(user, "test");
  persistentStringVar(pass, "111111");
};

class Config : public RootConfiguration
{
public:
  stringVar(name, "home-control");
  subconfig(WifiConfig, wifi);
  subconfig(MqttConfig, mqtt);
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

char node_th_topic[30] = "";
int r8 = sprintf(node_switch_state_topic, "esp/%08X/TH", ESP.getChipId());
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

long lastReconnectAttempt = 0;
long lastScheduleTime = 0;
int count = 0;
const char *user = (&C.mqtt.user) -> c_str();
const char *pass = (&C.mqtt.pass) -> c_str();
void setup()
{
  Serial.begin(115200);
  Serial.printf("Flash real id:");
  Serial.println(nodeid);
  
  //串口
  swSer.begin(9600);
  pinMode(RX, INPUT);
  pinMode(TX, OUTPUT);

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
  
  const char *host = (&C.mqtt.host) -> c_str();
  uint16_t  port = (uint16_t )C.mqtt.port;
  Serial.println(host);
  Serial.println(port);
  Serial.println(user);
  Serial.println(pass);
  client.setServer(host, port);
  client.setCallback(callback);
}

boolean reconnect()
{

  if (client.connect(nodeid, user, pass))
  {
    client.subscribe(node_heart_topic);
    client.publish(node_status_topic, "online");
    Serial.println(node_status_topic);
    client.subscribe(node_switch_control_topic);
    Serial.println(node_switch_control_topic);
  }
  delay(100);
  return client.connected();
}

void ShowCommand()  {
  Serial.print(millis());
  Serial.print(" OUT>>");
  for (int i = 0; i < 5; i++) {
    Serial.print( (open[i] < 0x10 ? " 0" : " "));
    Serial.print(open[i], HEX);
  }
  Serial.println();
}

void checkReturn() {
  unsigned long startMs = millis();
  while ( ((millis() - startMs) < 500) && (!swSer.available()) ) ;
  if (swSer.available()) {
    Serial.print(millis());
    Serial.print(" IN>>>");
    while (swSer.available()) {
      byte ch =  (byte) swSer.read();
      Serial.print((ch < 0x10 ? " 0" : " "));
      Serial.print(ch, HEX);
    }
    Serial.println();
  }
}

void SendCommand(bool checkResult) {
  ShowCommand();
  swSer.write(open, 5);
  if (checkResult) checkReturn();
}


byte lastTemperature = 0;
byte lastHumidity = 0;
void publishTemp() {
  byte temperature = 0;
  byte humidity = 0;
  dht11.read(DHT11, &temperature, &humidity, NULL);

  if (temperature == lastTemperature && humidity == lastHumidity) return;
  Serial.print((int)temperature); 
  Serial.print(" *C, "); 
  Serial.print((int)humidity); 
  Serial.println(" H");

  //mqtt publish
  StaticJsonBuffer<50> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["temperature"] = (int)temperature;
  root["thumidityime"] = (int)humidity;
  String output;
  root.printTo(output);
  const char *res = output.c_str();
  client.publish(node_th_topic, res);



}

void schedule500() {
  long now = millis();

  if ((now - lastScheduleTime) < 500) return;
  lastScheduleTime = now;
  // 
  publishTemp();


}

void schedule2000() {
  long now = millis();
  if ((now - lastScheduleTime) < 2000) return;
  lastScheduleTime = now;
  // 
  
  
}


void connectMqtt() {
  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      Serial.println("mqtt now start to connecting.");
      lastReconnectAttempt = now;
      Serial.print(".");
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  }else {
    client.loop();
    schedule2000();
    schedule500();
  }
}


void loop()
{
  Bleeper.handle();
  auto status = WiFi.status();
  bool isConnected = (status == WL_CONNECTED);
  if (isConnected) {
    connectMqtt();
  }else {
    delay(500);
    Serial.println("Wifi is not connect");
  }
}

