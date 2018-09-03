#include <Arduino.h>
#include <string>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include "Bleeper.h"
#include <ESP8266WiFi.h>
#include <SimpleDHT.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <IRsend.h>
#include "../lib/ir/include/ir_decode.h"

//宏定义
//
#define MAX_PACKETSIZE 512 // UDP包大小
#define SERIAL_DEBUG Serial
#define TRY_COUNT 5         // 尝试次数
#define UDP_PORT 8000       // UDP端口
#define USER_DATA_SIZE 1024 // 红外码数组大小

// 全局变量
char buffUDP[MAX_PACKETSIZE]; // UDP缓存区
DynamicJsonBuffer jsonBuffer;
JsonObject &settings_json = jsonBuffer.createObject();

UINT16 user_data[USER_DATA_SIZE]; // 红外码数组

static t_remote_ac_status ac_status =
    {
        // 默认空调状态
        AC_POWER_ON,
        AC_TEMP_24,
        AC_MODE_COOL,
        AC_SWING_ON,
        AC_WS_AUTO,
        1,
        0,
        0};

#define DHT11 2
SimpleDHT11 dht11;

/**
说明：保存设置
返回值：保存成功返回true, 否则返回false
*/
boolean saveSettings();

/**
说明：提取设置
返回值：提取成功返回true, 否则返回false
*/
boolean getSettings();

/**
说明：下载文件
参数：文件id
返回值：下载成功返回true, 否则返回false
*/
boolean downLoadFile(int index_id);

/**
 * 
 * 数据转换
 */
int coverToEnum(String str, String type);

/**
 *发送红外 
 */
boolean sendIR();

/**
 * mqtt callback
 */
void callback(char *topic, byte *payload, unsigned int length);

/**
 * 下载固件
 */
boolean downLoadFile(int index_id);

class WifiConfig : public Configuration
{
public:
  persistentStringVar(ssid, "ssid");
  persistentStringVar(password, "password");
};

