#include <Arduino.h>
#include <ESP8266WiFi.h>           //https://github.com/esp8266/Arduino
#include <Wire.h>

//needed for library
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager.git
ESP8266WebServer server(80);
const char* configurationAPName = "AutoConnectAP";

//SSD1306 I2C
#include <SH1106.h>              //https://github.com/squix78/esp8266-oled-ssd1306.git
// Include custom images
#include "images.h"
// Display Settings
SH1106 display(0x3c, D1, D2);

//BMP280
#include "Seeed_BME280.h"
BME280 bme; // I2C

#include <math.h>
#include <FS.h>                            //stockage de settings
#include <ArduinoJson.h>                   //https://github.com/bblanchon/ArduinoJson.git
StaticJsonBuffer<1000> jsonBuffer;

const int led = 13;
unsigned long interval = 1000;             // refresh display interval
unsigned long prevMillis = 0;
unsigned int localPort = 2390;             // local port to listen for UDP packets

//*****************
//* initial setup *
//*****************
void setup() {
  // put your setup code here, to run once:
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(115200);
  //saut de ligne pour l'affichage dans la console.
  Serial.println();
  Serial.println();

  SPIFFS.begin();
  loadSettings();

  // initialize dispaly
  display.init();
  display.clear();
  display.display();

  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);
  display.drawString(64, 10, "Initialisation...");
  display.display();

  //bme280
  if (!bme.init()) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //reset saved settings
  //wifiManager.resetSettings();

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(180);

  //set custom ip for portal
  //wifiManager.setAPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect(configurationAPName);
  //or use this for auto generated name ESP + ChipID
  //wifiManager.autoConnect();

  if(WiFi.status() != WL_CONNECTED) {
      //relance l'application après le timeout du portail
      //en cas d'arret secteur où la box redemarre lentement.
      ESP.restart();
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp8266")) {
      Serial.println("MDNS responder started");
  }

  server.on("/", handleRoot);
  server.on("/altitude", HTTP_GET, handleAltitude);
  server.on("/humidity", HTTP_GET, handleHumidity);
  server.on("/pressure", HTTP_GET, handlePressure);
  server.on("/temperature", HTTP_GET, handleTemperature);
  // server.on("/params", HTTP_GET, handleParams);
  // server.on("/params", HTTP_POST, handleUpdateParams);
  // server.on("/action", HTTP_GET, handleAction);
  // server.on("/action", HTTP_POST, handleUpdateAction);
  // server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

  displayManagement();
}

//*************
//* main loop *
//*************

void loop() {
    // put your main code here, to run repeatedly:
    server.handleClient();

    unsigned long currMillis = millis();
    // gestion de l'interval entre les actions d'affichage
    if(currMillis > (prevMillis + interval)) {
        prevMillis = currMillis;
        displayManagement();
    }
}

//**********************
//* display management *
//**********************

void displayManagement() {
    display.clear();
    display.setFont(ArialMT_Plain_10);

    //wifi signal indicator
    if(WiFi.status() == WL_CONNECTED) {
        const long signalWiFi = WiFi.RSSI();
        if(signalWiFi > -100) {
            int strength = 0;
            if(signalWiFi > (-60)) {
                strength = 5;
            } else if(signalWiFi > (-70)) {
                strength = 4;
            } else if(signalWiFi > (-75)) {
                strength = 3;
            } else if(signalWiFi > (-80)) {
                strength = 2;
            } else if(signalWiFi > (-85)) {
                strength = 1;
            }
            display.drawXbm(60, 2, 8, 8, wifiSymbol);
            for(int i=0; i < strength; i++) {
                display.drawLine(68 + (i*2), 10, 68 + (i*2), 10 - (i*2));
            }
            display.setFont(ArialMT_Plain_10);
            display.setTextAlignment(TEXT_ALIGN_CENTER);
            display.drawString(96, 54, String(signalWiFi) + "dBm");
        }
    }

    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(32, 11, printTemperature());

    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(96, 11, printHumidity());

    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(32, 32, printPressure());

    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(96, 32, printAltitude());

    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(32, 54, WiFi.localIP().toString());

    display.display();
}

//**********************
//* configuration mode *
//**********************

void configModeCallback (WiFiManager *myWiFiManager) {
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 10, "Configuaration Mode");
    display.drawString(64, 28, "please connect to " + String(configurationAPName));
    display.display();

    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());

    Serial.println(myWiFiManager->getConfigPortalSSID());
}

