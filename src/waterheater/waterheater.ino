#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <DHT.h>
#include <FS.h> 

//HARDWARE ----------------------
//BOARD
#define BZ1         32
//SOCKET 0
#define TEMPSENS    0
//SOCKET 1
#define THERMOSTAT  23
//SOCKET 2
#define RESISTANCE  22   
//SOCKET 3
#define TOUCH       15
#define LEDB        5   //S3-Y
#define LEDG        17  //S3-W
#define LEDR        16  //S3-R
//CODE --------------------------
#define FORMAT_ON_FAIL
#define CLIENTID    "Water heater"
#define DHTTYPE     DHT22
 
enum Buzzer {set, on, off, action};
enum Status {standby, idle, error, onAction, onBoot, onEspError};
const char* mqttClientID = CLIENTID;
const char* settingsFile = "/settings.json";
const char* urlMqttHome = "/";
const char* urlSettings = "/settings";
const char* urlSettingsSave = "/save";
static const char homePage[] PROGMEM = R"({"title": "Water heater", "uri": "/", "menu": true, "element": [
      {"name": "header", "type": "ACElement", "value": "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Water heater setup page</h2>"},
      { "name": "content", "type": "ACText", "value": "Powered by <a href=https://github.com/Hieromon/AutoConnect>AutoConnect</a>"},
      { "name": "content2", "type": "ACText", "value": "<br>Part of the project <a href=https://github.com/agustin-perez/iotdevices>IoTDevices</a><br>Agustín Pérez"}]
})";
static const char settingsPage[] PROGMEM = R"({"title": "Settings", "uri": "/settings", "menu": true, "element": [
      {"name": "style", "type": "ACStyle", "value": "label+input,label+select{position:sticky;left:140px;width:204px!important;box-sizing:border-box;}"},
      {"name": "header", "type": "ACElement", "value": "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Water heater settings</h2>"},
      {"name": "caption", "type": "ACText", "value": "Setup Home Assistant's MQTT Broker connection", "posterior": "par"},
      {"name": "mqttServer", "type": "ACInput", "label": "Server", "pattern": "^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9])$", "placeholder": "MQTT server address", "global": true},
      {"name": "mqttUser", "type": "ACInput", "label": "User", "global": true},
      {"name": "mqttPass", "type": "ACInput", "label": "Password", "apply": "password", "global": true},
      {"name": "resistanceTemp", "type": "ACRange", "label": "Resistance default temperature", "value": "50", "min": "0", "max": "70", "magnify": "behind", "global": true},
      {"name": "buzzer", "type": "ACCheckbox", "label": "Enable buzzer", "checked": "false", "global": true},
      {"name": "alwaysOn", "type": "ACCheckbox", "label": "Always on after restart", "checked": "false", "global": true},
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
DHT dht(TEMPSENS, DHTTYPE);
uint16_t chipIDRaw = 0;
String chipID;
String strTopic;
String strPayload;
String mqttServer;
String mqttUser;
String mqttPass;
bool buzzer;
bool alwaysOn;
bool prevThermostat;
bool thermostat;
bool devicePower;
bool resistancePower;
bool safety;
bool prevTouch;
unsigned int resistanceTemp;
unsigned long touchMillis = 0;
unsigned long thermostatMillis = 0;
unsigned long tempSensMillis = 0;
const unsigned long touchMillisInterval = 50;
const unsigned long thermostatMillisInterval = 500;
const unsigned long tempSensMillisMillisInterval = 2000;
const float cold_threshold = 35.0; // Temperature below which it feels cold
const float warm_threshold = 45.0; // Temperature above which it feels warm

