const char* serverIndexFmt = "<div> Device ID: %s</div> <div><a href='/wifi'>set up wifi</a></div><div><a href='/code'>print label</a></div><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";
char serverIndex[255];

/** Handle root or redirect to captive portal */
void handleRoot() {
  sprintf(serverIndex, serverIndexFmt, cid);
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", serverIndex);
}
void handleCode() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.sendContent(
    "<html>"
    "<head>"
    "<style type='text/css'>"
    "@media print {"
    "  @page {"
    "    size: 3cm 3.5cm;"
    "    margin: 0cm;"
    "  }"
    "}"
    "body {"
    "  margin: 0cm;"
    "}"
    ".code {"
    "  text-align: center; width:3cm; height:.4cm;"
    "}"
    "</style>"
    "<script type='text/javascript' src='qrcode.js'></script>"
    "<script type='text/javascript' src='html5-qrcode.js'></script>"
    "</head><body>"
    "<div class='code'>" +String(cid) + "</div>"
    "<div id='qrcode'></div>"
    "<script type='text/javascript'>"
    "var text = '"+String(cid)+"';"
    "var code = showQRCode(text, 1, 'H');"
    "var element = document.getElementById('qrcode');"
    "if(element.lastChild) {"
    "  element.replaceChild(code, element.lastChild);"
    "} else {"
    "  element.appendChild(code);"
    "}"
    "</script></body></html>");
}
/** Wifi config page handler */
void handleWifi() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.sendContent(
    "<html><head></head><body>"
    "<h1>Wifi config</h1>"
  );
  if (server.client().localIP() == apIP) {
    server.sendContent(String("<p>You are connected through the soft AP: ") + softAP_ssid + "</p>");
  } else {
    server.sendContent(String("<p>You are connected through the wifi network: ") + ssid + "</p>");
  }
  server.sendContent(
    "\r\n<br />"
    "<table><tr><th align='left'>SoftAP config</th></tr>"
  );
  server.sendContent(String() + "<tr><td>SSID " + String(softAP_ssid) + "</td></tr>");
  server.sendContent(String() + "<tr><td>IP " + toStringIp(WiFi.softAPIP()) + "</td></tr>");
  server.sendContent(
    "</table>"
    "\r\n<br />"
    "<table><tr><th align='left'>WLAN config</th></tr>"
  );
  server.sendContent(String() + "<tr><td>SSID " + String(ssid) + "</td></tr>");
  server.sendContent(String() + "<tr><td>IP " + toStringIp(WiFi.localIP()) + "</td></tr>");
  server.sendContent(
    "</table>"
    "\r\n<br />"
    "<table><tr><th align='left'>WLAN list (refresh if any missing)</th></tr>"
  );
  Serial.println("scan start");
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      server.sendContent(String() + "\r\n<tr><td>SSID <button value=\""+WiFi.SSID(i)+"\" onclick='document.getElementById(\"sid\").value=this.value' >" + WiFi.SSID(i)+"</button>" + String((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":" *") + " (" + WiFi.RSSI(i) + ")</td></tr>");
    }
  } else {
    server.sendContent(String() + "<tr><td>No WLAN found</td></tr>");
  }
  server.sendContent(String() +
    "</table>"
    "\r\n<br /><form method='POST' action='wifisave'><h4>Connect to network:</h4>"
    "<br /><label><input type='checkbox' name='w'/>Update wifi Settings:</label>"
    "<br /><input type='text' placeholder='network' id='sid' name='n' value=\""+ssid+"\"/>"
    "<br /><input type='password' placeholder='password' name='p'/>"
    "<br /><label><input type='checkbox' name='a'/>Update advanced Settings:</label>"
    "<br /><input type='text' placeholder='gateway' name='g' value='"+gateway+"'/>"
    "<br /><input type='text' placeholder='dns' name='d' value='"+dns+"'/>"
    "<br />App Settings:<br /><input type='text' placeholder='MQTT server' name='m'  value='"+mqtt_server+"'/>"
    "<br /><input type='submit' value='Connect/Update'/></form>"
    "<p>You may want to <a href='/'>return to the home page</a>.</p>"
    "</body></html>"
  );
  server.send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
  
  //server.client().stop(); // Stop is needed because we sent no content length
}

/** Handle the WLAN save form and redirect to WLAN config page again */
void handleWifiSave() {
  Serial.println("wifi save");
  // update wifi
  if(server.hasArg("w")) {
    server.arg("n").toCharArray(ssid, sizeof(ssid) - 1);
    server.arg("p").toCharArray(password, sizeof(password) - 1);
  }
  // update advanced
  if(server.hasArg("a")) {
    server.arg("g").toCharArray(gateway, sizeof(gateway) - 1);
    server.arg("d").toCharArray(dns, sizeof(dns) - 1);
  }
  // only update server if specified
  if(server.arg("m").length()>0) {
     server.arg("m").toCharArray(mqtt_server, sizeof(mqtt_server) - 1);
  }
  server.sendHeader("Location", "wifi", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send ( 302, "text/plain", "");  // Empty content inhibits Content-length header so we have to close the socket ourselves.
  server.client().stop(); // Stop is needed because we sent no content length
  saveSettings();
  connect = strlen(ssid) > 0; // Request WLAN connect with new credentials if there is a SSID
}

bool handleFileRead(String path){
  SPIFFS.begin();
  handleFileReadInt(path);
  SPIFFS.end();
}
bool handleFileReadInt(String path){
  Serial.println("handleFileRead: " + path);
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}
String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send ( 404, "text/plain", message );
}
