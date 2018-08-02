#include <Arduino.h>
#include <string>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "Bleeper.h"
#include <ESP8266WiFi.h>
#include <SimpleDHT.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

#define RX 3
#define TX 1
#define DHT11 4

SimpleDHT11 dht11;

SoftwareSerial swSer(RX, TX, false, 256);
byte open[300] = {0xA1, 0xF1,0x01,0xFB,0x08};

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

char node_10_control_topic[30] = "";
int r10 = sprintf(node_10_control_topic, "esp/%08X/10/control", ESP.getChipId());
char node_16_control_topic[30] = "";
int r16 = sprintf(node_16_control_topic, "esp/%08X/16/control", ESP.getChipId());
///////////////////////////

void ShowCommand(int index)  {
  Serial.print(millis());
  Serial.print(" OUT>>");
  for (int i = 0; i < index; i++) {
    Serial.print( (open[i] < 0x10 ? " 0" : " "));
    Serial.print(open[i], HEX);
  }
  Serial.println();
}

void checkReturn() {
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

void SendCommand(bool checkResult, int index) {
  ShowCommand(index);
  //串口
  swSer.begin(9600);
  pinMode(RX, INPUT);
  pinMode(TX, OUTPUT);

 
  swSer.write(open, index);
  swSer.flush();
  if (checkResult) checkReturn();
}

WiFiClient espClient;
PubSubClient client(espClient);
void callback(char *topic, byte *payload, unsigned int length)
{
  String _topic = topic;
  Serial.print("msg from topic: ");
  Serial.print(topic);
  Serial.print(" payload: ");

  Serial.println();
  char msg[length + 1];
  for (int i = 0; i < length; i++) {
    msg[i] = (char)payload[i];
  }
  msg[length] = '\0';
  String msgText(msg);
  Serial.print(msgText);
  Serial.println();

  if (_topic == node_10_control_topic || _topic == node_16_control_topic) {
    char delimiter[] = " ";
    char* command = strtok(msg,  delimiter);
    int index = 0;
    while (command != NULL) {
      if (_topic == node_10_control_topic) {
        open[index] = strtol(command, NULL, 10);
      }else {
        open[index] = strtol(command, NULL, 16);
      }
      
      command = strtok(NULL, delimiter);
      index ++;
    }
    SendCommand(true, index);
  }

}

long lastReconnectAttempt = 0;
long lastScheduleTime500 = millis();
long lastScheduleTime2000 = millis();
long lastScheduleTime5000 = millis();
int count = 0;
const char *user = (&C.mqtt.user) -> c_str();
const char *pass = (&C.mqtt.pass) -> c_str();
void setup()
{
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  Serial.printf("Flash real id:");
  Serial.println(nodeid);
  
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
    client.subscribe(node_10_control_topic);
    client.subscribe(node_16_control_topic);
    Serial.println(node_switch_control_topic);
    Serial.println(node_10_control_topic);
    Serial.println(node_16_control_topic);
  }
  delay(100);
  return client.connected();
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
  // StaticJsonBuffer<50> jsonBuffer;
  // JsonObject& root = jsonBuffer.createObject();
  // root["temperature"] = (int)temperature;
  // root["thumidityime"] = (int)humidity;
  // String output;
  // root.printTo(output);
  // const char *res = output.c_str();
  // client.publish(node_th_topic, res);



}

void schedule500() {
  long now = millis();

  if ((now - lastScheduleTime500) < 500) return;
  lastScheduleTime500 = now;
  // 
  Serial.println("schedule500");


}

void schedule2000() {
  long now = millis();
  if ((now - lastScheduleTime2000) < 2000) return;
  lastScheduleTime2000 = now;
  // 
  Serial.println("schedule2000");
  
}

void schedule5000() {
  long now = millis();
  if ((now - lastScheduleTime5000) < 5000) return;
  lastScheduleTime5000 = now;
  // 
  Serial.println("schedule5000");
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
    schedule2000();
    schedule500();
    schedule5000();
    client.loop();
    
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