void beep(Buzzer buzzerStatus) {
  if (buzzer){
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

void writeLED(uint16_t R, uint16_t G, uint16_t B){
  digitalWrite(LEDR, R);
  digitalWrite(LEDG, G);
  digitalWrite(LEDB, B);
}

void changeState(Status status){
  switch(status){
    case standby: writeLED(0,0,0);
      break;
    case idle: writeLED(0,255,0);
      break;
    case error: writeLED(255,50,0);
      break;
    case onAction: writeLED(255,255,255);
      break;
    case onBoot: writeLED(255,255,255);
      break;
    case onEspError: writeLED(255,255,0);
      break;
  }
}

void setResistancePower(bool power){
  if (power && safety){
    Serial.println("Resistance ON");
    resistancePower = true;
    digitalWrite(RESISTANCE, HIGH);
    client.publish(("stat/waterheater/" + chipID + "/resistance").c_str(), "on");
  } else {
    Serial.println("Resistance OFF");
    resistancePower = false;
    digitalWrite(RESISTANCE, LOW);
    client.publish(("stat/waterheater/" + chipID + "/resistance").c_str(), "off");
  }
}

void setTempState(float temp){
  if (temp > 45){
    if (temp > 58){
      writeLED(255,0,0);
    } else {
      writeLED(255,0,70);
    }
  } else {
    if (temp > 35){
      writeLED(50,0,255);
    } else {
      writeLED(0,0,255);
    }
  }
}

void resistanceTempUpdate(){
  float temp = dht.readTemperature();
  if (devicePower){
    if(!isnan(temp) && safety){
      if (temp < resistanceTemp) setResistancePower(true);
      else setResistancePower(false);
      setTempState(temp);
      client.publish(("stat/waterheater/" + chipID + "/temp").c_str(), String(temp).c_str());
    } else {
      changeState(Status::error);
      setResistancePower(false);
      beep(Buzzer::set);
      beep(Buzzer::off);
    }

    if (!isnan(temp) && temp > 75.0){
      beep(Buzzer::set);
      safety = false;
      client.publish(("stat/waterheater/" + chipID + "/safety").c_str(), "UNSAFE TEMPERATURE, SHUTTING DOWN!!!");
      setResistancePower(false);
      changeState(Status::error);
    } else if (!safety){
      safety = true;
    }
  }
}

void setResistanceTemp(unsigned int temp){
  Serial.println("Resistance temp set to" + temp);
  resistanceTemp = temp;
  client.publish(("stat/waterheater/" + chipID + "/resistanceTemp").c_str(), String(temp).c_str());
  resistanceTempUpdate();
}

void setDevicePower(bool power){
  if (power){
    Serial.println("Device ON");
    devicePower = true;
    client.publish(("stat/waterheater/" + chipID + "/power").c_str(), "on");
    resistanceTempUpdate();
    beep(Buzzer::on);
    changeState(Status::idle);
  } else {
    Serial.println("Device OFF");
    devicePower = false;
    client.publish(("stat/waterheater/" + chipID + "/power").c_str(), "off");
    setResistancePower(false);
    beep(Buzzer::off);
    changeState(Status::standby);
  }
}


void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  strTopic = String((char*)topic);

  if (strTopic == "cmnd/homeassistant/watchdog") {
    client.publish(("avail/waterheater/" + chipID).c_str(), "online");

    if (devicePower) client.publish(("stat/waterheater/" + chipID + "/power").c_str(), "on"); 
    else client.publish(("stat/waterheater/" + chipID + "/power").c_str(), "off");

    if (resistancePower) client.publish(("stat/waterheater/" + chipID + "/resistance").c_str(), "on");
    else client.publish(("stat/waterheater/" + chipID + "/resistance").c_str(), "off");

    if (!digitalRead(THERMOSTAT)) client.publish(("stat/waterheater/" + chipID + "/thermostat").c_str(), "on");
    else client.publish(("stat/waterheater/" + chipID + "/thermostat").c_str(), "off");  

    client.publish(("stat/waterheater/" + chipID + "/resistanceTemp").c_str(), String(resistanceTemp).c_str());
  }

  if (strTopic == "cmnd/waterheater/" + chipID + "/power") {
    changeState(Status::onAction);
    String res = String((char*)payload);
    if (res == "on") setDevicePower(true);
    else if (res == "off") setDevicePower(false);
  }

  if (strTopic == "cmnd/waterheater/" + chipID + "/resistanceTemp") {
    changeState(Status::onAction);
    int temp = atoi((char*)payload);
    setResistanceTemp(temp);
    resistanceTempUpdate();
  }
}

String loadSettingsInPage(AutoConnectAux& aux, PageArgument& args) {
  aux[F("header")].as<AutoConnectInput>().value = "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Water heater settings - ID:" + chipID + "</h2>";
  aux[F("mqttServer")].as<AutoConnectInput>().value = mqttServer;
  aux[F("mqttUser")].as<AutoConnectInput>().value = mqttUser;
  aux[F("mqttPass")].as<AutoConnectInput>().value = mqttPass;
  aux[F("resistanceTemp")].as<AutoConnectInput>().value = resistanceTemp;
  aux[F("buzzer")].as<AutoConnectInput>().value = buzzer;
  aux[F("alwaysOn")].as<AutoConnectInput>().value = alwaysOn;
  return String();
}

String loadSavedSettings(AutoConnectAux& aux) {
  mqttServer = aux[F("mqttServer")].as<AutoConnectInput>().value;
  mqttUser = aux[F("mqttUser")].as<AutoConnectInput>().value;
  mqttPass = aux[F("mqttPass")].as<AutoConnectInput>().value;
  resistanceTemp = aux[F("resistanceTemp")].as<AutoConnectRange>().value;
  AutoConnectCheckbox& buzzerCheckbox = aux[F("buzzer")].as<AutoConnectCheckbox>();
  buzzer = buzzerCheckbox.checked;
  AutoConnectCheckbox& alwaysOnCheckbox = aux[F("alwaysOn")].as<AutoConnectCheckbox>();
  alwaysOn = alwaysOnCheckbox.checked;
  return String();
}

void loadSettingsFile(AutoConnectAux& aux) {
  Serial.println("Saving settings to file");
  File file = SPIFFS.open(settingsFile, "r");
  if (file && aux.loadElement(file)) loadSavedSettings(aux);
  file.close();
}

String saveSettingsFile(AutoConnectAux& aux, PageArgument& args) {
  Serial.println("Loading settings");
  AutoConnectAux& settings = aux.referer();
  loadSavedSettings(settings);
  File file = SPIFFS.open(settingsFile, "w");
  if (file) {
    settings.saveElement(file, {"mqttServer", "mqttUser", "mqttPass", "buzzer", "alwaysOn"});
    file.close();
  }
  return String();
}


void mqttReconnect() {
  Serial.println("Reconnecting to MQTT Server");
  client.setCallback(mqttCallback);
  client.setServer(mqttServer.c_str(), 1883);

  if (client.connect(mqttClientID, mqttUser.c_str(), mqttPass.c_str())) {
    client.subscribe("cmnd/homeassistant/watchdog");
    client.subscribe(("avail/waterheater/" + chipID).c_str());
    client.subscribe(("cmnd/waterheater/" + chipID + "/power").c_str());
    client.subscribe(("cmnd/waterheater/" + chipID + "/resistanceTemp").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/power").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/thermostat").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/resistance").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/resistanceTemp").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/temp").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/safety").c_str());
    client.publish(("avail/waterheater/" + chipID).c_str(), "online");
    client.publish(("stat/waterheater/" + chipID + "/safety").c_str(), "safe");
    client.publish(("stat/waterheater/" + chipID + "/thermostat").c_str(), "off");
    client.publish(("stat/waterheater/" + chipID + "/resistanceTemp").c_str(), String(resistanceTemp).c_str());
    changeState(Status::idle);
    Serial.println("MQTT Connected!\n");
  } else {
    changeState(Status::onEspError);
    Serial.println("MQTT Server connection failed... Retrying");
    delay(2000);
  }
}

