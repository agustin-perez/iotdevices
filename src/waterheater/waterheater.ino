#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <FS.h> 

//HARDWARE ----------------------
//BOARD
#define BZ1         32
//SOCKET 0
#define PT100       13
//SOCKET 1
#define THERMOSTAT   23
//SOCKET 2
#define RELAY       22   
//SOCKET 3
#define LEDB        16  //S3-Y
#define LEDG        17  //S3-W
#define LEDR        5   //S3-R
//CHANNELS ----------------------
#define CHBZ1       0
#define CHR         3
#define CHG         2
#define CHB         1
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
bool power;
unsigned long ledMillis= 0;
unsigned long thermostatMillis= 0;
unsigned long PT100Millis= 0;
const unsigned long ledMillisInterval = 25;
const unsigned long thermostatMillisInterval = 500;
const unsigned long PT100MillisMillisInterval = 500;

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
    case onAction: writeLED(0,0,255);
      break;
    case onBoot: writeLED(0,255,0);
      break;
    case onEspError: writeLED(255,0,255);
      break;
  }
}

void setRelayPower(bool power){
  if (power){
    power = true;
    digitalWrite(RELAY, HIGH);
    client.publish(("stat/waterheater/" + chipID + "/POWER").c_str(), "ON");
    beep(Buzzer::on);
  } else {
    power = false;
    digitalWrite(RELAY, LOW);
    client.publish(("stat/waterheater/" + chipID + "/POWER").c_str(), "OFF");
    beep(Buzzer::off);
  }

}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  changeState(Status::onAction);
  payload[length] = '\0';
  strTopic = String((char*)topic);
  if (strTopic == "cmnd/homeassistant/watchdog") {
    client.publish(("avail/waterheater/" + chipID).c_str(), "ONLINE");
  }
  
  if (strTopic == "cmnd/waterheater/" + chipID + "/POWER") {
    String res = String((char*)payload);
    if (res == "ON") setRelayPower(true);
    else if (res == "OFF") setRelayPower(false);
  }

  if (strTopic == "cmnd/waterheater/" + chipID + "/LED") {
    String strData = String((char*)payload);
    writeLED(strData.substring(0,2).toInt(), strData.substring(4,6).toInt(), strData.substring(8,10).toInt()); 
  }
  changeState(Status::idle);
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
    client.subscribe(("cmnd/waterheater/" + chipID + "/POWER").c_str());
    client.subscribe(("cmnd/waterheater/" + chipID + "/LED").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/POWER").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/TEMP").c_str());
    client.subscribe("cmnd/homeassistant/watchdog");
    client.publish(("avail/waterheater/" + chipID).c_str(), "ONLINE");
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
  ledcSetup(CHB, 12000, 8);
  ledcSetup(CHG, 12000, 8);
  ledcSetup(CHR, 12000, 8);
  ledcAttachPin(LEDB, CHB);
  ledcAttachPin(LEDG, CHG);
  ledcAttachPin(LEDR, CHR);
  changeState(Status::onBoot);
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
//enum Status {standby, idle, error, onAction, onBoot, onEspError};
// changeState(Status status){
void setup() {
  hardwareInit();
  softwareInit();
  autoConnectInit();
  beep(Buzzer::set);
  if (alwaysOn) setRelayPower(true);
}

void millisLoop() {
  bool currentThermostat = digitalRead(THERMOSTAT);
  if (currentThermostat != prevThermostat){
    prevThermostat = currentThermostat;
    if (currentThermostat) client.publish(("stat/waterheater/" + chipID + "/POWER").c_str(), "ON");
    else client.publish(("stat/waterheater/" + chipID + "/POWER").c_str(), "OFF");
  }
  unsigned long current = millis();
  if (current - ledMillis >= ledMillisInterval) {
    ledMillis = current;
  }
  if (current - PT100Millis >= PT100MillisMillisInterval) {
    PT100Millis = current;
  }
}

void loop() {
  portal.handleClient();
  if (!client.connected()) mqttReconnect();
  client.loop();
  millisLoop();
}

