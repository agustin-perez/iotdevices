5#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <LittleFS.h>

//DEFINITIONS
#define RL1 16    //D0
#define RL2 4     //D2
#define TS1 15    //D8
#define TS2 13    //D7
#define BZ1 5     //D1
#define BZ1SET 1500
#define BZ1SETLENGTH 1200
#define BZ1TSON 1800
#define BZ1TSONLENGTH 300
#define BZ1TSOFF 1500
#define BZ1TSOFFLENGTH 300
#define FORMAT_ON_FAIL
#define STAMqttClientID "Wall Socket"

const char* mqttClientID = STAMqttClientID;
const char* paramFile = "/param.json";T
const char* urlMqttHome = "/";
const char* urlSettings = "/settings";
const char* urlSettingsSave = "/save";
static const char homePage[] PROGMEM = R"({"title": "Wall Socket", "uri": "/", "menu": true, "element": [
      {"name": "header", "type": "ACElement", "value": "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Wall Socket setup page</h2>"},
      { "name": "content", "type": "ACText", "value": "Powered by <a href=https://github.com/Hieromon/AutoConnect>AutoConnect</a>"},
      { "name": "content2", "type": "ACText", "value": "<br>Part of the project <a href=https://github.com/agustin-perez/iotdevices>IoTDevices</a><br>Agustín Pérez"}]
})";
static const char settingsPage[] PROGMEM = R"({"title": "Settings", "uri": "/settings", "menu": true, "element": [
      {"name": "style", "type": "ACStyle", "value": "label+input,label+select{position:sticky;left:140px;width:204px!important;box-sizing:border-box;}"},
      {"name": "header", "type": "ACElement", "value": "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Wall Socket settings</h2>"},
      {"name": "caption", "type": "ACText", "value": "Setup Home Assistant's MQTT Broker connection", "posterior": "par"},
      {"name": "mqttServer", "type": "ACInput", "label": "Server", "pattern": "^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9])$", "placeholder": "MQTT server address", "global": true},
      {"name": "mqttUser", "type": "ACInput", "label": "User", "global": true},
      {"name": "mqttPass", "type": "ACInput", "label": "Password", "apply": "password", "global": true},
      {"name": "caption2", "type": "ACText", "value": "Relay powered on at boot<br>Useful for always-on appliances", "posterior": "par"},
      {"name": "rl1state", "type": "ACCheckbox", "label": "Relay 1", "checked": "false", "global": true},
      {"name": "rl2state", "type": "ACCheckbox", "label": "Relay 2", "checked": "false", "global": true},
      {"name": "save", "type": "ACSubmit", "value": "Save and connect", "uri": "/save"},
      {"name": "discard", "type": "ACSubmit", "value": "Discard", "uri": "/"}]
})";
static const char saveSettingsPage[] PROGMEM = R"({"title": "Save", "uri": "/save", "menu": true, "element": [
      { "name": "caption", "type": "ACText", "value": "<h2>Settings saved!</h2>",  "style": "text-align:center;color:#2f4f4f;padding:10px;" }]
})";

WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server;
AutoConnect portal(server);
AutoConnectConfig config;
AutoConnectAux homePageObj;
AutoConnectAux settingsPageObj;
AutoConnectAux saveSettingsPageObj;
FS& FlashFS = LittleFS;
String strTopic;
String strPayload;
String mqttServer;
String mqttUser;
String mqttPass;
String chipID;
bool prevTS1State;
bool prevTS2State;
bool ts1State;
bool ts2State;
bool rl1state = false;
bool rl2state = false;
unsigned long previousMillis = 0;
const long interval = 500;

void rl1Toggle() {
  digitalWrite(RL1, !digitalRead(RL1));
  if (digitalRead(RL1) == HIGH) {
    client.publish(("stat/wallsocket/" + chipID + "/RL1").c_str(), "ON");
    tone(BZ1, BZ1TSON, BZ1TSONLENGTH);
  } else {
    client.publish(("stat/wallsocket/" + chipID + "/RL1").c_str(), "OFF");
    tone(BZ1, BZ1TSOFF, BZ1TSOFFLENGTH);
  }
}

