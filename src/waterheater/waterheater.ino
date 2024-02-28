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
#define CLIENTID        "Water heater"
#define DHTTYPE         DHT22
#define DEVICEMAXTEMP   70
#define SAFETYTEMP      75
#define MAXSMARTTEMP    50
#define MINSMARTTEMP    -20

enum Buzzer {set, error, on, off, action};
enum Status {standby, idle, onError, onAction, onBoot, onEspError};
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
      {"name": "caption2", "type": "ACText", "value": "User adjustable settings", "posterior": "par"},      
      {"name": "resistanceTemp", "type": "ACRange", "label": "Resistance default temperature °C", "value": "50", "min": "0", "max": "70", "magnify": "behind", "global": true},
      {"name": "buzzer", "type": "ACCheckbox", "label": "Enable buzzer", "checked": "false", "global": true},
      {"name": "alwaysOn", "type": "ACCheckbox", "label": "On by default", "checked": "false", "global": true},
      {"name": "caption3", "type": "ACText", "value": "Advanced  settings", "posterior": "par"},
      {"name": "resistanceHysteresis", "type": "ACRange", "label": "Thermostat hysteresis °C", "value": "5", "min": "1", "max": "15", "magnify": "behind", "global": true},
      {"name": "maxSmartTemp", "type": "ACRange", "label": "Max output smart mode temperature °C", "value": "65", "min": "0", "max": "70", "magnify": "behind", "global": true},
      {"name": "minSmartTemp", "type": "ACRange", "label": "Min output smart mode temperature °C", "value": "50", "min": "0", "max": "70", "magnify": "behind", "global": true},
      {"name": "warmThresholdSet", "type": "ACRange", "label": "Warm threshold range -°C", "value": "20", "min": "0", "max": "40", "magnify": "behind", "global": true},
      {"name": "save", "type": "ACSubmit", "value": "Save", "uri": "/save"},
      {"name": "discard", "type": "ACSubmit", "value": "Cancel", "uri": "/"}]
})";

static const char saveSettingsFilePage[] PROGMEM = R"({"title": "Save", "uri": "/save", "menu": true, "element": [
      { "name": "caption", "type": "ACText", "value": "<h2>Settings saved!</h2>",  "style": "text-align:center;color:#2f4f4f;padding:10px;" }]
})";

//Libs
WiFiClient espClient;
PubSubClient client(espClient);
WebServer server;
AutoConnect portal(server);
AutoConnectConfig config;
AutoConnectAux homePageObj;
AutoConnectAux settingsPageObj;
AutoConnectAux saveSettingsFilePageObj;
DHT dht(TEMPSENS, DHTTYPE);

//Setup Vars
uint16_t chipIDRaw = 0;
String chipID;
String strTopic;
String strPayload;
String mqttServer;
String mqttUser;
String mqttPass;

//Function vars
unsigned int hotThreshold;
unsigned int warmThreshold;
int smartTempValue = 20;
bool prevThermostat;
bool thermostat;
bool devicePower;
bool resistancePower;
bool safety;
bool prevTouch;

//Loop Intervals
unsigned long touchMillis = 0;
unsigned long thermostatMillis = 0;
unsigned long tempSensMillis = 0;
const unsigned long touchMillisInterval = 50;
const unsigned long thermostatMillisInterval = 500;
const unsigned long tempSensMillisMillisInterval = 2000;

//AutoConnect Web Settings
float resistanceTemp = 50;
float resistanceHysteresis = 5;
float maxSmartTemp = 65;
float minSmartTemp = 50;
float warmThresholdSet = 20;
bool buzzer;
bool alwaysOn;

