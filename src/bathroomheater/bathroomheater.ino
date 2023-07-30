#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <LittleFS.h>
#include <IRsend.h>

//HARDWARE ----------------------
//BOARD
#define BZ1       15
//SOCKET 0
#define RL1        0
#define OPTS1     16
#define OPTS2      5
#define OPTS3      4
//SOCKET 1
#define IR1       13
//CODE --------------------------
#define FORMAT_ON_FAIL
#define CLIENTID "Bathroom heater"

enum Buzzer {set, on, off, action};
const char* mqttClientID = CLIENTID;
const char* settingsFile = "/settings.json";
const char* urlMqttHome = "/";
const char* urlSettings = "/settings";
const char* urlSettingsSave = "/save";
static const char homePage[] PROGMEM = R"({"title": "Bathroom heater", "uri": "/", "menu": true, "element": [
      {"name": "header", "type": "ACElement", "value": "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Bathroom heater setup page</h2>"},
      { "name": "content", "type": "ACText", "value": "Powered by <a href=https://github.com/Hieromon/AutoConnect>AutoConnect</a>"},
      { "name": "content2", "type": "ACText", "value": "<br>Part of the project <a href=https://github.com/agustin-perez/iotdevices>IoTDevices</a><br>Agustín Pérez"}]
})";
static const char settingsPage[] PROGMEM = R"({"title": "Settings", "uri": "/settings", "menu": true, "element": [
      {"name": "style", "type": "ACStyle", "value": "label+input,label+select{position:sticky;left:140px;width:204px!important;box-sizing:border-box;}"},
      {"name": "header", "type": "ACElement", "value": "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Bathroom heater settings</h2>"},
      {"name": "caption", "type": "ACText", "value": "Setup Home Assistant's MQTT Broker connection", "posterior": "par"},
      {"name": "mqttServer", "type": "ACInput", "label": "Server", "pattern": "^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9])$", "placeholder": "MQTT server address", "global": true},
      {"name": "mqttUser", "type": "ACInput", "label": "User", "global": true},
      {"name": "mqttPass", "type": "ACInput", "label": "Password", "apply": "password", "global": true},
      {"name": "buzzer", "type": "ACCheckbox", "label": "Enable buzzer", "checked": "false", "global": true},
      {"name": "alwaysOn", "type": "ACCheckbox", "label": "Always on on restart", "checked": "false", "global": true},
      {"name": "save", "type": "ACSubmit", "value": "Save", "uri": "/save"},
      {"name": "discard", "type": "ACSubmit", "value": "Cancel", "uri": "/"}]
})";
static const char saveSettingsFilePage[] PROGMEM = R"({"title": "Save", "uri": "/save", "menu": true, "element": [
      { "name": "caption", "type": "ACText", "value": "<h2>Settings saved!</h2>",  "style": "text-align:center;color:#2f4f4f;padding:10px;" }]
})";

WiFiClient espClient;
PubSubClient client(espClient);
WebServer server;
AutoConnect portal(server);
AutoConnectConfig config;
AutoConnectAux homePageObj;
AutoConnectAux settingsPageObj;
AutoConnectAux saveSettingsFilePageObj;
FS& FlashFS = LittleFS;
uint16_t chipIDRaw = 0;
String chipID;
String strTopic;
String strPayload;
String mqttServer;
String mqttUser;
String mqttPass;
bool buzzer;
bool alwaysOn;
bool power;
bool heat1;
bool heat2;
unsigned long optMillis = 0;;
const unsigned long optMillisInterval = 50;

void beep(Buzzer buzzerStatus) {
  if (buzzer) {
    switch (buzzerStatus) {
      case set: tone(BZ1, 3500, 1200);
        break;
      case on: tone(BZ1, 3000, 300);
        break;
      case off: tone(BZ1, 1500, 300);
        break;
      case action: tone(BZ1, 2000, 50);
        break;
    }
  }
}

void setRelayPower(bool power) {
  if (power) {
    power = true;
    digitalWrite(RL1, HIGH);
    client.publish(("stat/bathroomheater/" + chipID + "/POWER").c_str(), "ON");
    delay(15);
    
    beep(Buzzer::on);
  } else {
    power = false;
    digitalWrite(RL1, LOW);
    client.publish(("stat/bathroomheater/" + chipID + "/POWER").c_str(), "OFF");
    beep(Buzzer::off);
  }
}

//POWER:    FF19E6
//TIMER:    FF29D6
//HEAT I:   FF09F6
//HEAT II:  FF31CE

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  strTopic = String((char*)topic);
  if (strTopic == "cmnd/homeassistant/watchdog") {
    client.publish(("avail/bathroomheater/" + chipID).c_str(), "ONLINE");
  }

  if (strTopic == "cmnd/bathroomheater/" + chipID + "/POWER") {
    String res = String((char*)payload);
    if (res == "ON") setRelayPower(true);
    else if (res == "OFF") setRelayPower(false);
  }

  if (strTopic == "cmnd/bathroomheater/" + chipID + "/HEAT") {
    String res = String((char*)payload);
    if (res == "ON") setRelayPower(true);
    else if (res == "OFF") setRelayPower(false);
  }
}

