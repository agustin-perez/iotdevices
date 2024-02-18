#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <DHT.h>;
#include <FS.h> 

//HARDWARE ----------------------
//BOARD
#define BZ1         32
//SOCKET 0
#define TEMPSENS    0
//SOCKET 1
#define THERMOSTAT  23
//SOCKET 2
#define RELAY       22   
//SOCKET 3
#define TOUCH       15
#define LEDB        5   //S3-Y
#define LEDG        17  //S3-W
#define LEDR        16  //S3-R
//CHANNELS ----------------------
#define CHBZ1       0
//CODE --------------------------
#define FORMAT_ON_FAIL
#define CLIENTID "Water heater"

enum Buzzer {set, on, off, action};
enum Status {standby, idle, error, onAction, onBoot, onEspError};
//enum LEDMode {static, fadein, fadeout};
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
      {"name": "buzzer", "type": "ACCheckbox", "label": "Enable buzzer", "checked": "false", "global": true},
      {"name": "alwaysOn", "type": "ACCheckbox", "label": "Always on - on reboot", "checked": "false", "global": true},
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
DHT dht(TEMPSENS, DHT22);
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
bool isOn;
bool safety;
bool prevTouch;
unsigned long touchMillis = 0;
unsigned long thermostatMillis = 0;
unsigned long DHT22Millis = 0;
const unsigned long touchMillisInterval = 50;
const unsigned long thermostatMillisInterval = 500;
const unsigned long DHT22MillisMillisInterval = 1000;
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
    case error: writeLED(255,0,0);
      break;
    case onAction: writeLED(0,0,255);
      break;
    case onBoot: writeLED(255,255,255);
      break;
    case onEspError: writeLED(255,0,255);
      break;
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

void setRelayPower(bool power){
  if (power && safety){
    isOn = true;
    digitalWrite(RELAY, HIGH);
    client.publish(("stat/waterheater/" + chipID + "/power").c_str(), "on");
    beep(Buzzer::on);
    changeState(Status::idle);
  } else {
    isOn = false;
    digitalWrite(RELAY, LOW);
    client.publish(("stat/waterheater/" + chipID + "/power").c_str(), "off");
    beep(Buzzer::off);
    changeState(Status::idle);
  }

}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  strTopic = String((char*)topic);
  if (strTopic == "cmnd/homeassistant/watchdog") {
    client.publish(("avail/waterheater/" + chipID).c_str(), "online");
  }
  if (strTopic == "cmnd/waterheater/" + chipID + "/power") {
    changeState(Status::onAction);
    String res = String((char*)payload);
    if (res == "on" && safety) setRelayPower(true);
    else if (res == "off") setRelayPower(false);
    changeState(Status::idle);
  }
}

String loadSettingsInPage(AutoConnectAux& aux, PageArgument& args) {
  aux[F("header")].as<AutoConnectInput>().value = "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Water heater settings - ID:" + chipID + "</h2>";
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
  File file = SPIFFS.open(settingsFile, "r");
  if (file && aux.loadElement(file)) loadSavedSettings(aux);
  file.close();
}

String saveSettingsFile(AutoConnectAux& aux, PageArgument& args) {
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
  client.setCallback(mqttCallback);
  client.setServer(mqttServer.c_str(), 1883);
  if (client.connect(mqttClientID, mqttUser.c_str(), mqttPass.c_str())) {
    client.subscribe(("avail/waterheater/" + chipID).c_str());
    client.subscribe(("cmnd/waterheater/" + chipID + "/power").c_str());
    client.subscribe(("cmnd/waterheater/" + chipID + "/led").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/thermostat").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/temp").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/safety").c_str());
    client.subscribe("cmnd/homeassistant/watchdog");
    client.publish(("avail/waterheater/" + chipID).c_str(), "online");
    client.publish(("stat/waterheater/" + chipID + "/safety").c_str(), "safe");
    client.publish(("stat/waterheater/" + chipID + "/thermostat").c_str(), "off");
    changeState(Status::idle);
  } else {
    changeState(Status::error);
    delay(2000);
  }
}

void hardwareInit() {
  pinMode(BZ1, OUTPUT);
  digitalWrite(BZ1, LOW);
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);
  pinMode(THERMOSTAT, INPUT);
  pinMode(TOUCH, INPUT);
  digitalWrite(BZ1, LOW);
  pinMode(LEDR, OUTPUT);
  digitalWrite(LEDR, LOW);
  pinMode(LEDG, OUTPUT);
  digitalWrite(LEDG, LOW);
  pinMode(LEDB, OUTPUT);
  digitalWrite(LEDB, LOW);
  changeState(Status::onBoot);
}

void softwareInit() {
  for (int i = 0; i < 17; i = i + 8) {
    chipIDRaw |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  chipID = String(chipIDRaw);
  dht.begin();
  SPIFFS.begin();
  safety = true;
}

void autoConnectInit() {
  config.autoReconnect = true;
  config.reconnectInterval = 1;
  config.apid = String(CLIENTID) + " - " + chipID;
  config.title = "Water heater - ID:" + chipID;
  config.hostName = "Water-heater-" + chipID;
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
  unsigned long currentMillis = millis();
  bool currentThermostat = !digitalRead(THERMOSTAT);
  
  if (currentThermostat != prevThermostat){
    prevThermostat = currentThermostat;
    if (currentThermostat) { 
      client.publish(("stat/waterheater/" + chipID + "/thermostat").c_str(), "on");
    } else { 
      client.publish(("stat/waterheater/" + chipID + "/thermostat").c_str(), "off");  
    }
  }
  
  if (currentMillis - touchMillis >= touchMillisInterval) {
    touchMillis = currentMillis;
    bool touch = digitalRead(TOUCH);
    if (prevTouch != touch){
      prevTouch = touch;
      if (touch) setRelayPower(!isOn);
    }  
  }
  
  if (currentMillis - DHT22Millis >= DHT22MillisMillisInterval) {
    DHT22Millis = currentMillis;
    float temp = dht.readTemperature();
    if(isOn && safety){
      setTempState(temp);
    } else {
      changeState(Status::standby);
    }
    
    if (temp > 75.0){
      beep(Buzzer::set);
      safety = false;
      client.publish(("stat/waterheater/" + chipID + "/safety").c_str(), "UNSAFE TEMPERATURE, SHUTTING DOWN!!!");
      setRelayPower(false);
      changeState(Status::error);
    } else if (!safety){
      safety = true;
    }
    client.publish(("stat/waterheater/" + chipID + "/temp").c_str(), String(temp).c_str());
  }
}

void loop() {
  portal.handleClient();
  if (!client.connected()) mqttReconnect();
  client.loop();
  millisLoop();
}