void beep(Buzzer buzzerStatus) {
  if (buzzer) {
    switch (buzzerStatus) {
      case set: tone(BZ1, 3500, 1200);
        break;
      case error: tone(BZ1, 2300, 1200);
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

void writeLED(uint16_t R, uint16_t G, uint16_t B) {
  digitalWrite(LEDR, R);
  digitalWrite(LEDG, G);
  digitalWrite(LEDB, B);
}

void changeState(Status status) {
  switch (status) {
    case standby: writeLED(0, 0, 0);
      break;
    case idle: writeLED(0, 255, 0);
      break;
    case onError: writeLED(255, 50, 0);
      break;
    case onAction: writeLED(255, 255, 255);
      break;
    case onBoot: writeLED(255, 255, 255);
      break;
    case onEspError: writeLED(255, 255, 0);
      break;
  }
}

void setResistancePower(bool power) {
  if (power && safety) {
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

void setTempState(float temp) {
  if (temp < hotThreshold && temp >= warmThreshold) {
    if (temp >= warmThreshold + (hotThreshold - warmThreshold) / 2) {
      writeLED(255, 0, 70);
    } else {
      writeLED(30, 0, 255);
    }
  } else if (temp >= hotThreshold ) {
    writeLED(255, 0, 0);
  } else {
    writeLED(0, 0, 255);
  }
}

void resistanceTempUpdate() {
  float temp = dht.readTemperature();
  if (devicePower) {
    if (!isnan(temp) && safety) {
      if (temp < resistanceTemp - resistanceHysteresis) setResistancePower(true);
      else if (temp >= resistanceTemp) setResistancePower(false);
      setTempState(temp);
      client.publish(("stat/waterheater/" + chipID + "/temp").c_str(), String(temp).c_str());
    } else {
      beep(Buzzer::error);
      changeState(Status::onError);
      setResistancePower(false);
    }

    if (!isnan(temp) && temp > SAFETYTEMP) {
      beep(Buzzer::error);
      safety = false;
      client.publish(("stat/waterheater/" + chipID + "/safety").c_str(), "UNSAFE TEMPERATURE, SHUTTING DOWN!!!");
      setResistancePower(false);
      changeState(Status::onError);
    } else if (!safety) {
      safety = true;
    }
  }
}

void setResistanceTemp(float temp) {
  Serial.println("Resistance temp set to" + (int)temp);
  resistanceTemp = temp;
  client.publish(("stat/waterheater/" + chipID + "/resistanceTemp").c_str(), String(temp).c_str());
  hotThreshold = temp - resistanceHysteresis;
  warmThreshold = hotThreshold + resistanceHysteresis - warmThresholdSet;
  resistanceTempUpdate();
}

void smartTemp(float temp) {
  Serial.println("Smart temp set to" + (int)temp);
  unsigned int targetTemp = DEVICEMAXTEMP - temp;
  if (targetTemp > maxSmartTemp) {
    setResistanceTemp(maxSmartTemp);
  } else if (targetTemp <= maxSmartTemp && targetTemp > minSmartTemp) {
    setResistanceTemp(targetTemp);
  } else {
    setResistanceTemp(minSmartTemp);
  }
}

void setDevicePower(bool power) {
  if (power) {
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
    client.publish(("stat/waterheater/" + chipID + "/smartTemp").c_str(), String(smartTempValue).c_str());
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
    if (temp <= DEVICEMAXTEMP) {
      setResistanceTemp(temp);
      resistanceTempUpdate();
    } else {
      Serial.println("Invalid temp: " + temp);
    }
  }

  if (strTopic == "cmnd/waterheater/" + chipID + "/smartTemp") {
    changeState(Status::onAction);
    int temp = atoi((char*)payload);
    if (temp > MAXSMARTTEMP) temp = MAXSMARTTEMP;
    else if (temp < MINSMARTTEMP) temp = MINSMARTTEMP;
    smartTempValue = temp;
    client.publish(("stat/waterheater/" + chipID + "/smartTemp").c_str(), String(smartTempValue).c_str());
    if (temp < 0) temp = 0;
    smartTemp(temp);
    resistanceTempUpdate();
  }
}

String loadSettingsInPage(AutoConnectAux& aux, PageArgument& args) {
  //Element static settings
  aux[F("header")].as<AutoConnectText>().value = "<h2 style='text-align:center;color:#2f4f4f;margin-top:10px;margin-bottom:10px'>Water heater settings - ID:" + chipID + "</h2>";
  aux[F("buzzer")].as<AutoConnectCheckbox>().labelPosition = AC_Infront;
  aux[F("alwaysOn")].as<AutoConnectCheckbox>().labelPosition = AC_Infront;
  //MQTT settings
  aux[F("mqttServer")].as<AutoConnectInput>().value = mqttServer;
  aux[F("mqttUser")].as<AutoConnectInput>().value = mqttUser;
  aux[F("mqttPass")].as<AutoConnectInput>().value = mqttPass;
  //User settings
  aux[F("resistanceTemp")].as<AutoConnectRange>().value = resistanceTemp;
  aux[F("buzzer")].as<AutoConnectCheckbox>().value = buzzer;
  aux[F("alwaysOn")].as<AutoConnectCheckbox>().value = alwaysOn;
  //Advanced settings
  aux[F("resistanceHysteresis")].as<AutoConnectRange>().value = resistanceHysteresis;
  aux[F("maxSmartTemp")].as<AutoConnectRange>().value = maxSmartTemp;
  aux[F("minSmartTemp")].as<AutoConnectRange>().value = minSmartTemp;
  aux[F("warmThresholdSet")].as<AutoConnectRange>().value = warmThresholdSet;
  return String();
}

String loadSavedSettings(AutoConnectAux& aux) {
  //MQTT settings
  mqttServer = aux[F("mqttServer")].as<AutoConnectInput>().value;
  mqttUser = aux[F("mqttUser")].as<AutoConnectInput>().value;
  mqttPass = aux[F("mqttPass")].as<AutoConnectInput>().value;
  //User settings
  resistanceTemp = aux[F("resistanceTemp")].as<AutoConnectRange>().value;
  AutoConnectCheckbox& buzzerCheckbox = aux[F("buzzer")].as<AutoConnectCheckbox>();
  buzzer = buzzerCheckbox.checked;
  AutoConnectCheckbox& alwaysOnCheckbox = aux[F("alwaysOn")].as<AutoConnectCheckbox>();
  alwaysOn = alwaysOnCheckbox.checked;
  //Advanced settings
  resistanceHysteresis = aux[F("resistanceHysteresis")].as<AutoConnectRange>().value;
  maxSmartTemp = aux[F("maxSmartTemp")].as<AutoConnectRange>().value;
  minSmartTemp = aux[F("minSmartTemp")].as<AutoConnectRange>().value;
  warmThresholdSet = aux[F("warmThresholdSet")].as<AutoConnectRange>().value;
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
    settings.saveElement(file, {"mqttServer", "mqttUser", "mqttPass", "resistanceTemp", "buzzer", "alwaysOn", "resistanceHysteresis", "maxSmartTemp", "minSmartTemp", "warmThresholdSet"});
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
    client.subscribe(("cmnd/waterheater/" + chipID + "/smartTemp").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/power").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/thermostat").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/resistance").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/resistanceTemp").c_str());
    client.subscribe(("stat/waterheater/" + chipID + "/smartTemp").c_str());
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
  setResistancePower(false);
  if (alwaysOn) setDevicePower(true);
  else setDevicePower(false);
}

void millisLoop() {
  unsigned long currentMillis = millis();
  bool currentThermostat = !digitalRead(THERMOSTAT);

  if (currentThermostat != prevThermostat) {
    prevThermostat = currentThermostat;
    if (currentThermostat) client.publish(("stat/waterheater/" + chipID + "/thermostat").c_str(), "on");
    else client.publish(("stat/waterheater/" + chipID + "/thermostat").c_str(), "off");
  }

  if (currentMillis - touchMillis >= touchMillisInterval) {
    touchMillis = currentMillis;
    bool touch = digitalRead(TOUCH);
    if (prevTouch != touch) {
      prevTouch = touch;
      if (touch) setDevicePower(!devicePower);
    }
  }

  if (currentMillis - tempSensMillis >= tempSensMillisMillisInterval) {
    tempSensMillis = currentMillis;
    if (devicePower) resistanceTempUpdate();
  }
}

void loop() {
  portal.handleClient();
  if (!client.connected()) mqttReconnect();
  client.loop();
  millisLoop();
}