String loadSettingsInPage(AutoConnectAux& aux, PageArgument& args) {
  aux[F("header")].as<AutoConnectInput>().value = "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Bathroomheater settings - ID:" + chipID + "</h2>";
  aux[F("mqttServer")].as<AutoConnectInput>().value = mqttServer;
  aux[F("mqttUser")].as<AutoConnectInput>().value = mqttUser;
  aux[F("mqttPass")].as<AutoConnectInput>().value = mqttPass;
  aux[F("buzzer")].as<AutoConnectInput>().value = buzzer;
  aux[F("alwaysOn")].as<AutoConnectInput>().value = alwaysOn;
  return String();
}

String loadSavedSettings(AutoConnectAux& aux) {
  mqttServer = aux[F("mqttServer")].as<AutoConnectInput>().value;
  mqttUser = aux[F("mqttUser")].as<AutoConnectInput>().value;
  mqttPass = aux[F("mqttPass")].as<AutoConnectInput>().value;
  AutoConnectCheckbox& buzzerCheckbox = aux[F("buzzer")].as<AutoConnectCheckbox>();
  buzzer = buzzerCheckbox.checked;
  AutoConnectCheckbox& alwaysOnCheckbox = aux[F("alwaysOn")].as<AutoConnectCheckbox>();
  alwaysOn = alwaysOnCheckbox.checked;
  return String();
}

void loadSettingsFile(AutoConnectAux& aux) {
  File file = FlashFS.open(settingsFile, "r");
  if (file && aux.loadElement(file)) loadSavedSettings(aux);
  file.close();
}

String saveSettingsFile(AutoConnectAux& aux, PageArgument& args) {
  AutoConnectAux& settings = aux.referer();
  loadSavedSettings(settings);
  File file = FlashFS.open(settingsFile, "w");
  if (file) {
    settings.saveElement(file, {"mqttServer", "mqttUser", "mqttPass", "buzzer", "alwaysOn"});
    file.close();
  }
  return String();
}


void mqttReconnect() {
  client.setCallback(mqttCallback);
  client.setServer(mqttServer.c_str(), 1883);
  if (client.connect(mqttClientID, mqttUser.c_str(), mqttPass.c_str())) {
    client.subscribe(("avail/bathroomheater/" + chipID).c_str());
    client.subscribe(("cmnd/bathroomheater/" + chipID + "/POWER").c_str());
    client.subscribe(("stat/bathroomheater/" + chipID + "/POWER").c_str());
    client.subscribe(("stat/bathroomheater/" + chipID + "/HEAT").c_str());
    client.subscribe("cmnd/homeassistant/watchdog");
    client.publish(("avail/bathroomheater/" + chipID).c_str(), "ONLINE");
  } else {
    delay(2000);
  }
}


#define BZ1       15
//SOCKET 0
#define RL1        0
#define OPTS1     16
#define OPTS2      5
#define OPTS3      4
//SOCKET 1
#define IR1       13
void hardwareInit() {
  pinMode(BZ1, OUTPUT);
  digitalWrite(BZ1, LOW);
  pinMode(RL1, OUTPUT);
  digitalWrite(RL1, LOW);
  pinMode(IR1, OUTPUT);
  digitalWrite(IR1, LOW)
  pinMode(OPTS1, INPUT);
  pinMode(OPTS2, INPUT);
  pinMode(OPTS3, INPUT);
}

void softwareInit() {
  chipID = String(ESP.getChipId(), HEX);
  FlashFS.begin(FORMAT_ON_FAIL);
}

void autoConnectInit() {
  config.autoReconnect = true;
  config.reconnectInterval = 1;
  config.apid = String(CLIENTID) + " - " + chipID;
  config.title = "Bathroomheater - ID:" + chipID;
  config.hostName = "Bathroomheater-" + chipID;
  config.ota = AC_OTA_BUILTIN;
  portal.config(config);
  homePageObj.load(homePage);
  settingsPageObj.load(settingsPage);
  saveSettingsFilePageObj.load(saveSettingsFilePage);
  portal.join({ homePageObj, settingsPageObj, saveSettingsFilePageObj });
  portal.on(urlSettings, loadSettingsInPage);
  portal.on(urlSettingsSave, saveSettingsFile);
  AutoConnectAux& settingsRestore = *portal.aux(urlSettings);
  loadSettingsFile(settingsRestore);
  portal.begin();
}

void setup() {
  hardwareInit();
  softwareInit();
  autoConnectInit();
  beep(Buzzer::set);
  if (alwaysOn) setRelayPower(true);
}

void millisLoop() {
  bool currentPower = digitalRead(OPTS1);
  if (currentPower != power) {
    power = currentPower;
    if (currentThermostat) client.publish(("stat/bathroomheater/" + chipID + "/POWER").c_str(), "1");
  }
  
  bool currentHeat1 = digitalRead(OPTS2);
  if (currentHeat1 != heat1) {
    heat1 = currentHeat1;
    if (currentThermostat) client.publish(("stat/bathroomheater/" + chipID + "/HEAT").c_str(), "1");
    bool currentHeat2 = digitalRead(OPTS3);
    if (currentHeat2 != heat2) {
      heat2 = currentHeat2;
      if (currentThermostat) client.publish(("stat/bathroomheater/" + chipID + "/HEAT").c_str(), "2");
    }
  } else {
    client.publish(("stat/bathroomheater/" + chipID + "/HEAT").c_str(), "0")
  }
  
  unsigned long currentMillis = millis();
  if (currentMillis - optMillis >= optMillisInterval) {
    optMillis = currentMillis;
  }
}

void loop() {
  portal.handleClient();
  if (!client.connected()) mqttReconnect();
  client.loop();
  millisLoop();
}