void rl2Toggle() {
  digitalWrite(RL2, !digitalRead(RL2));
  if (digitalRead(RL2) == HIGH) {
    client.publish(("stat/wallsocket/" + chipID + "/RL2").c_str(), "ON");
    tone(BZ1, BZ1TSON, BZ1TSONLENGTH);
  } else {
    client.publish(("stat/wallsocket/" + chipID + "/RL2").c_str(), "OFF");
    tone(BZ1, BZ1TSOFF, BZ1TSOFFLENGTH);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  strTopic = String((char*)topic);

  if (strTopic == "cmnd/wallsocket/" + chipID + "/RL1") {
    if (payload[0] == '1') {
      digitalWrite(RL1, HIGH);
      client.publish(("stat/wallsocket/" + chipID + "/RL1").c_str(), "ON");
      tone(BZ1, BZ1TSON, BZ1TSONLENGTH);
    }
    if (payload[0] == '0') {
      digitalWrite(RL1, LOW);
      client.publish(("stat/wallsocket/" + chipID + "/RL1").c_str(), "OFF");
      tone(BZ1, BZ1TSOFF, BZ1TSOFFLENGTH);
    }
  }

  if (strTopic == "cmnd/wallsocket/" + chipID + "/RL2") {
    if (payload[0] == '1') {
      digitalWrite(RL2, HIGH);
      client.publish(("stat/wallsocket/" + chipID + "/RL2").c_str(), "ON");
      tone(BZ1, BZ1TSON, BZ1TSONLENGTH);
    }
    if (payload[0] == '0') {
      digitalWrite(RL2, LOW);
      client.publish(("stat/wallsocket/" + chipID + "/RL2").c_str(), "OFF");
      tone(BZ1, BZ1TSOFF, BZ1TSOFFLENGTH);
    }
  }
}

String submitSettings(AutoConnectAux& aux, PageArgument& args) {
  aux[F("mqttServer")].as<AutoConnectInput>().value = mqttServer;
  aux[F("mqttUser")].as<AutoConnectInput>().value = mqttUser;
  aux[F("mqttPass")].as<AutoConnectInput>().value = mqttPass;
  aux[F("rl1state")].as<AutoConnectInput>().value = rl1state;
  aux[F("rl2state")].as<AutoConnectInput>().value = rl2state;
  return String();
}

String updateSettings(AutoConnectAux& aux) {
  mqttServer = aux[F("mqttServer")].as<AutoConnectInput>().value;
  mqttUser = aux[F("mqttUser")].as<AutoConnectInput>().value;
  mqttPass = aux[F("mqttPass")].as<AutoConnectInput>().value;
  AutoConnectCheckbox& rl1stateCheckbox = aux[F("rl1state")].as<AutoConnectCheckbox>();
  rl1state = rl1stateCheckbox.checked;
  AutoConnectCheckbox& rl2stateCheckbox = aux[F("rl2state")].as<AutoConnectCheckbox>();
  rl1state = rl2stateCheckbox.checked;
  return String();
}

void loadParams(AutoConnectAux& aux) {
  File param = FlashFS.open(paramFile, "r");
  if (param && aux.loadElement(param)) updateSettings(aux);
  param.close();
}

String saveSettings(AutoConnectAux& aux, PageArgument& args) {
  AutoConnectAux& settings = aux.referer();
  updateSettings(settings);
  File param = FlashFS.open(paramFile, "w");
  if (param) {
    settings.saveElement(param, {"mqttServer", "mqttUser", "mqttPass", "rl1state", "rl2state"});
    param.close();
  }
  return String();
}


void mqttReconnect() {
  client.setCallback(mqttCallback);
  client.setServer(mqttServer.c_str(), 1883);
  if (client.connect(mqttClientID, mqttUser.c_str(), mqttPass.c_str())) {
    client.subscribe(("avail/wallsocket/" + chipID).c_str());
    client.subscribe(("cmnd/wallsocket/" + chipID + "/RL1").c_str());
    client.subscribe(("cmnd/wallsocket/" + chipID + "/RL2").c_str());
    client.subscribe(("stat/wallsocket/" + chipID + "/RL1").c_str());
    client.subscribe(("stat/wallsocket/" + chipID + "/RL2").c_str());
    client.publish(("avail/wallsocket/" + chipID).c_str(), "Online");
  } else {
    delay(2000);
  }
}

void setup() {
  pinMode(RL1, OUTPUT);
  digitalWrite(RL1, LOW);
  pinMode(RL2, OUTPUT);
  digitalWrite(RL2, LOW);
  pinMode(TS1, INPUT);
  pinMode(TS2, INPUT);
  pinMode(BZ1, OUTPUT);
  tone(BZ1, BZ1SET, BZ1SETLENGTH);
  delay(1000);
  chipID = String(ESP.getChipId(), HEX);
  FlashFS.begin(FORMAT_ON_FAIL);
  config.autoReconnect = true;
  config.reconnectInterval = 1;
  config.ota = AC_OTA_BUILTIN;
  config.apid = "Wall Socket - " + chipID;
  portal.config(config);
  homePageObj.load(homePage);
  settingsPageObj.load(settingsPage);
  saveSettingsPageObj.load(saveSettingsPage);
  portal.join({ homePageObj, settingsPageObj, saveSettingsPageObj });
  portal.on(urlSettings, submitSettings);
  portal.on(urlSettingsSave, saveSettings);
  AutoConnectAux& settingsRestore = *portal.aux(urlSettings);
  loadParams(settingsRestore);
  portal.begin();
  if (rl1state) digitalWrite(RL1, HIGH);
  if (rl2state) digitalWrite(RL2, HIGH);
  bool prevTS1State = digitalRead(TS1);
  bool prevTS2State = digitalRead(TS2);
}

void millisLoop() {
  prevTS1State = ts1State;
  prevTS2State = ts2State;
  ts1State = digitalRead(TS1);
  ts2State = digitalRead(TS2);
  if (!prevTS1State && ts1State) rl1Toggle();
  if (!prevTS2State && ts2State) rl2Toggle();
}

void loop() {
  portal.handleClient();
  if (!client.connected()) mqttReconnect();
  client.loop();
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    millisLoop();
  }
}
