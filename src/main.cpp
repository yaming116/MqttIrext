#include <Arduino.h>

#include <RCSwitch.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "Bleeper.h"

RCSwitch mySwitch = RCSwitch();

int lightpin=5;
int doorstatepin=4;

class WifiConfig: public Configuration {
public:
  persistentStringVar(ssid, "ssid");
  persistentStringVar(password, "password");
};

class Config: public RootConfiguration {
public:
  stringVar(name, "home-control");
  subconfig(WifiConfig, wifi);
};

Config C;

char nodeid[10]="";
int r1=sprintf(nodeid, "%08X",ESP.getFlashChipId()); 

char node_status_topic[30]="";
int r2=sprintf(node_status_topic, "esp/%08X/ison",ESP.getFlashChipId()); 
char node_cardoor_control_topic[30]="";
int r3=sprintf(node_cardoor_control_topic, "esp/%08X/cardoor/control",ESP.getFlashChipId()); 
char node_light_control_topic[30]="";
int r4=sprintf(node_light_control_topic, "esp/%08X/light/control",ESP.getFlashChipId()); 
char node_cardoor_state_topic[30]="";
int r5=sprintf(node_cardoor_state_topic, "esp/%08X/cardoor/state",ESP.getFlashChipId()); 
char node_light_state_topic[30]="";
int r6=sprintf(node_light_state_topic, "esp/%08X/light/state",ESP.getFlashChipId()); 
///////////////////////////
WiFiClient espClient;
PubSubClient client( espClient);
void callback(char* topic, byte* payload, unsigned int length) {
  String _topic=topic;
	Serial.print("msg from topic: ");
	Serial.print(topic);
  Serial.print(" payload: ");
  String msg="";
  for (int i=0;i<length;i++) {
    msg+=(char)payload[i];
  }
	Serial.println(msg);
  if (_topic==node_cardoor_control_topic){
    Serial.println("cardoor control");
  }else if (_topic==node_light_control_topic){
    Serial.print("light control  ");
    if (msg=="on"){
      Serial.println("on");
      digitalWrite(lightpin,HIGH);
      client.publish(node_light_state_topic,"on");
    }else {
      Serial.println("off");
      digitalWrite(lightpin,LOW);
      client.publish(node_light_state_topic,"off");
    }
  }
}



boolean reconnect() {
  if (client.connect("apcan_home_node", "esp", "esp8266")) {
      client.publish(node_status_topic,"online");
      Serial.println(node_status_topic);
      client.subscribe(node_cardoor_control_topic);
      Serial.println(node_cardoor_control_topic);
      client.subscribe(node_light_control_topic);
      Serial.println(node_light_control_topic);
  }
  return client.connected();
}


long lastReconnectAttempt = 0;
int count=0;

void setup() {
	Serial.begin(115200);
  mySwitch.enableTransmit(14);
  mySwitch.setPulseLength(320);
  Serial.printf("Flash real id:");
	Serial.println(nodeid);

  //light_pin_init
  pinMode(lightpin,OUTPUT);
  digitalWrite(lightpin,LOW);
  //door_state_pin_init
  pinMode(doorstatepin,INPUT_PULLUP);

  Bleeper
    .verbose()
    .configuration
      .set(&C)
      .done()
    .configurationInterface
      .addDefaultWebServer()
      .done()
    .connection
      .setSingleConnectionFromPriorityList({
          new Wifi(&C.wifi.ssid, &C.wifi.password),
          new AP() 
      })
      .done()
    .storage
      .setDefault() 
      .done()
    .init();
    client.setServer("apcan.cn", 1883);
    client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      Serial.println("mqtt now start to connecting.");
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }else {
      Serial.print(".");
    }
  } else {
    client.loop();
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      count++;
      if (digitalRead(doorstatepin)==LOW){
        client.publish(node_cardoor_state_topic,"open");
      }else{
        client.publish(node_cardoor_state_topic,"closed");
      }
      if (count==10){
        lastReconnectAttempt = 0;
      }
    }
  }
}

