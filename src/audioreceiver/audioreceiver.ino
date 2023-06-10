/**
 * Es necesario hacer override en MQTT_MAX_PACKET_SIZE 1024, en el .h de PubSubClient
 */ 
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <FS.h> 
#include <IRsend.h>
#include <iostream>
#include <string>
#include <sstream>

//HARDWARE ----------------------
//BOARD
#define BZ1 14
//SOCKET 0
#define IRLED 13
//SOCKET 1
#define OPTOBAND        5  //S1-B
#define OPTOFMMODE      18 //S1-G
#define OPTOMEMORY      19 //S1-O
#define OPTOTUNINGD     21 //S1-Y
#define OPTOTUNINGU     22 //S1-W
#define OPTODTUNING     23 //S1-R
//SOCKET 2
#define OPTOSPKA        15 //S2-B     
#define OPTOSPKB        2  //S2-G
#define OPTOVCR1        0  //S2-O
#define OPTOCD          4  //S2-Y
#define OPTOTUNER       16 //S2-W
#define OPTOPHONO       17 //S2-R
//SOCKET 3
#define LEDB            26 //S3-B
#define LEDG            12 //S3-G
#define LEDR            33 //S3-O
#define OPTOPOWER       32 //S3-Y
#define POWERSENS       35 //S3-W
//CHANNELS ----------------------
#define CHBZ1           0
#define CHR             3
#define CHG             2
#define CHB             1
//CODE --------------------------
#define FORMAT_ON_FAIL
#define CLIENTID        "Audio Receiver"
#define PRONTOLENGTH    104
#undef  MQTT_MAX_PACKET_SIZE 
#define MQTT_MAX_PACKET_SIZE 1024

enum Buzzer {set, on, off, action};
enum Opto {power, band, fmmode, memory, tuningd, tuningu, dtuning, spka, spkb, vcr1, cd, tuner, phono};
enum Status {standby, idle, error, onAction, onBoot, onEspError};
//enum LEDMode {static, fadein, fadeout};
const char* mqttClientID = CLIENTID;
const char* settingsFile = "/settings.json";
const char* urlMqttHome = "/";
const char* urlSettings = "/settings";
const char* urlSettingsSave = "/save";
static const char homePage[] PROGMEM = R"({"title": "Audio Receiver", "uri": "/", "menu": true, "element": [
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
      {"name": "buzzer", "type": "ACCheckbox", "label": "Enable buzzer", "checked": "false", "global": true},
      {"name": "voldown", "type": "ACCheckbox", "label": "Volume down on shutdown", "checked": "false", "global": true},
      {"name": "volsteps", "type": "ACRange", "label": "Volume steps", "min": "1", "max": "10", "magnify": "infront", "global": true},
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
unsigned long ledMillis= 0;
unsigned long powerMillis= 0;
const unsigned long ledMillisInterval = 25;
const unsigned long powerMillisInterval = 500;