void hardwareInit() {
  Serial.begin(115200);
  Serial.println("Booting ...");
  pinMode(BZ1, OUTPUT);
  digitalWrite(BZ1, LOW);
  pinMode(RESISTANCE, OUTPUT);
  digitalWrite(RESISTANCE, LOW);
  pinMode(THERMOSTAT, INPUT);
  pinMode(TEMPSENS, INPUT);
  pinMode(TOUCH, INPUT);
  digitalWrite(BZ1, LOW);
  pinMode(LEDR, OUTPUT);
  digitalWrite(LEDR, LOW);
  pinMode(LEDG, OUTPUT);
  digitalWrite(LEDG, LOW);
  pinMode(LEDB, OUTPUT);
  digitalWrite(LEDB, LOW);
  ledcAttachPin(BZ1, 0);
  changeState(Status::onBoot);
}

void softwareInit() {
  Serial.println("Loading efuse mac address");
  for (int i = 0; i < 17; i = i + 8) {
    chipIDRaw |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  chipID = String(chipIDRaw);
  Serial.println("Initializing dht temperature sensor");
  dht.begin();
  Serial.println("Initializing filesystem");
  SPIFFS.begin(true);
  safety = true;
}

void autoConnectInit() {
  Serial.println("Loading AutoConnect");
  config.autoReconnect = true;
  config.reconnectInterval = 1;
  config.apid = String(CLIENTID) + " - " + chipID;
  config.title = "Water heater - ID:" + chipID;
  config.hostName = "Water-heater-" + chipID;
  config.ota = AC_OTA_BUILTIN;
  Serial.println("Loading portal settings");
  portal.config(config);
  homePageObj.load(homePage);
  settingsPageObj.load(settingsPage);
  saveSettingsFilePageObj.load(saveSettingsFilePage);
  portal.join({ homePageObj, settingsPageObj, saveSettingsFilePageObj });
  portal.on(urlSettings, loadSettingsInPage);
  portal.on(urlSettingsSave, saveSettingsFile);
  AutoConnectAux& settingsRestore = *portal.aux(urlSettings);
  Serial.println("Loading settings file");
  loadSettingsFile(settingsRestore);
  portal.begin();
}

void setup() {
  hardwareInit();
  Serial.println("Hardware initialized ---------------- \n");
  softwareInit();
  Serial.println("OS Loaded! ------------------------- \n");
  autoConnectInit();
  Serial.println("AutoConnect ready! ---------------- \n");
  beep(Buzzer::set);
  if (alwaysOn) setDevicePower(true);
  else setDevicePower(false);
}

void millisLoop() {
  unsigned long currentMillis = millis();
  bool currentThermostat = !digitalRead(THERMOSTAT);
  
  if (currentThermostat != prevThermostat){
    prevThermostat = currentThermostat;
    if (currentThermostat) client.publish(("stat/waterheater/" + chipID + "/thermostat").c_str(), "on");
    else client.publish(("stat/waterheater/" + chipID + "/thermostat").c_str(), "off");  
  }
  
  if (currentMillis - touchMillis >= touchMillisInterval) {
    touchMillis = currentMillis;
    bool touch = digitalRead(TOUCH);
    if (prevTouch != touch){
      prevTouch = touch;
      if (touch) setDevicePower(!devicePower);
    }  
  }
  
  if (currentMillis - tempSensMillis >= tempSensMillisMillisInterval) {
    tempSensMillis = currentMillis;
    if(devicePower) resistanceTempUpdate();
  }
}

void loop() {
  portal.handleClient();
  if (!client.connected()) mqttReconnect();
  client.loop();
  millisLoop();
}
