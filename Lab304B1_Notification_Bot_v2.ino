#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266Ping.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "WebSocketClient.h"
#include "ArduinoJson.h"


// Personal settings
unsigned long lastMillis;
unsigned long currMillis;
unsigned long lastLogMillis;
int logInterval = 1000;
bool ledState = false;
int stateStartWait = 25;
bool startPass = false;
enum STATE {
  INACTIVE,
  INIT,
  OPEN,
  CLOSE
};
STATE state = INACTIVE;
STATE currState = INACTIVE;
int updatePresenceInterval = 3600;
unsigned long lastUpdatePresenceMillis;


int state0StartHour = 7;  // 7
int state0EndHour = 21;   // 21
int state1StartHour = 16; // 16
int state1EndHour = 21;   // 21
int state1WaitMin = 30;   // 30
int state2ResetHour = 0;  // 0

String closeMessage = "😴 Lab 304B1 is closed!";
String openMessage = "🙂 Lab 304B1 is opened!";

//const char * bot_token = "";
//const char* channelId = "1252143622074404864";


// NTP Server settings
const long utcOffsetInSeconds = 25200; // Change this according to your timezone
const char* ntpServer = "pool.ntp.org";
const int updateInterval = 3600000; // update interval (ms)


// Wifi settings

const char ssid[] = "PIF_Client";
const char pass[] = "88888888";

/*
const char ssid[] = "HyTommy's iPhone 11 Pro Max";
const char pass[] = "hygameo123123";
*/
/*
const char ssid[] = "75A_Main_2";
const char pass[] = "hygameo123123";
*/


// Discord settings
const uint16_t gateway_intents = 1 << 9 | 1 << 11;
const char * certificateFingerprint = /*FINGERPRINT>>*/"e1 83 99 07 09 25 67 95 e6 ce 86 5e c7 da 97 73 dc 66 a6 72"/*<<FINGERPRINT*/;
const char *host = "discord.com";
const int httpsPort = 443;
//bool discord_tts = false;

unsigned long heartbeatInterval = 0;
unsigned long lastHeartbeatAck = 0;
unsigned long lastHeartbeatSend = 0;

bool hasWsSession = false;
String websocketSessionId;
bool hasReceivedWSSequence = false;
unsigned long lastWebsocketSequence = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, utcOffsetInSeconds, updateInterval);
ESP8266WiFiMulti WiFiMulti;


//WiFiClientSecure client;

WebSocketClient ws(true);
DynamicJsonDocument doc(4096);

void connectWIFI() {

  WiFiMulti.addAP(ssid, pass);
  WiFi.mode(WIFI_STA);
  

  Serial.print("[WiFi] Connecting to: ");
  Serial.println(ssid);

  while(!(Ping.ping("www.google.com"))){
    WiFiMulti.run();
    Serial.print(".");
    delay(100);
  }

  Serial.println();
  Serial.println("[WiFi] Connected");

}

void sendPresenceUpdate(){

  StaticJsonDocument<512> jsonDoc;
  jsonDoc["op"] = 3;
  JsonObject d = jsonDoc.createNestedObject("d");
  d["since"] = 0;
  d["status"] = state == OPEN ? "online" : "idle";
  d["afk"] = state == OPEN ? false : true;
  JsonArray activities = d.createNestedArray("activities");
  JsonObject activity = activities.createNestedObject();
  activity["name"] = "blank";
  activity["state"] = state == OPEN ? openMessage : closeMessage;
  activity["type"] = 4; // 0: Playing, 1: Streaming, 2: Listening, 3: Watching, 4: Custom, 5: Competing

  String jsonString;
  serializeJson(jsonDoc, jsonString);
  ws.send(jsonString);
  Serial.println("Activity Status is updated!");

}

void checkInternetConnection(){
  if (WiFi.status() != WL_CONNECTED) {
    ESP.restart();
  }
}

bool sensorState(){
  return (digitalRead(14) == HIGH);
}

