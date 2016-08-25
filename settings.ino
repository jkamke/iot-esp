/** Load WLAN credentials from EEPROM */
void loadSettings() {
  EEPROM.begin(512);
  EEPROM.get(0, ssid);
  EEPROM.get(0+sizeof(ssid), password);
  EEPROM.get(0+sizeof(ssid)+sizeof(password), gateway);
  EEPROM.get(0+sizeof(ssid)+sizeof(password)+sizeof(gateway), dns);
  char ok[2+1];
  EEPROM.get(0+sizeof(ssid)+sizeof(password)+sizeof(gateway)+sizeof(dns), ok);
  EEPROM.get(0+sizeof(ssid)+sizeof(password)+sizeof(gateway)+sizeof(dns)+sizeof(ok), mqtt_server);
  EEPROM.end();
  if (String(ok) != String("OK")) {
    ssid[0] = 0;
    password[0] = 0;
    gateway[0] = 0;
    dns[0] = 0;
    mqtt_server[0] = 0;
  }
  Serial.println("Recovered settings:");
  Serial.printf("sidd: %s\n", ssid);
  Serial.printf("gateway: %s\n", gateway);
  Serial.printf("dns: %s\n", dns);
  Serial.printf("mqtt_server: %s\n", mqtt_server);
  Serial.println(strlen(password)>0?"********":"<no password>");
}

/** Store WLAN credentials to EEPROM */
void saveSettings() {
  EEPROM.begin(512);
  EEPROM.put(0, ssid);
  EEPROM.put(0+sizeof(ssid), password);
  EEPROM.put(0+sizeof(ssid)+sizeof(password), gateway);
  EEPROM.put(0+sizeof(ssid)+sizeof(password)+sizeof(gateway), dns);
  char ok[2+1] = "OK";
  EEPROM.put(0+sizeof(ssid)+sizeof(password)+sizeof(gateway)+sizeof(dns), ok);
  EEPROM.put(0+sizeof(ssid)+sizeof(password)+sizeof(gateway)+sizeof(dns)+sizeof(ok), mqtt_server);
  EEPROM.commit();
  EEPROM.end();
}