/*


#include <Arduino.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <DHT.h>
#include <ESP8266WiFi.h>

//DEFINITIONS
#define boardLed 2        //D4
#define relay 5           //D1
#define dht11 4           //D2
#define thRLED 14         //D5
#define thGLED 12         //D6
#define thBLED 13         //D7
#define DHTTYPE DHT11
#define STAMqttServerAddress ""
#define STAMqttUserName ""
#define STAMqttPwd ""
#define STAMqttClientID  "WaterHeater"

const char* mqttServerAddress = STAMqttServerAddress;
const char* mqttUserName = STAMqttUserName;
const char* mqttPwd = STAMqttPwd;
const char* mqttClientID = STAMqttClientID;
const int stepsPerRevolution = 2048;
const int ldr = A0;
bool prevThermostat = false;
bool relayState = false;
bool thermostat = false;
float prevTemp;
String strTopic;
String strPayload;
unsigned long previousMillis = 0;   
const long interval = 1000;
static const char HELLO_PAGE[] PROGMEM = R"(
{ "title": "Water Heater", "uri": "/", "menu": true, "element": [
    { "name": "caption", "type": "ACText", "value": "<h2>Water Heater - 40lt</h2>",  "style": "text-align:center;color:#2f4f4f;padding:10px;" },
    { "name": "content", "type": "ACText", "value": "ESP8266 management page" } ]
}
)";    

//INITIALIZATIONS
WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server;                              
AutoConnect portal(server);   
AutoConnectConfig config;           
AutoConnectAux hello;      
DHT dht(dht11, DHTTYPE, 15);

//MQTT CALLBACK FUNCTION
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  strTopic = String((char*)topic);
  int payloadInt = (int)payload;

  //RELAY SET
  if (strTopic == "cmnd/waterHeater/power") {
    switch(atoi((char*)payload)){
      case 1:
        digitalWrite(relay, HIGH);
        digitalWrite(thGLED, HIGH);
        relayState=true;
        client.publish("stat/waterHeater/power", "1"); 
      break;
      case 2:
        digitalWrite(relay, LOW);
        digitalWrite(thRLED, LOW);
        digitalWrite(thGLED, LOW);
        digitalWrite(thBLED, LOW);
        relayState=false;
        client.publish("stat/waterHeater/power", "2");  
        client.publish("stat/waterHeater/thermostat", "off");  
        thermostat = false;
        prevThermostat = false;
      break;
    }
  }
}

//MQTT RECONNECT
void mqttReconnect() {
  while (!client.connected()) {
    if (client.connect(mqttClientID, mqttUserName, mqttPwd)) {
      client.subscribe("avail/waterHeater");
      client.subscribe("cmnd/waterHeater/temp");
      client.subscribe("cmnd/waterHeater/thermostat");
      client.subscribe("cmnd/waterHeater/power");
      client.subscribe("stat/waterHeater/sensor");
      client.subscribe("stat/waterHeater/power");
      client.publish("avail/waterHeater", "Online");
    } else {
      delay(5000);
    }
  }
}

void rgbThermostat(float temp){
  digitalWrite(thGLED, LOW);
  if (temp <= 10){
    analogWrite(thRLED, 0);
    analogWrite(thBLED, 255);
  }
  if (temp <= 20 && temp >=10){
    analogWrite(thRLED, 150);
    analogWrite(thBLED, 255);
  }
  if (temp <= 30 && temp >=20){
    analogWrite(thRLED, 255);
    analogWrite(thBLED, 255);
  }
  if (temp <= 40 && temp >=30){
    analogWrite(thRLED, 255);
    analogWrite(thBLED, 150);
  }
  if (temp <= 50 && temp >=40){
    analogWrite(thRLED, 255);
    analogWrite(thBLED, 80);
  }
   if (temp > 50){
    analogWrite(thRLED, 255);
    analogWrite(thBLED, 0);
  }
}

void setup() {
  pinMode(boardLed, OUTPUT);
  pinMode(relay, OUTPUT);
  pinMode(thRLED, OUTPUT);
  pinMode(thGLED, OUTPUT);
  pinMode(thBLED, OUTPUT);
  digitalWrite(boardLed, HIGH);
  digitalWrite(relay, LOW);
  digitalWrite(thRLED, LOW);
  digitalWrite(thGLED, HIGH);
  digitalWrite(thBLED, LOW);
  pinMode(ldr, INPUT);
  dht.begin();
  client.setServer(mqttServerAddress, 1883);
  client.setCallback(mqttCallback);
  config.ota = AC_OTA_BUILTIN;      
  portal.config(config);           
  hello.load(HELLO_PAGE);         
  portal.join({ hello });           
  portal.begin();   
}

void loop() {
  if (!client.connected()) {
    mqttReconnect();
  }
  client.loop();
  portal.handleClient(); 
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    client.publish("avail/waterHeater", "Online");
    float temp = dht.readTemperature();
    int ldrStatus = analogRead(ldr);
    if (temp != prevTemp){
      client.publish("stat/waterHeater/sensor", String(temp).c_str());
      prevTemp = temp;
    }
    if (relayState){
      client.publish("stat/waterHeater/power", "1"); 
      if (ldrStatus > 300){
        thermostat = true;
      } else{
        thermostat = false;
      }
      if (prevThermostat != thermostat){
        if (thermostat){
          client.publish("stat/waterHeater/thermostat", "on");  
        } else {
          client.publish("stat/waterHeater/thermostat", "off");    
        }
        prevThermostat = thermostat;
      }
      if (thermostat){
        rgbThermostat(temp);
      } else if (relayState) {
        digitalWrite(thRLED, LOW);
        digitalWrite(thGLED, HIGH);
        digitalWrite(thBLED, LOW);
      } else {
        digitalWrite(thRLED, LOW);
        digitalWrite(thGLED, LOW);
        digitalWrite(thBLED, LOW);
      }
    } else {
      client.publish("stat/waterHeater/power", "2"); 
    }                   
  }
}*/
