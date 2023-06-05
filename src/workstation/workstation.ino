#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>

//DEFINITIONS
#define onBoardLED 2        //D4
#define powerBTN 0          //D3
#define powerLED 5          //D1
#define hddLED 4            //D2
#define STAMqttServerAddress ""
#define STAMqttUserName ""
#define STAMqttPwd ""
#define STAMqttClientID "Kaiba PC"

const char* mqttServerAddress = STAMqttServerAddress;
const char* mqttUserName = STAMqttUserName;
const char* mqttPwd = STAMqttPwd;
const char* mqttClientID = STAMqttClientID;
static const char HELLO_PAGE[] PROGMEM = R"(
{ "title": "Kaiba PC", "uri": "/", "menu": true, "element": [
    { "name": "caption", "type": "ACText", "value": "<h2>Kaiba PC IMM</h2>",  "style": "text-align:center;color:#2f4f4f;padding:10px;" },
    { "name": "content", "type": "ACText", "value": "ESP8266 based IMM module" } ]
}
)";      

//INITIALIZATIONS
WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server;                              
AutoConnect portal(server);   
AutoConnectConfig config;           
AutoConnectAux hello;
String strTopic;
String strPayload;
bool powerState=false;
bool prevPowerState=false;
bool hddState=false;
bool pHddState=false;
unsigned long previousMillis = 0;   
const long interval = 1000;

//MQTT CALLBACK FUNCTION
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  strTopic = String((char*)topic);
  if (strTopic == "cmnd/nzxtpc/power") {
    switch(atoi((char*)payload)){
      case 1:
        digitalWrite(powerBTN, HIGH);
        delay(50);
        digitalWrite(powerBTN, LOW);
      break;
      case 2: 
        digitalWrite(powerBTN, HIGH);
        delay(5000);
        digitalWrite(powerBTN, LOW);
      break;
    }
  }
}

//MQTT RECONNECT
void mqttReconnect() {
  while (!client.connected()) {
    if (client.connect(mqttClientID, mqttUserName, mqttPwd)) {
      Serial.println("MQTT Connected");
      client.subscribe("avail/nzxtpc");
      client.subscribe("cmnd/nzxtpc/power");
      client.subscribe("cmnd/nzxtpc/keyboard");
      client.subscribe("stat/nzxtpc/powerled");
      client.subscribe("stat/nzxtpc/hddled");
      client.publish("avail/nzxtpc", "Online");
    } else {
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");
  pinMode(onBoardLED, OUTPUT);
  pinMode(powerBTN, OUTPUT);
  pinMode(powerLED, INPUT);
  pinMode(hddLED, INPUT);
  digitalWrite(powerBTN, LOW);
  client.setServer(mqttServerAddress, 1883);
  client.setCallback(mqttCallback);
  config.ota = AC_OTA_BUILTIN;      
  portal.config(config);           
  hello.load(HELLO_PAGE);         
  portal.join({ hello });           
  portal.begin();    
  Serial.println("Booted");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (!client.connected()) {
    mqttReconnect();
  }
  client.loop();
  portal.handleClient(); 

  if (digitalRead(powerLED) == HIGH){
    powerState=true;     
  } else {
    powerState=false;
  }
  if (digitalRead(hddLED) == HIGH){
    hddState=true;     
  } else {
    hddState=false;
  }
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    client.publish("avail/nzxtpc", "Online");
    if (powerState){
      client.publish("stat/nzxtpc/powerled", "on");  
    } else {
      client.publish("stat/nzxtpc/powerled", "off");  
    }

    if (hddState){
      client.publish("stat/nzxtpc/hddled", "on");  
    } else {
      client.publish("stat/nzxtpc/hddled", "off");  
    }
  }
}