void discordWebsocketHandler(){
  if (!ws.isConnected()){
    Serial.println(F("connecting"));
    ws.setSecureFingerprint(certificateFingerprint);
    ws.connect(F("gateway.discord.gg"), F("/?v=8&encoding=json"), 443);
  }
  else {
    unsigned long now = millis();
    if (heartbeatInterval > 0) {
      if (now > lastHeartbeatSend + heartbeatInterval) {
        StaticJsonDocument<512> jsonDoc;
        jsonDoc["op"] = 1;
        jsonDoc["d"] = hasReceivedWSSequence ? String(lastWebsocketSequence, 10) : "null";
        String jsonString;
        serializeJson(jsonDoc, jsonString);
        ws.send(jsonString);
        lastHeartbeatSend = now;
      }
      if (lastHeartbeatAck > lastHeartbeatSend + (heartbeatInterval / 2)){
        ws.disconnect();
        heartbeatInterval = 0;
      }
    }

    String msg;
    if (ws.getMessage(msg)) {
      Serial.println(String("[WS] recieving: ") + msg);
      DeserializationError err = deserializeJson(doc, msg);
      if (err) {
        Serial.print(F("deserializeJson() failed with code "));
        Serial.println(err.f_str());
        if (err == DeserializationError::NoMemory) {
          Serial.println(F("Try increasing DynamicJsonDocument size"));
        }
      }
      else {
        if (doc["op"] == 0){
          if (doc.containsKey(F("s"))){
            lastWebsocketSequence = doc[F("s")];
            hasReceivedWSSequence = true;
          }
          if (doc[F("t")] == "READY"){
            websocketSessionId = doc[F("d")][F("session_id")].as<String>();
            hasWsSession = true;
          }
        }
        else if (doc["op"] == 9){
          ws.disconnect();
          hasWsSession = false;
          heartbeatInterval = 0;
        }
        else if (doc["op"] == 11){
          lastHeartbeatAck = now;
        }
        else if (doc["op"] == 10){
          heartbeatInterval = doc[F("d")][F("heartbeat_interval")];
          if (hasWsSession){
            StaticJsonDocument<512> jsonDoc;
            jsonDoc["op"] = 6;
            JsonObject d = jsonDoc.createNestedObject("d");
            d["token"] = String(bot_token);
            d["session_id"] = websocketSessionId;
            d["seq"] = String(lastWebsocketSequence, 10);
            String jsonString;
            serializeJson(jsonDoc, jsonString);
            ws.send(jsonString);
          }
          else{
            StaticJsonDocument<512> jsonDoc;
            jsonDoc["op"] = 2;
            JsonObject d = jsonDoc.createNestedObject("d");
            d["token"] = String(bot_token);
            d["intents"] = 1;
            JsonObject properties = d.createNestedObject("properties");
            properties["$os"] = "linux";
            properties["$browser"] = "Google Chrome";
            properties["$device"] = "ESP8266";
            d["compress"] = "false";
            d["large_threshold"] = "250";
            String jsonString;
            serializeJson(jsonDoc, jsonString);
            ws.send(jsonString);
          }
          lastHeartbeatSend = now;
          lastHeartbeatAck = now;
        }
      }
    }
  }
}

void setup() {

  Serial.begin(115200);

  pinMode(14, INPUT); // AM312 -> D5
  pinMode(2, OUTPUT);
  connectWIFI();
  
  // Initialize NTP client
  timeClient.begin();
  // Fetch initial time
  timeClient.update();

  lastMillis = millis();
  lastLogMillis = millis();
  lastUpdatePresenceMillis = millis();

  //sendDiscord("PIF Bot is on!");

}

void loop() {

  checkInternetConnection();
  discordWebsocketHandler();

  currMillis = millis();

  if(currMillis > lastLogMillis + logInterval){
    Serial.println(String(timeClient.getFormattedTime()) + ": State: " + String(state == INACTIVE ? "INACTIVE" : state == INIT ? "INIT" : state == OPEN ? "OPEN" : "CLOSE") + " sensorState: " + String(sensorState()) + " || currMillis: " + String(currMillis) + " || lastMillis: " + String(lastMillis));
    ledState = !ledState;
    digitalWrite(2, ledState);
    lastLogMillis = currMillis;
  }

  if(currMillis > lastMillis + stateStartWait*1000 || startPass){
    if(!startPass) state = INIT;
    startPass = true;

    if(currMillis > lastUpdatePresenceMillis + updatePresenceInterval*1000){
      sendPresenceUpdate();
      lastUpdatePresenceMillis = currMillis;
    }

    if(state != currState){
      sendPresenceUpdate();
      currState = state;
      lastMillis = currMillis;
    }

  }

  if(timeClient.getHours() == state2ResetHour && timeClient.getMinutes() <= 5){
    state = INACTIVE;
    startPass = false;
    lastMillis = currMillis;
    ESP.restart();
  }

  switch(state){
    default:
      break;

    case INIT:
      if(timeClient.getHours() >= state0StartHour && timeClient.getHours() < state0EndHour){
        if(sensorState()){
          state = OPEN;
        }   
      }
      else if(timeClient.getHours() > state0EndHour){
        state = CLOSE;
      }
      break;

    case OPEN:
      if(timeClient.getHours() >= state1StartHour && timeClient.getHours() < state1EndHour){
        if(sensorState()){
          lastMillis = currMillis;
        }
        if(currMillis > lastMillis + state1WaitMin*60*1000){
          state = CLOSE;
        }
      }
      else if(timeClient.getHours() > state1EndHour){
        state = CLOSE;
      }
      break;
  }
}
