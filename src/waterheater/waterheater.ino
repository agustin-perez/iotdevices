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
}
