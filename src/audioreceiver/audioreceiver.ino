#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <LittleFS.h>
#include <IRsend.h>
#include <iostream>
#include <string>
#include <sstream>

//HARDWARE -------------------
//BOARD
#define BZ1 14
//SOCKET 0
#define IRLED 13
//SOCKET 1
#define OPTOBAND    5  //S1-B
#define OPTOFMMODE  18 //S1-G
#define OPTOMEMORY  19 //S1-O
#define OPTOTUNINGD 21 //S1-Y
#define OPTOTUNINGU 22 //S1-W
#define OPTODTUNING 23 //S1-R
//SOCKET 2
#define OPTOSPKA    15 //S2-B     
#define OPTOSPKB    2  //S2-G
#define OPTOVCR1    0  //S2-O
#define OPTOCD      4  //S2-Y
#define OPTOTUNER   16 //S2-W
#define OPTOPHONO   17 //S2-R
//SOCKET 3
#define LEDB        26 //S3-B
#define LEDG        25 //S3-G
#define LEDR        33 //S3-O
#define OPTOPOWER   32 //S3-Y
#define POWERSENS   12 //S3-W
//CODE -----------------------
#define FORMAT_ON_FAIL
#define CLIENTID "Audio Receiver"
#define PRONTOLENGTH 104

enum Buzzer {set, on, off, action};
enum Opto {power, band, fmmode, memory, tuningd, tuningu, dtuning, spka, spkb, vcr1, cd, tuner, phono};
enum Color {standby, error, onAction, onBoot, onEspError};

const char* mqttClientID = CLIENTID;
const char* paramFile = "/param.json";
const char* urlMqttHome = "/";
const char* urlSettings = "/settings";
const char* urlSettingsSave = "/save";
static const char homePage[] PROGMEM = R"({"title": + CLIENTID +, "uri": "/", "menu": true, "element": [
      {"name": "header", "type": "ACElement", "value": "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Audio Receiver setup page</h2>"},
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
      {"name": "buzzer", "type": "ACCheckbox", "label": "Disable buzzer", "checked": "false", "global": true},
      {"name": "voldown", "type": "ACCheckbox", "label": "Volume down on shutdown", "checked": "false", "global": true},
      {"name": "volsteps", "type": "ACRange", "label": "Volume steps", "min": "1", "max": "10", "global": true},
      {"name": "save", "type": "ACSubmit", "value": "Save and connect", "uri": "/save"},
      {"name": "discard", "type": "ACSubmit", "value": "Discard", "uri": "/"}]
})";
static const char saveSettingsFilePage[] PROGMEM = R"({"title": "Save", "uri": "/save", "menu": true, "element": [
      { "name": "caption", "type": "ACText", "value": "<h2>Settings saved!</h2>",  "style": "text-align:center;color:#2f4f4f;padding:10px;" }]
})";

WiFiClient espClient;
PubSubClient client(espClient);
WebServer server;
IRsend irsend(IRLED);
AutoConnect portal(server);
AutoConnectConfig config;
AutoConnectAux homePageObj;
AutoConnectAux settingsPageObj;
AutoConnectAux saveSettingsFilePageObj;
fs::LittleFSFS& FlashFS = LittleFS;
uint16_t chipIDRaw = 0;
String chipID;
String strTopic;
String strPayload;
String mqttServer;
String mqttUser;
String mqttPass;
bool buzzer;
bool voldown;
uint8_t volsteps; 
bool powerState;
unsigned long previousMillis = 0;
const long interval = 500;

