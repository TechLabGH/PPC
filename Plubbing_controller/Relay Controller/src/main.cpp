#include <Arduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// WiFi Connection Details
const char* ssid          = "SSID";
const char* password      = "wifiPassword";

// MQTT Broker Connection Details
const char* mqtt_server   = "mqtt.broker.server.cloud";
const char* mqtt_username = "broker_user";
const char* mqtt_password = "broker_password";
const int mqtt_port       =8883;

// MQTT Message details
const char* topictxt      = "pump";

// NTP setup
const long utcOffsetInSeconds = 3600;
char daysOfTheWeek[7][4] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

// Init Secure WiFi Connectivity
WiFiClientSecure espClient;

// Init MQTT Client
PubSubClient client(espClient);

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
uint16_t ntpSyncT = 0;

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
#define ONE_WIRE_BUS D5
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
DeviceAddress sensor1 = { 0x28, 0xFF, 0x77, 0x62, 0x40, 0x17, 0x4, 0x31 };
DeviceAddress sensor2 = { 0x28, 0xFF, 0xB4, 0x6, 0x33, 0x17, 0x3, 0x4B };
float temp1 = 0.00;     // IN
float temp2 = 0.00;     // OUT

// Delay timer
long A_timer = millis();

// ON/OFF timer
uint8_t off_h = 0;
uint8_t off_m = 0;
uint8_t on_h = 0;
uint8_t on_m = 0;
uint8_t Mode1T = 0;

// Mode
uint8_t workMode = 0;
         // 0 - Turned OFF
         // 1 - Turned ON (temp controlled)
         // 2 - Turned ON (30 min)
         // 3 - Scheduled ON
uint8_t schedH = 0;
uint8_t schedM = 0;

