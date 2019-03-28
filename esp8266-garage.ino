#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <DHT.h>
#include "secrets.h"

// Wifi details
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;
WiFiClient espClient;
long lastMsg = 0;
char msg[50];
int value = 0;

// Mqtt details
const char* mqtt_server = MQTT_SERVER;
int mqtt_port = MQTT_PORT;
long millisConnected;
PubSubClient client(espClient);

// Temperature sensor setup
const int DHTPIN = D4;
#define DHTTYPE DHT22
const int readFrequency = 60000;
static unsigned long lastRefreshTime = 0;
DHT dht(DHTPIN, DHTTYPE);

// Door opener relay setup
const int RELAYPIN = D1;
const int openerDuration = 300;
bool previousGarageState = false;

//Door state sensor setup
const int reedPin = D7;

//Car presence/distance setup
const int trigPin = D5;
const int echoPin = D6;
long duration;
int distance;

void setup() {
  Serial.begin(115200);
  randomSeed(micros());
  setup_wifi();
  Serial.println("Booting");

  // MQTT SETUP
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  // PIN SETUP
  pinMode(RELAYPIN, OUTPUT);
  pinMode(trigPin, OUTPUT); // Prepare the pin as Input pin
  pinMode(echoPin, INPUT); // Prepare the pin as Input pin
  pinMode(reedPin, INPUT);


  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(HOST_NAME);

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

}

void loop() {
  ArduinoOTA.handle();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if ( doorOpen() != previousGarageState ) {
    garageStatusLoop();
    previousGarageState = doorOpen();
  }


  if ( millis() - lastRefreshTime >= readFrequency) {
    dhtLoop();
    garageStatusLoop();
    lastRefreshTime = millis();
  }
}



void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  //if (millis() - millisConnected < 10000){
  //  Serial.println("Connected for less than 10000ms, ignoring message");
  //} else {
  activateOpener();
  //}

}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    Serial.println(clientId);
    // Attempt to connect
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
      millisConnected = millis();
      client.subscribe("garage/door/actuator");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void garageStatusLoop() {
  if (doorOpen()) {
        int i = 0;
    while (i < 10 && doorOpen() == true)
    {
      delay(10);
      i++;
    }
    if (i == 10) {
      client.publish("garage/door/status", "open");
      Serial.println("door open");
    }

  } else {
    int i = 0;
    while (i < 10 && doorOpen() == false)
    {
      delay(10);
      i++;
    }
    if (i == 10) {
      client.publish("garage/door/status", "closed");
      Serial.println("door closed");

    }
  }
}


void dhtLoop() {
  float h = dht.readHumidity();
  if ((!isnan(h))) {
    String hpayload;
    hpayload = String(h, 1);
    client.publish("garage/humidity", (char*) hpayload.c_str());
  }
  float t = dht.readTemperature();
  if ((!isnan(t))) {
    String tpayload;
    tpayload = String(t, 1);
    client.publish("garage/temperature", (char*) tpayload.c_str());
  }
}

void activateOpener() {
  digitalWrite(RELAYPIN, HIGH);   // Turn the LED on (Note that LOW is the voltage level
  delay(openerDuration);
  digitalWrite(RELAYPIN, LOW);   // Turn the LED on (Note that LOW is the voltage level
}


int getDistance() {
  // Clears the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.034 / 2;
  Serial.println(distance);
  String dpayload;
  dpayload += distance;

  client.publish("garage/door/distance", (char*) dpayload.c_str());
  return distance;
}

bool doorOpen() {
  if (digitalRead(reedPin) == HIGH) {
    return false;
  } else return true;
}
