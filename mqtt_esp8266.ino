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

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
ADC_MODE(ADC_VCC);

char ssid[32] = "";
char password[32] = "";
const char* mqtt_server = "www.test.yaktor";

const char *softAP_ssid = "ESP_ap";
const char *softAP_password = "12345678";

IPAddress dns(172, 18, 0, 2);
IPAddress gateway(10, 0, 1, 5);
IPAddress subnet(255, 255, 255, 0);

IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

ESP8266WebServer server(80);

const char* sub = "demo.Outlet/state/+/1";
const int BLUE_LED = 2;
const int BACK_OFF_MAX = 30;
const int BUZZER_PIN = 13;

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

/** Last time I tried to connect to WLAN */
long lastConnectTry = 0;

/** Current WLAN status */
int status = WL_IDLE_STATUS;

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);
  Serial.print("Configuring access point...");

  /* You can remove the password parameter if you want the AP to be open. */
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(softAP_ssid, softAP_password);
  delay(500); // Without delay I've seen the IP address blank
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  loadCredentials(); // Load WLAN credentials from network
  connect = strlen(ssid) > 0; // Request WLAN connect if there is a SSID
  setup_server();
  pinMode(BLUE_LED, OUTPUT);
  pinMode(0, INPUT);
  digitalWrite(BLUE_LED, HIGH);
  digitalWrite(BUILTIN_LED, HIGH);
  //pinMode(0, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(0), buttonPushed, RISING);
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

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  WiFi.config(WiFi.localIP(), gateway, subnet, dns);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
//  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      BACK_OFF = 1;
      Serial.println("connected");
      // Once connected, publish an announcement...
      //client.publish("outTopic", "hello world");
      // ... and resubscribe
      client.subscribe(sub);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      if (BACK_OFF > BACK_OFF_MAX) {
        BACK_OFF = BACK_OFF_MAX;
      }
      int dd = 200 * BACK_OFF++;
      Serial.printf(" try again in %i milli.\n", dd);
      // Wait delay seconds before retrying
      delay(dd);
    }
//  }
}
void loop() {
  if (connect) {
    connect = false;
    setup_wifi();
    client.setServer(mqtt_server, 80);
    client.setCallback(callback);
  }
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    reconnect();
    digitalWrite(BUILTIN_LED, HIGH);
  } else {
    digitalWrite(BUILTIN_LED, LOW);
    if (buttoned == 1) {
      buttoned = 2;
      char json[255];
      sprintf(json, "{\"agentData\": {\"_id\": \"1\"}, \"data\": {\"volts\": %d}}", ESP.getVcc());
      client.publish("demo.Switch/flip", json);
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
  tone(BUZZER_PIN, 250, 250);
}
void blink_it () {
  int state = digitalRead(BLUE_LED);
  digitalWrite(BLUE_LED, !state);
}
void callback(char* topic, byte* payload, unsigned int length) {
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
    // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    beeper.detach();
    blinker.detach();
    noTone(8);
    digitalWrite(BLUE_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }

}
