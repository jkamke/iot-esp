/*
  Basic ESP8266 MQTT example

  This sketch demonstrates the capabilities of the pubsub library in combination
  with the ESP8266 board/library.

  It connects to an MQTT server then:
  - publishes "hello world" to the topic "outTopic" every two seconds
  - subscribes to the topic "inTopic", printing out any messages
    it receives. NB - it assumes the received payloads are strings not binary
  - If the first character of the topic "inTopic" is an 1, switch ON the ESP Led,
    else switch it off

  It will reconnect to the server if the connection is lost using a blocking
  reconnect function. See the 'mqtt_reconnect_nonblocking' example for how to
  achieve the same result without blocking the main loop.

  To install the ESP8266 board, (using Arduino 1.6.4+):
  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"

*/
#define IP_FORWARD 1
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
extern "C" {
#include "user_interface.h" 
}
ADC_MODE(ADC_VCC);

//160bytest left in EEPROM(512)
char ssid[32] = "";
char password[32] = "";
char gateway[16] = "";
char dns[16] = "";
char mqtt_server[256] = "";

const char *softAP_ssidFmt = "ESP_%d";
char softAP_ssid[64];
const char *softAP_password = "12345678";

IPAddress dnsIPaddr;
IPAddress gatewayIPaddr;

IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

ESP8266WebServer server(80);
int id = ESP.getChipId();

const char* sub = "demo.Outlet/state/+/%d";
const int BLUE_LED = 2;
const int GREEN_LED = 13;
const int BACK_OFF_MAX = 30;
const int BUZZER_PIN = 14;
const int BUTTON_PIN = 4;

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;
int buttoned = 0;
int BACK_OFF = 1;
Ticker beeper;
Ticker blinker;

/** Should I connect to WLAN asap? */
boolean connect;
boolean connected = false;

/** Last time I tried to connect to WLAN */
long lastConnectTry = 0;

/** Current WLAN status */
int status = WL_IDLE_STATUS;

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);
  Serial.print("Configuring access point...");

  /* You can remove the password parameter if you want the AP to be open. */
  sprintf(softAP_ssid,softAP_ssidFmt,id);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(softAP_ssid, softAP_password);
  delay(500); // Without delay I've seen the IP address blank
  Serial.printf("ESP id: %d\n",id);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  loadSettings(); // Load WLAN credentials from network
  connect = strlen(ssid) > 0; // Request WLAN connect if there is a SSID
  setup_server();
  pinMode(BLUE_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(BLUE_LED, HIGH);
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(BUILTIN_LED, LOW);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonPushed, RISING);
}
void setup_server() {

  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/wifisave", HTTP_POST, handleWifiSave);
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.setDebugOutput(true);
      Serial.printf("Update: %s\n", upload.filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(false);
    }
    yield();
  });
  server.onNotFound ( handleNotFound );
  server.begin();

  Serial.print("Ready! Open http://");
  Serial.print(WiFi.localIP());
  Serial.print(" in your browser\n");

}
void setup_wifi() {
  int i = 0;
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.printf("sidd: %s\n", ssid);
  Serial.println(ssid);
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {
    delay(500);
    Serial.print(".");
  }
  if(WiFi.status() == WL_CONNECTED && strlen(gateway) > 0) {
    gatewayIPaddr.fromString(gateway);
    dnsIPaddr.fromString(dns);
    Serial.printf("gateway: %s\n", gatewayIPaddr.toString().c_str());
    Serial.printf("dns: %s\n", dnsIPaddr.toString().c_str());
    Serial.printf("subnet: %s\n", WiFi.subnetMask().toString().c_str());
    WiFi.config(WiFi.localIP(), gatewayIPaddr, WiFi.subnetMask(), dnsIPaddr);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
  }
  connected = true;
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  //MQTT setup
  client.setCallback(onClientMessage);
}

void reconnect() {
  // Loop until we're reconnected
//  while (!client.connected()) {
    client.setServer(mqtt_server, 80);
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(softAP_ssid)) {
      BACK_OFF = 1;
      Serial.println("connected");
      // Once connected, publish an announcement...
      //client.publish("outTopic", "hello world");
      // ... and resubscribe
      onClientConnect();
//    } else {
//      Serial.print("failed, rc=");
//      Serial.print(client.state());
//      if (BACK_OFF > BACK_OFF_MAX) {
//        BACK_OFF = BACK_OFF_MAX;
//      }
//      int dd = 200 * BACK_OFF++;
//      Serial.printf(" try again in %i milli.\n", dd);
//      // Wait delay seconds before retrying
//      delay(dd);
    }
//  }
}
void loop() {
  if (connect) {
    connect = false;
    setup_wifi();
  }
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    reconnect();
    digitalWrite(BUILTIN_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
  } else {
    digitalWrite(BUILTIN_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
    if (buttoned == 1) {
      buttoned = 2;
      onButtonPushed();
      buttoned = 0;
    }
  }
  client.loop();
  server.handleClient();
}

void buttonPushed() {
  if (buttoned == 0) {
    Serial.println("button pushed");
    buttoned = 1;
  }

}
void beep () {
  tone(BUZZER_PIN, 3000, 250);
}
void blink_it () {
  int state = digitalRead(BLUE_LED);
  digitalWrite(BLUE_LED, !state);
}

// START APP CODE
void onButtonPushed(){
  char json[255];
  sprintf(json, "{\"agentData\": {\"_id\": %d}, \"data\": {\"volts\": %d}}", id, ESP.getVcc());
  client.publish("demo.Switch/flip", json);
}

void onClientConnect() {
  char subId[255];
  sprintf(subId, sub, id);
  client.subscribe(subId);
}

void onClientMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // f for flowing
  if (topic[18] == 'f') {
    beeper.attach(.5, beep);
    blinker.attach(.2, blink_it);
  } else {
    beeper.detach();
    blinker.detach();
    noTone(BUZZER_PIN);
    digitalWrite(BLUE_LED, HIGH); // Turn the LED off by making the voltage HIGH
  }
}