void beep(Buzzer buzzer){
  switch (buzzer){
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

void sendOpto(Opto opto){
    switch (buzzer){
    case power:      
      digitalWrite(OPTOPOWER, HIGH);
      delay(40);
      digitalWrite(OPTOPOWER, LOW);
    break;
    case band:
      digitalWrite(OPTOBAND, HIGH);
      delay(40);
      digitalWrite(OPTOBAND, LOW);
    break;
    case fmmode:
      digitalWrite(OPTOFMMODE, HIGH);
      delay(40);
      digitalWrite(OPTOFMMODE, LOW);
    break;
    case memory:
      digitalWrite(OPTOMEMORY, HIGH);
      delay(40);
      digitalWrite(OPTOMEMORY, LOW);
    break;
    case tuningd:
      digitalWrite(OPTOTUNINGD, HIGH);
      delay(40);
      digitalWrite(OPTOTUNINGD, LOW);
    break;
    case tuningu:
      digitalWrite(OPTOTUNINGU, HIGH);
      delay(40);
      digitalWrite(OPTOTUNINGU, LOW);
    break;
    case dtuning:
      digitalWrite(OPTODTUNING, HIGH);
      delay(40);
      digitalWrite(OPTODTUNING, LOW);
    break;
    case spka:
      digitalWrite(OPTOSPKA, HIGH);
      delay(40);
      digitalWrite(OPTOSPKA, LOW);
    break;
    case spkb:
      digitalWrite(OPTOSPKB, HIGH);
      delay(40);
      digitalWrite(OPTOSPKB, LOW);
    break;
    case vcr1:
      digitalWrite(OPTOVCR1, HIGH);
      delay(40);
      digitalWrite(OPTOVCR1, LOW);
    break;
    case cd:
      digitalWrite(OPTOCD, HIGH);
      delay(40);
      digitalWrite(OPTOCD, LOW);
    break;
    case tuner:
      digitalWrite(OPTOTUNER, HIGH);
      delay(40);
      digitalWrite(OPTOTUNER, LOW);
    break;
    case phono:
      digitalWrite(OPTOPHONO, HIGH);
      delay(40);
      digitalWrite(OPTOPHONO, LOW);
    break;
  }
}

void sendProntoStr(char* cmnd){
  std::string cmndStr(cmnd);
  uint16_t cmndArr[PRONTOLENGTH];
  uint16_t tempValue;
  uint16_t index = 0;
  cmndStr.erase(std::remove(cmndStr.begin(), cmndStr.end(), ' '), cmndStr.end());
  std::istringstream iss(cmndStr);
  while (iss >> std::hex >> tempValue) {
    cmndArr[index] = tempValue;
    index++;
  }
  irsend.sendPronto(cmndArr, PRONTOLENGTH);
}

void setLED(){

}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  strTopic = String((char*)topic);
  if (strTopic == "cmnd/audioreceiver/" + chipID + "/BTN") {
    strPayload = String((char*)payload);
    if(strPayload == "POWER") sendOpto(Opto::power);
    else if(strPayload == "BAND") sendOpto(Opto::band);
    else if(strPayload == "FMMODE") sendOpto(Opto::fmmode);
    else if(strPayload == "MEMORY") sendOpto(Opto::memory);
    else if(strPayload == "TUNINGD") sendOpto(Opto::tuningd);
    else if(strPayload == "TUNINGU") sendOpto(Opto::tuningu);
    else if(strPayload == "DTUNING") sendOpto(Opto::dtuning);
    else if(strPayload == "SPKA") sendOpto(Opto::spka);
    else if(strPayload == "SPKB") sendOpto(Opto::spkb);
    else if(strPayload == "VCR1") sendOpto(Opto::vcr1);
    else if(strPayload == "CD") sendOpto(Opto::cd);
    else if(strPayload == "TUNER") sendOpto(Opto::tuner);
    else if(strPayload == "PHONO") sendOpto(Opto::phono);
  }
  if (strTopic == "cmnd/audioreceiver/" + chipID + "/IR") {
    sendProntoStr((char*)payload);
  }
  if (strTopic == "cmnd/audioreceiver/" + chipID + "/LED") {

  }
}

String settingsToPage(AutoConnectAux& aux, PageArgument& args) {
  aux[F("mqttServer")].as<AutoConnectInput>().value = mqttServer;
  aux[F("mqttUser")].as<AutoConnectInput>().value = mqttUser;
  aux[F("mqttPass")].as<AutoConnectInput>().value = mqttPass;
  aux[F("buzzer")].as<AutoConnectInput>().value = buzzer;
  aux[F("voldown")].as<AutoConnectInput>().value = voldown;
  aux[F("volsteps")].as<AutoConnectInput>().value = volsteps;
  return String();
}

String loadSavedSettings(AutoConnectAux& aux) {
  mqttServer = aux[F("mqttServer")].as<AutoConnectInput>().value;
  mqttUser = aux[F("mqttUser")].as<AutoConnectInput>().value;
  mqttPass = aux[F("mqttPass")].as<AutoConnectInput>().value;
  AutoConnectCheckbox& buzzerCheckbox = aux[F("buzzer")].as<AutoConnectCheckbox>();
  buzzer = buzzerCheckbox.checked;
  AutoConnectCheckbox& voldownCheckbox = aux[F("voldown")].as<AutoConnectCheckbox>();
  voldown = voldownCheckbox.checked;
  AutoConnectRange& volstepsRange = aux[F("volsteps")].as<AutoConnectRange>();
  volsteps = volstepsRange.step;
  return String();
}

void loadSettingsFile(AutoConnectAux& aux) {
  File param = FlashFS.open(paramFile, "r");
  if (param && aux.loadElement(param)) loadSavedSettings(aux);
  param.close();
}

String saveSettingsFile(AutoConnectAux& aux, PageArgument& args) {
  AutoConnectAux& settings = aux.referer();
  loadSavedSettings(settings);
  File param = FlashFS.open(paramFile, "w");
  if (param) {
    settings.saveElement(param, {"mqttServer", "mqttUser", "mqttPass", "buzzer", "voldown", "volsteps"});
    param.close();
  }
  return String();
}


void mqttReconnect() {
  client.setCallback(mqttCallback);
  client.setServer(mqttServer.c_str(), 1883);
  if (client.connect(mqttClientID, mqttUser.c_str(), mqttPass.c_str())) {
    client.subscribe(("avail/audioreceiver/" + chipID).c_str());
    client.subscribe(("cmnd/audioreceiver/" + chipID + "/BTN").c_str());
    client.subscribe(("cmnd/audioreceiver/" + chipID + "/IR").c_str());
    client.subscribe(("cmnd/audioreceiver/" + chipID + "/LED").c_str());
    client.subscribe(("stat/audioreceiver/" + chipID + "/POWER").c_str());
    client.publish(("avail/audioreceiver/" + chipID).c_str(), "ONLINE");
  } else {
    delay(2000);
  }
}

void hardwareInit(){
  pinMode(BZ1, OUTPUT);
  digitalWrite(BZ1, LOW);
  pinMode(IRLED, OUTPUT);
  digitalWrite(IRLED, LOW);
  pinMode(OPTOBAND, OUTPUT);
  digitalWrite(OPTOBAND, LOW);
  pinMode(OPTOFMMODE, OUTPUT);
  digitalWrite(OPTOFMMODE, LOW);
  pinMode(OPTOMEMORY, OUTPUT);
  digitalWrite(OPTOMEMORY, LOW);
  pinMode(OPTOTUNINGD, OUTPUT);
  digitalWrite(OPTOTUNINGD, LOW);
  pinMode(OPTOTUNINGU, OUTPUT);
  digitalWrite(OPTOTUNINGU, LOW);
  pinMode(OPTODTUNING, OUTPUT);
  digitalWrite(OPTODTUNING, LOW);
  pinMode(OPTOSPKA, OUTPUT);
  digitalWrite(OPTOSPKA, LOW);
  pinMode(OPTOSPKB, OUTPUT);
  digitalWrite(OPTOSPKB, LOW);
  pinMode(OPTOVCR1, OUTPUT);
  digitalWrite(OPTOVCR1, LOW);
  pinMode(OPTOCD, OUTPUT);
  digitalWrite(OPTOCD, LOW);
  pinMode(OPTOTUNER, OUTPUT);
  digitalWrite(OPTOTUNER, LOW);
  pinMode(OPTOPHONO, OUTPUT);
  digitalWrite(OPTOPHONO, LOW);
  pinMode(LEDB, OUTPUT);
  digitalWrite(LEDB, LOW);
  pinMode(LEDG, OUTPUT);
  digitalWrite(LEDG, LOW);
  pinMode(LEDR, OUTPUT);
  digitalWrite(LEDR, LOW);
  pinMode(OPTOPOWER, OUTPUT);
  digitalWrite(OPTOPOWER, LOW);
  pinMode(POWERSENS, INPUT);
}

void softwareInit(){
  for (int i = 0; i < 17; i = i + 8) {
    chipIDRaw |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  chipID = String(chipIDRaw);
  FlashFS.begin(FORMAT_ON_FAIL);
  irsend.begin();
}

void autoConnectInit(){
  config.autoReconnect = true;
  config.reconnectInterval = 1;
  config.ota = AC_OTA_BUILTIN;
  config.apid = "Technics SA-GX170 - " + chipID;
  portal.config(config);
  homePageObj.load(homePage);
  settingsPageObj.load(settingsPage);
  saveSettingsFilePageObj.load(saveSettingsFilePage);
  portal.join({ homePageObj, settingsPageObj, saveSettingsFilePageObj });
  portal.on(urlSettings, settingsToPage);
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
}

void millisLoop() {

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