class MqttConfig : public Configuration
{
public:
  persistentStringVar(host, "xxx.cn");
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

char node_toast[30] = "esp/nomal/toast";

char node_heart_topic[30] = "";
int r7 = sprintf(node_heart_topic, "esp/%08X/heart", ESP.getChipId());
char node_state_topic[30] = "";
int r6 = sprintf(node_state_topic, "esp/%08X/state", ESP.getChipId());

char node_info_topic[30] = "";
int r4 = sprintf(node_info_topic, "esp/%08X/info", ESP.getChipId());

char node_th_topic[30] = "";
int r8 = sprintf(node_th_topic, "esp/%08X/TH", ESP.getChipId());

char node_study_topic[30] = "";
int r10 = sprintf(node_study_topic, "esp/%08X/study", ESP.getChipId());
char node_mode_set_topic[30] = "";
int r11 = sprintf(node_mode_set_topic, "esp/%08X/mode/set", ESP.getChipId());
char node_temperature_set_topic[30] = "";
int r12 = sprintf(node_temperature_set_topic, "esp/%08X/temperature/set", ESP.getChipId());
char node_swing_set_topic[30] = "";
int r13 = sprintf(node_swing_set_topic, "esp/%08X/swing/set", ESP.getChipId());
char node_fan_set_topic[30] = "";
int r14 = sprintf(node_fan_set_topic, "esp/%08X/fan/set", ESP.getChipId());

///////////////////////////

long lastReconnectAttempt = 0;
long lastScheduleTime500 = millis();
long lastScheduleTime1000 = millis();
long lastScheduleTime2000 = millis();
long lastScheduleTime5000 = millis();
int count = 0;
const char *user = (&C.mqtt.user)->c_str();
const char *pass = (&C.mqtt.pass)->c_str();
String mqttHost = "";

WiFiClient espClient;
PubSubClient client(espClient);

void setup()
{
  Serial.begin(115200);
  while (!Serial)
  {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  Serial.printf("Flash real id:");
  Serial.println(nodeid);
  SPIFFS.begin();
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

  uint16_t port = (uint16_t)C.mqtt.port;
  const char *host = (&C.mqtt.host)->c_str();
  mqttHost = String(host);
  Serial.println(host);
  Serial.println(port);
  Serial.println(user);
  Serial.println(pass);
  client.setServer(host, port);
  client.setCallback(callback);

  if (getSettings() == true)
  {
    SERIAL_DEBUG.println("get settings_json");
    settings_json.printTo(SERIAL_DEBUG);
  }
}

boolean reconnect()
{

  if (client.connect(nodeid, user, pass))
  {
    client.subscribe(node_heart_topic);
    client.publish(node_state_topic, "online");

    //空调主题
    client.subscribe(node_study_topic);
    client.subscribe(node_fan_set_topic);
    client.subscribe(node_swing_set_topic);
    client.subscribe(node_temperature_set_topic);
    client.subscribe(node_mode_set_topic);

    client.publish(node_toast, node_heart_topic);

    Serial.println(node_study_topic);
    Serial.println(node_fan_set_topic);
    Serial.println(node_swing_set_topic);
    Serial.println(node_temperature_set_topic);
    Serial.println(node_mode_set_topic);

  }
  delay(100);
  return client.connected();
}

byte lastTemperature = 0;
byte lastHumidity = 0;
long lastTempUpdateTime = millis();

void publishTemp()
{
  byte temperature = 0;
  byte humidity = 0;
  dht11.read(DHT11, &temperature, &humidity, NULL);
  long now = millis();
  if (now - lastTempUpdateTime < 30000)
  {
    if (temperature == lastTemperature && humidity == lastHumidity)
      return;
  }
  else
  {
    lastTempUpdateTime = now;
  }
  if (temperature == 0 || humidity == 0)
    return;
  lastTemperature = temperature;
  lastHumidity = humidity;
  Serial.print((int)temperature);
  Serial.print(" *C, ");
  Serial.print((int)humidity);
  Serial.println(" H");

  //mqtt publish
  StaticJsonBuffer<50> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  root["temperature"] = (int)temperature;
  root["thumidityime"] = (int)humidity;
  String output;
  root.printTo(output);
  const char *res = output.c_str();
  client.publish(node_th_topic, res);
}

void schedule500()
{
  long now = millis();

  if ((now - lastScheduleTime500) < 500)
    return;
  lastScheduleTime500 = now;
  //
  Serial.println("schedule500");
}

void schedule1000()
{
  long now = millis();

  if ((now - lastScheduleTime1000) < 500)
    return;
  lastScheduleTime1000 = now;
  //
  Serial.println("lastScheduleTime1000");
  publishTemp();
}

void schedule2000()
{
  long now = millis();
  if ((now - lastScheduleTime2000) < 2000)
    return;
  lastScheduleTime2000 = now;
  //
  Serial.println("schedule2000");
}

void schedule5000()
{
  long now = millis();
  if ((now - lastScheduleTime5000) < 5000)
    return;
  lastScheduleTime5000 = now;
  //
  Serial.println("schedule5000");
}

void connectMqtt()
{
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
    // schedule2000();
    // schedule1000();
    // schedule500();
    // schedule5000();
    client.loop();
  }
}

boolean downLoadFile(int index_id)
{

  HTTPClient http;
  String url = String("http://irext.net/irext/int/download_remote_index?admin_id=null&token=null&remote_index_id=" + String(index_id));
  boolean flag = false;
  SERIAL_DEBUG.println(index_id);
  // Prepare cache file
  String filename = String(index_id);
  if (SPIFFS.exists(filename))
  {
    SERIAL_DEBUG.println("already have file");
    return true;
  }
  File cache = SPIFFS.open(filename, "w");
  File *filestream = &cache;
  if (!cache)
  {
    SERIAL_DEBUG.println("Could not create cache file");
    return false;
  }
  else
  {
    int httpCode = 0;
    http.begin(url);
    httpCode = http.GET();

    SERIAL_DEBUG.print("[HTTP] GET return code: ");
    SERIAL_DEBUG.println(url);
    SERIAL_DEBUG.println(httpCode);

    if (httpCode == HTTP_CODE_OK)
    {
      http.writeToStream(filestream);
      SERIAL_DEBUG.printf("download %s ok\n", filename.c_str());
      flag = true;
       client.publish(node_toast, "download is ok");
    }
    else
    {
      SPIFFS.remove(filename);
    }
    cache.close();
    http.end();
    delay(10);
  }

  return flag;
}
bool netRestart = false;
const long lastUnConnectTime = millis();
const long min_5 = 1000 * 60 * 5;
void loop()
{

  Bleeper.handle();
  auto status = WiFi.status();
  bool isConnected = (status == WL_CONNECTED);
  if (isConnected)
  {
    connectMqtt();
    netRestart = true;
  }
  else
  {
    
    delay(500);
    if (netRestart)
    {
      ESP.restart();
      return;
    }

    if(millis() - lastUnConnectTime > min_5) {
      ESP.restart();
      return;
    }

    Serial.println("Wifi is not connect");
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  String _topic = topic;
  Serial.print("msg from topic: ");
  Serial.print(topic);
  Serial.print(" payload: ");

  String cmd = "";
  for (int i = 0; i < length; i++)
    cmd += (char)payload[i];

  String topic_str = String(topic);
  Serial.print(cmd);
  Serial.println();
  if (topic_str == node_study_topic)
  {
    // pin
    // index

    JsonObject &object = jsonBuffer.parse(cmd);
    if (object.containsKey("data_pin"))
    {
      settings_json["data_pin"] = object["data_pin"];
    }
    if (object.containsKey("use_file"))
    {
      settings_json["use_file"] = object["use_file"];
      downLoadFile(object["use_file"]);
    }
    saveSettings();

    return;
  }

  if (topic_str == node_heart_topic)
  {
    client.publish(node_state_topic, "online");
    return;
  }

  if (topic_str == node_info_topic)
  {
    StaticJsonBuffer<1024> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();
    root["data_pin"] = settings_json["data_pin"];
    root["use_file"] = settings_json["use_file"];

    root["wifi_ssid"] = &C.wifi.ssid;
    root["wifi_user"] = &C.wifi.password;

    root["mtqq_ip"] = mqttHost;
    root["mtqq_user"] = user;
    root["mtqq_pass"] = pass;

    String output;
    root.printTo(output);
    const char *res = output.c_str();
    client.publish(node_info_topic, res);
    return;
  }

  if (topic_str == node_mode_set_topic && (cmd == "False"))
  {
    ac_status.ac_power = AC_POWER_OFF;
  }
  else
  {
    ac_status.ac_power = AC_POWER_ON;
  }
  if (topic_str == node_temperature_set_topic)
  {
    int tmp = cmd.toInt();
    t_ac_temperature temperature = t_ac_temperature(tmp - 16);
    ac_status.ac_temp = temperature;
  }
  if (topic_str == node_mode_set_topic && (cmd != "False"))
  {
    ac_status.ac_mode = (t_ac_mode)(coverToEnum(cmd, "mode"));
  }
  if (topic_str == node_swing_set_topic)
  {
    ac_status.ac_wind_dir = (t_ac_swing)(coverToEnum(cmd, "swind"));
  }
  if (topic_str == node_fan_set_topic)
  {
    ac_status.ac_wind_speed = (t_ac_wind_speed)(coverToEnum(cmd, "swind_speed"));
  }

  sendIR();
  SERIAL_DEBUG.println("***   ac_status   ***");
  SERIAL_DEBUG.printf("power = %d\n", ac_status.ac_power);
  SERIAL_DEBUG.printf("temperature = %d\n", ac_status.ac_temp);
  SERIAL_DEBUG.printf("swing = %d\n", ac_status.ac_wind_dir);
  SERIAL_DEBUG.printf("speed = %d\n", ac_status.ac_wind_speed);
  SERIAL_DEBUG.printf("mode = %d\n", ac_status.ac_mode);
  SERIAL_DEBUG.println("**********************");
}

boolean saveSettings()
{
  File cache = SPIFFS.open("settings", "w");
  Stream *file_stream = &cache;
  if (cache)
  {
    String tmp;
    settings_json.printTo(tmp);
    cache.println(tmp);
  }
  else
  {
    SERIAL_DEBUG.println("can't open settings");
    return false;
  }
  cache.close();
  return true;
}

boolean getSettings()
{
  File cache = SPIFFS.open("settings", "r");
  if (cache)
  {
    String tmp = cache.readString();
    JsonObject &json_object = jsonBuffer.parseObject(tmp);
    JsonObject::iterator it;
    for (it = json_object.begin(); it != json_object.end(); ++it)
    {
      settings_json[it->key] = json_object[it->key];
    }
  }
  else
  {
    SERIAL_DEBUG.println("settings is not exits");
    return false;
  }
  cache.close();
  return true;
}

boolean sendIR()
{

  String filename = settings_json["use_file"];
  String tmp = settings_json["data_pin"];
  int data_pin = tmp.toInt();
  IRsend irsend = IRsend(data_pin);
  irsend.begin();
  if (SPIFFS.exists(filename))
  {
    File f = SPIFFS.open(filename, "r");
    File *fp = &f;
    if (f)
    {
      UINT16 content_length = f.size();
      if (content_length == 0)
      {
        return false;
      }
      SERIAL_DEBUG.printf("content_length = %d\n", content_length);
      UINT8 *content = (UINT8 *)malloc(content_length * sizeof(UINT8));
      f.seek(0L, fs::SeekSet);
      f.readBytes((char *)content, content_length);
      INT8 ret = ir_binary_open(IR_CATEGORY_AC, 1, content, content_length);
      int length = ir_decode(2, user_data, &ac_status, 0);
      SERIAL_DEBUG.println();
      for (int i = 0; i < length; i++)
      {
        Serial.printf("%d ", user_data[i]);
      }
      SERIAL_DEBUG.println();
      irsend.sendRaw(user_data, length, 38);
      ir_close();
      return true;
    }
    else
    {
      SERIAL_DEBUG.printf("open %s was failed\n", filename.c_str());
      return false;
    }
    f.close();
  }
  else
  {
    SERIAL_DEBUG.printf("%s is not exsits\n", filename.c_str());
    return false;
  }
}

int coverToEnum(String str, String type)
{
  String swing_str[] = {"True", "False"};
  String swing_speed_str[] = {"auto", "low", "medium", "high"};
  String mode_str[] = {"cool", "heat", "auto", "fan", "dry"};
  if (type == "mode")
    for (int i = 0; i < sizeof(mode_str) / sizeof(mode_str[0]); i++)
    {
      if (str == mode_str[i])
        return i;
    }

  if (type == "swind")
    for (int i = 0; i < sizeof(swing_str) / sizeof(swing_str[0]); i++)
    {
      if (str == swing_str[i])
        return i;
    }
  if (type == "swind_speed")
    for (int i = 0; i < sizeof(swing_speed_str) / sizeof(swing_speed_str[0]); i++)
    {
      if (str == swing_speed_str[i])
        return i;
    }
}