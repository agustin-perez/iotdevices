#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <LittleFS.h>

//DEFINITIONS
#define RL1 16    //D0
#define RL2 4     //D2
#define FORMAT_ON_FAIL
#define STAMqttClientID "Outdoor spotlights"

const char* mqttClientID = STAMqttClientID;
const char* paramFile = "/param.json";
const char* urlMqttHome = "/";
const char* urlSettings = "/settings";
const char* urlSettingsSave = "/save";
static const char homePage[] PROGMEM = R"({"title": "Outdoor spotlights", "uri": "/", "menu": true, "element": [
      {"name": "header", "type": "ACElement", "value": "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Outdoor spotlights setup page</h2>"},
      { "name": "content", "type": "ACText", "value": "Powered by <a href=https://github.com/Hieromon/AutoConnect>AutoConnect</a>"},
      { "name": "content2", "type": "ACText", "value": "<br>Part of the project <a href=https://github.com/agustin-perez/iotdevices>IoTDevices</a><br>Agustín Pérez"}]
})";
static const char settingsPage[] PROGMEM = R"({"title": "Settings", "uri": "/settings", "menu": true, "element": [
      {"name": "style", "type": "ACStyle", "value": "label+input,label+select{position:sticky;left:140px;width:204px!important;box-sizing:border-box;}"},
      {"name": "header", "type": "ACElement", "value": "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Outdoor spotlights settings</h2>"},
      {"name": "caption", "type": "ACText", "value": "Setup Home Assistant's MQTT Broker connection", "posterior": "par"},
      {"name": "mqttServer", "type": "ACInput", "label": "Server", "pattern": "^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9])$", "placeholder": "MQTT server address", "global": true},
      {"name": "mqttUser", "type": "ACInput", "label": "User", "global": true},
      {"name": "mqttPass", "type": "ACInput", "label": "Password", "apply": "password", "global": true},
      {"name": "caption2", "type": "ACText", "value": "Light powered on at boot<br>Useful for always-on lights", "posterior": "par"},
      {"name": "sw1state", "type": "ACCheckbox", "label": "Light 1", "checked": "false", "global": true},
      {"name": "sw2state", "type": "ACCheckbox", "label": "Light 2", "checked": "false", "global": true},
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
bool sw1state = false;
bool sw2state = false;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  strTopic = String((char*)topic);

  if (strTopic == "cmnd/outdoorspotlights/" + chipID + "/sw1") {
    if (payload[0] == '1') {
      digitalWrite(RL1, HIGH);
      client.publish(("stat/outdoorspotlights/" + chipID + "/sw1").c_str(), "on");
    }
    if (payload[0] == '0') {
      digitalWrite(RL1, LOW);
      client.publish(("stat/outdoorspotlights/" + chipID + "/sw1").c_str(), "off");
    }
  }

  if (strTopic == "cmnd/outdoorspotlights/" + chipID + "/sw2") {
    if (payload[0] == '1') {
      digitalWrite(RL2, HIGH);
      client.publish(("stat/outdoorspotlights/" + chipID + "/sw2").c_str(), "on");
    }
    if (payload[0] == '0') {
      digitalWrite(RL2, LOW);
      client.publish(("stat/outdoorspotlights/" + chipID + "/sw2").c_str(), "off");
    }
  }
}

String submitSettings(AutoConnectAux& aux, PageArgument& args) {
  aux[F("mqttServer")].as<AutoConnectInput>().value = mqttServer;
  aux[F("mqttUser")].as<AutoConnectInput>().value = mqttUser;
  aux[F("mqttPass")].as<AutoConnectInput>().value = mqttPass;
  aux[F("sw1state")].as<AutoConnectInput>().value = sw1state;
  aux[F("sw2state")].as<AutoConnectInput>().value = sw2state;
  return String();
}

String updateSettings(AutoConnectAux& aux) {
  mqttServer = aux[F("mqttServer")].as<AutoConnectInput>().value;
  mqttUser = aux[F("mqttUser")].as<AutoConnectInput>().value;
  mqttPass = aux[F("mqttPass")].as<AutoConnectInput>().value;
  AutoConnectCheckbox& sw1stateCheckbox = aux[F("sw1state")].as<AutoConnectCheckbox>();
  sw1state = sw1stateCheckbox.checked;
  AutoConnectCheckbox& sw2stateCheckbox = aux[F("sw2state")].as<AutoConnectCheckbox>();
  sw2state = sw2stateCheckbox.checked;
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
    settings.saveElement(param, {"mqttServer", "mqttUser", "mqttPass", "sw1state", "sw2state"});
    param.close();
  }
  return String();
}

void mqttReconnect() {
  client.setCallback(mqttCallback);
  client.setServer(mqttServer.c_str(), 1883);
  if (client.connect(mqttClientID, mqttUser.c_str(), mqttPass.c_str())) {
    client.subscribe(("avail/outdoorspotlights/" + chipID).c_str());
    client.subscribe(("cmnd/outdoorspotlights/" + chipID + "/sw1").c_str());
    client.subscribe(("cmnd/outdoorspotlights/" + chipID + "/sw2").c_str());
    client.subscribe(("stat/outdoorspotlights/" + chipID + "/sw1").c_str());
    client.subscribe(("stat/outdoorspotlights/" + chipID + "/sw2").c_str());
    client.publish(("avail/outdoorspotlights/" + chipID).c_str(), "online");
  } else {
    delay(2000);
  }
}

void setup() {
  pinMode(RL1, OUTPUT);  
  pinMode(RL2, OUTPUT);
  digitalWrite(RL1, LOW);
  digitalWrite(RL2, LOW);
  delay(1000);
  chipID = String(ESP.getChipId(), HEX);
  FlashFS.begin(FORMAT_ON_FAIL);
  config.autoReconnect = true;
  config.reconnectInterval = 1;
  config.ota = AC_OTA_BUILTIN;
  config.apid = "Outdoor spotlights - " + chipID;
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
  if (sw1state) digitalWrite(RL1, HIGH);
}

void loop() {
  portal.handleClient();
  if (!client.connected()) mqttReconnect();
  client.loop();
}