void beep(Buzzer buzzer) {
  switch (buzzer) {
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

void writeLED(uint16_t R, uint16_t G, uint16_t B){
  ledcWrite(CHR, R);
  ledcWrite(CHG, G);
  ledcWrite(CHB, B);
}

void changeState(Status status){
  switch(status){
    case standby: writeLED(170,0,0);
      break;
    case idle: writeLED(0,0,0);
      break;
    case error: writeLED(255,0,0);
      break;
    case onAction: writeLED(0,0,150);
      break;
    case onBoot: writeLED(0,255,0);
      break;
    case onEspError: writeLED(255,0,255);
      break;
  }
}


void sendOpto(Opto opto) {
  switch (opto) {
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

//TODO: REFACTOR DE ESTO URGENTE. FUNCIONA!!!
void sendProntoStr(String code) {
  Serial.print("Sending code");
  uint16_t codeArr[PRONTOLENGTH];
  String tempString;
  int index = 0;
  for (uint16_t i = 0; i < code.length(); i++) {
    char currentChar = code.charAt(i);
    if (currentChar != ' ') {
      tempString += currentChar;
      if (tempString.length() == 4) {
        codeArr[index] = strtol(tempString.c_str(), NULL, 16);
        index++;
        tempString = "";
      }
      if (index == PRONTOLENGTH) break;
    }
  }
  irsend.sendPronto(codeArr, PRONTOLENGTH);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  changeState(Status::onAction);
  payload[length] = '\0';
  strTopic = String((char*)topic);
  if (strTopic == "cmnd/audioreceiver/" + chipID + "/BTN") {
    strPayload = String((char*)payload);
    if (strPayload == "POWER") sendOpto(Opto::power);
    else if (strPayload == "BAND") sendOpto(Opto::band);
    else if (strPayload == "FMMODE") sendOpto(Opto::fmmode);
    else if (strPayload == "MEMORY") sendOpto(Opto::memory);
    else if (strPayload == "TUNINGD") sendOpto(Opto::tuningd);
    else if (strPayload == "TUNINGU") sendOpto(Opto::tuningu);
    else if (strPayload == "DTUNING") sendOpto(Opto::dtuning);
    else if (strPayload == "SPKA") sendOpto(Opto::spka);
    else if (strPayload == "SPKB") sendOpto(Opto::spkb);
    else if (strPayload == "VCR1") sendOpto(Opto::vcr1);
    else if (strPayload == "CD") sendOpto(Opto::cd);
    else if (strPayload == "TUNER") sendOpto(Opto::tuner);
    else if (strPayload == "PHONO") sendOpto(Opto::phono);
  }
  
  if (strTopic == "cmnd/audioreceiver/" + chipID + "/IR") {
    sendProntoStr(String((char*)payload));
  }
  
  if (strTopic == "cmnd/audioreceiver/" + chipID + "/LED") {
    String strData = String((char*)payload);
    writeLED(strData.substring(0,2).toInt(), strData.substring(4,6).toInt(), strData.substring(8,10).toInt()); 
  }
  changeState(Status::idle);
}

String loadSettingsInPage(AutoConnectAux& aux, PageArgument& args) {
  aux[F("header")].as<AutoConnectInput>().value = "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Wall Socket settings - ID:" + chipID + "</h2>";
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
  File file = SPIFFS.open(settingsFile, "r");
  if (file && aux.loadElement(file)) loadSavedSettings(aux);
  file.close();
}

String saveSettingsFile(AutoConnectAux& aux, PageArgument& args) {
  AutoConnectAux& settings = aux.referer();
  loadSavedSettings(settings);
  File file = SPIFFS.open(settingsFile, "w");
  if (file) {
    settings.saveElement(file, {"mqttServer", "mqttUser", "mqttPass", "buzzer", "voldown", "volsteps"});
    file.close();
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
    changeState(Status::idle);
  } else {
    changeState(Status::error);
    delay(2000);
  }
}

void hardwareInit() {
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
  pinMode(OPTOPOWER, OUTPUT);
  digitalWrite(OPTOPOWER, LOW);
  pinMode(POWERSENS, INPUT);
  ledcSetup(CHB, 12000, 8);
  ledcSetup(CHG, 12000, 8);
  ledcSetup(CHR, 12000, 8);
  ledcAttachPin(LEDB, CHB);
  ledcAttachPin(LEDG, CHG);
  ledcAttachPin(LEDR, CHR);
  changeState(Status::onBoot);
  irsend.begin();
}

void softwareInit() {
  for (int i = 0; i < 17; i = i + 8) {
    chipIDRaw |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  chipID = String(chipIDRaw);
  SPIFFS.begin();
}

void autoConnectInit() {
  config.autoReconnect = true;
  config.reconnectInterval = 1;
  config.apid = String(CLIENTID) + " - " + chipID;
  config.title = "Audio receiver - ID:" + chipID;
  config.hostName = "Audio receiver-" + chipID;
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
//enum Status {standby, idle, error, onAction, onBoot, onEspError};
// changeState(Status status){
void setup() {
  Serial.begin(115200);
  Serial.print("Booting");
  hardwareInit();
  softwareInit();
  autoConnectInit();
  if (buzzer) beep(Buzzer::set);
}

void millisLoop() {
  unsigned long current = millis();
  if (current - ledMillis >= ledMillisInterval) {
    ledMillis = current;
  }
  if (current - powerMillis >= powerMillisInterval) {
    powerMillis = current;
  }
}

void loop() {
  portal.handleClient();
  if (!client.connected()) mqttReconnect();
  client.loop();
  millisLoop();
}