// WiFi connection setup
void setup_wifi() {
  delay(10);
  Serial.print("\nConnecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  randomSeed(micros());
  Serial.println("\nWiFi connected\nIP address: ");
  Serial.println(WiFi.localIP());
}

// Broker connection setup
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-";   // Create a random client ID
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("connected");

      client.subscribe(topictxt);   // subscribe the topics here

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");   // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// Message received
void callback(char* topic, byte* payload, unsigned int length) {
  String incommingMessage = "";
  for (uint8_t i = 0; i < length; i++) incommingMessage+=(char)payload[i];

  // incomming message: incommingMessage

    // Mode: turn ON - AUTO
    if(incommingMessage == "ON_AUTO") {
      workMode = 1;
      Mode1T = 0;
      Serial.println("Mode:1 (temp compare) request");
      A_timer = A_timer + 30000LU;
    }

    // Mode: turn ON - TIME
    if(incommingMessage == "ON_TIME") {
      workMode = 2;
      off_h = timeClient.getHours();
      off_m = timeClient.getMinutes() + 30;
      if( off_m > 59) {
        off_m = off_m - 60;
        off_h = off_h + 1;
      }
      if ( off_h == 24 ) off_h = 0;
      Serial.println("Mode:2 (30 min) request");
      A_timer = A_timer + 30000LU;
    }

    if (incommingMessage.substring(0, 7) == "ON_SCHD") {
      workMode = 3;
      on_h = incommingMessage.substring(7, 8).toInt() * 10 + incommingMessage.substring(8, 9).toInt();
      on_m = incommingMessage.substring(9, 10).toInt() * 10 + incommingMessage.substring(10, 11).toInt();
      Serial.println("Mode:3 (scheduled ON) request: " + incommingMessage.substring(7,9) + ":" + incommingMessage.substring(9,11));
      A_timer = A_timer + 30000LU;
    }

    if (incommingMessage == "OFF") {
      workMode = 0;
      Serial.println("TURN OFF request");
      A_timer = A_timer + 30000LU;
    }
}

// Publish message
void publishMessage(String payload , boolean retained){
  if (client.publish(topictxt, payload.c_str(), true))
      Serial.println("Message publised ["+String(topictxt)+"]: "+payload);
}

void setup() {

  Serial.begin(9600);
  while (!Serial) delay(1);

  pinMode(D6, OUTPUT);        // Relay
  digitalWrite(D6, LOW);  
  
  // call function to connect WiFi
    setup_wifi();

  // setting MQTT connection
    espClient.setInsecure();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    reconnect();

  // init NTP client
    timeClient.begin();
    timeClient.update();
      Serial.println("Time synced");
      Serial.print(daysOfTheWeek[timeClient.getDay()]);
      Serial.print(", ");
      Serial.print(timeClient.getHours());
      Serial.print(":");
      Serial.print(timeClient.getMinutes());
      Serial.print(":");
      Serial.println(timeClient.getSeconds());
}

void loop() {

  // calling function to check incomming message
  client.loop();

  // temp and status check every 30 sec
  if(millis() > A_timer + 30000LU) {

    sensors.requestTemperatures();
    temp1 = sensors.getTempC(sensor1);
    temp2 = sensors.getTempC(sensor2);

    // Turning on (Mode=3) when Schedule set and time reached
    if (workMode == 3 && on_h == timeClient.getHours() && on_m == timeClient.getMinutes()) {
      workMode = 2;
      off_h = timeClient.getHours();
      off_m = timeClient.getMinutes() + 30;
      if( off_m > 59) {
        off_m = off_m - 60;
        off_h = off_h + 1;
      }
      if ( off_h == 24 ) off_h = 0;
      Serial.println("Pump ON by schedule - will switch-off in 30 min at " + String(off_h) + ":" + String(off_m));
    }

    // Tutning off from Mode=1
    if (workMode == 2 && off_h == timeClient.getHours() && off_m == timeClient.getMinutes()) {
      workMode = 0;
      Serial.println("Turning OFF by timer");
    }

    // Mode = 2 - compare imput and aoutput temd to detect full circulation
    if (workMode == 1) {
      Mode1T++;

      // Pump will run for at least 1 min
      if (Mode1T > 2 && temp2 >= temp1 - 5){
        workMode = 0;
        Mode1T = 0;
      }
      Serial.println("Pump running in auto mode until circulation is completed (temp compare)");
    }

    // ON / OFF Relay
    if (workMode == 1 || workMode == 2) 
          digitalWrite(D6, HIGH);
    else  digitalWrite(D6, LOW);

      A_timer = millis();
      ntpSyncT++;
      // sync NTP every 24h but only if pump is not running and not schedued
      if (ntpSyncT > 2880 && workMode == 0) {
        timeClient.update();
        ntpSyncT = 0;
      }
  publishMessage("Mode"+ String(workMode) + "/" + String(temp1) + "/" + String(temp2), true);
  Serial.println("Ststus: Mode: "+ String(workMode) + " T1: " + String(temp1) + " T2: " + String(temp2));
  
  }
}

/*
Mode 0
------
Pump is turned off
Status message is sent to broker every about 30 sec.
Mode0/21.00/15.00
|     |     |
|     |     Temperature on return from plumbing system
|     Temperature on output from boiler
Mode#
mqtt-message: OFF

Mode 1 - Running auto cycle based on temperature compare
------
Pump will run for at least 1 min (Mode1T > 2). After this controlled compare temperature 
"before" and "after" plumbing and runs until difference between them is < 5C.
mqtt-message: ON_AUTO

Mode 2 - Running on 30 min timer
------
Contoller turns-on pump and keeps it working for 30 min controlled by timer (off_h:off_m)
that set when request is received.
mqtt-message: ON_TIME

Mode 3 - Schedulig delayed cycle
------
Request message set time, when contoller will start pump (hh:mm). While waiting Mode3 is used.
At the scheduled time, conroller turns pump on, set end time and switch to Mode2 same way as it was
manually requested. Timer can be set up to 24h in advance.
mqtt-message: ON_SCHDhhmm   <- where hh:mm is ON time in 24h format.

Sending mqtt-message OFF - switch to Mode0 causing shutting-off pump and disabling any running timers.

* Requesting Mode3 while pump is running will switch it-off
* All transitions betweend modes are valid.
  - sending Mode2 request while this mode is already running will extend time for another 30 min.
* Internal RTC is synced every 24h but only in Mode0 

Mode:3 (scheduled ON) request: 23:15                  < - received ON request for 23:15
Message publised [pompa]: Mode3/-127.00/-127.00
Ststus: Mode: 3 T1: -127.00 T2: -127.00
...
Message publised [pompa]: Mode3/-127.00/-127.00
Ststus: Mode: 3 T1: -127.00 T2: -127.00
Pump ON by schedule - will switch-off in 30 min at 23:45  < - on 23:15 turns-ON and set OFF time for 23:45
Message publised [pompa]: Mode2/-127.00/-127.00
Ststus: Mode: 2 T1: -127.00 T2: -127.00
Message publised [pompa]: Mode2/-127.00/-127.00
Ststus: Mode: 2 T1: -127.00 T2: -127.00
Message publised [pompa]: Mode2/-127.00/-127.00
Ststus: Mode: 2 T1: -127.00 T2: -127.00
Mode:3 (scheduled ON) request: 23:50               < - received new ON request for 23:50
Message publised [pompa]: Mode3/-127.00/-127.00    < = turns off (Mode3) and wait for new scheduled ON
Ststus: Mode: 3 T1: -127.00 T2: -127.00
Message publised [pompa]: Mode3/-127.00/-127
*/