//**********************
//* setting management *
//**********************
void saveSettings() {
    Serial.println("saving config");
    jsonBuffer.clear();
    JsonObject& json = jsonBuffer.createObject();
    //Serial.println("encodage JSON");
    // json["timezone"] = timezone;
    json.prettyPrintTo(Serial);
    Serial.println();

    File configFile = SPIFFS.open("/settings.json", "w");
    if (!configFile) {
        Serial.println("file open failed");
    } else {
        //Serial.println("ecriture fichier");
        json.printTo(configFile);
    }
    //Serial.println("fermeture dufichier");
    configFile.close();
    Serial.println("saving done");
}

void loadSettings() {
    Serial.println("loading config");
    //creation du fichier avec les valeurs par default si il n'existe pas
    if(!SPIFFS.exists("/settings.json")) {
        //Serial.println("fichier absent -> creation");
        saveSettings();
    }

    //lecture des données du fichier
    File configFile = SPIFFS.open("/settings.json", "r");
    if(!configFile) {
        Serial.println("file open failed");
    } else {
        //Serial.println("lecture fichier");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);

        //Serial.println("décodage JSON");
        jsonBuffer.clear();
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        //json.prettyPrintTo(Serial);
        //Serial.println();
        if (json.success()) {
            //Serial.println("parsing dans les variables");
            // timezone = json["timezone"];
        }
    }
    //Serial.println("fermeture dufichier");
    configFile.close();
    Serial.println("loading done");
}

//*************
//* utilities *
//*************
#define countof(a) (sizeof(a) / sizeof(a[0]))

String formatJSON(const JsonObject& obj) {
    char buffer[1000];
    obj.printTo(buffer, sizeof(buffer));
    String strOut = buffer;
    return strOut;
}

String printTemperature() {
    float value = bme.getTemperature();
    char result[10];
    dtostrf(value,1,2,result);
    String strOut = result;
    return strOut + " C";
}

String printHumidity() {
    float value = bme.getHumidity();
    char result[10];
    dtostrf(value,1,2,result);
    String strOut = result;
    return strOut + " %";
}

String printPressure() {
    float value = bme.getPressure()/100;
    char result[10];
    dtostrf(value,1,0,result);
    String strOut = result;
    return strOut + " hP";
}

String printAltitude() {
    float value = bme.calcAltitude(bme.getPressure());
    char result[10];
    dtostrf(value,1,0,result);
    String strOut = result;
    return strOut + " M";
}

//***********
//* routing *
//***********

void handleRoot() {
    digitalWrite(led, 1);
    jsonBuffer.clear();
    JsonObject& root = jsonBuffer.createObject();
    //valeurs
    root["altitude"] = printAltitude();
    root["humidity"] = printHumidity();
    root["pressure"] = printPressure();
    root["temperature"] = printTemperature();
    root.prettyPrintTo(Serial);
    Serial.println();
    //envoi
    server.send(200, "application/json", formatJSON(root));
    digitalWrite(led, 0);
}

void handleParams() {
    digitalWrite(led, 1);
    jsonBuffer.clear();
    JsonObject& root = jsonBuffer.createObject();
    //paramètres
    // root["interval"] = interval;
    root.prettyPrintTo(Serial);
    Serial.println();
    //envoi
    server.send(200, "application/json", formatJSON(root));
    digitalWrite(led, 0);
}

void handleAltitude() {
    digitalWrite(led, 1);
    jsonBuffer.clear();
    JsonObject& root = jsonBuffer.createObject();
    root["altitude"] = printAltitude();
    root.prettyPrintTo(Serial);
    Serial.println();
    server.send(200, "application/json", formatJSON(root));
    digitalWrite(led, 0);
}

void handleHumidity() {
    digitalWrite(led, 1);
    jsonBuffer.clear();
    JsonObject& root = jsonBuffer.createObject();
    root["humidity"] = printHumidity();
    root.prettyPrintTo(Serial);
    Serial.println();
    server.send(200, "application/json", formatJSON(root));
    digitalWrite(led, 0);
}

void handlePressure() {
    digitalWrite(led, 1);
    jsonBuffer.clear();
    JsonObject& root = jsonBuffer.createObject();
    root["pressure"] = printPressure();
    root.prettyPrintTo(Serial);
    Serial.println();
    server.send(200, "application/json", formatJSON(root));
    digitalWrite(led, 0);
}

void handleTemperature() {
    digitalWrite(led, 1);
    jsonBuffer.clear();
    JsonObject& root = jsonBuffer.createObject();
    root["temperature"] = printTemperature();
    root.prettyPrintTo(Serial);
    Serial.println();
    server.send(200, "application/json", formatJSON(root));
    digitalWrite(led, 0);
}
