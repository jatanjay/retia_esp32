#include <Adafruit_MCP4725.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>

#include "webpages.h"

#define FIRMWARE_VERSION "v0.0.1"

const String default_ssid = "Verizon_J9SPGP";
const String default_wifipassword = "cab3-azure-plot";
const String default_httpuser = "admin";
const String default_httppassword = "admin";
const int default_webserverporthttp = 80;


struct Config {
  String ssid;            
  String wifipassword;    
  String httpuser;        
  String httppassword;    
  int webserverporthttp;  
};


Config config;              
bool shouldReboot = false;  
AsyncWebServer *server;     
bool shouldFormat = false;
Adafruit_MCP4725 dac;
#define DAC_ADDRESS 0x60
hw_timer_t *My_timer = NULL;

bool timerFlag = false;
bool onFlag = false;
bool file_opened = false;

void IRAM_ATTR onTimer() { timerFlag = true; }

File file_on_flash;

String listFiles(bool ishtml = false);

void openFile(const char *filename) {
  file_on_flash = SPIFFS.open(filename, "r");
  if (!file_on_flash) {
    Serial.println("Failed to open file for reading");
    return;
  }
}

void transmitToDAC(int value) { dac.setVoltage(value, false); }

void rebootESP(String message) {
  Serial.print("Rebooting ESP32: ");
  Serial.println(message);
  ESP.restart();
}

String listFiles(bool ishtml) {
  String returnText = "";
  Serial.println("Listing files stored on SPIFFS");
  File root = SPIFFS.open("/");
  File foundfile = root.openNextFile();

  String excludeFiles[] = {};

  if (ishtml) {
    returnText +=
        "<table><tr><th align='left'>Name</th><th "
        "align='left'>Size</th><th></th><th></th></tr>";
  }

  while (foundfile) {
    bool exclude = false;
    for (int i = 0; i < sizeof(excludeFiles) / sizeof(excludeFiles[0]); i++) {
      if (String(foundfile.name()) == excludeFiles[i]) {
        exclude = true;
        break;
      }
    }

    if (!exclude) {
      if (ishtml) {
        returnText += "<tr align='left'><td>" + String(foundfile.name()) +
                      "</td><td>" + humanReadableSize(foundfile.size()) +
                      "</td>";
        returnText += "<td><button onclick=\"downloadDeleteButton(\'" +
                      String(foundfile.name()) +
                      "\', \'delete\')\">Delete</button></tr>";
      } else {
        returnText += "File: " + String(foundfile.name()) +
                      " Size: " + humanReadableSize(foundfile.size()) + "\n";
      }
    }
    foundfile = root.openNextFile();
  }

  if (ishtml) {
    returnText += "</table>";
  }

  root.close();
  foundfile.close();
  return returnText;
}

String humanReadableSize(const size_t bytes) {
  if (bytes < 1024)
    return String(bytes) + " B";
  else if (bytes < (1024 * 1024))
    return String(bytes / 1024.0) + " KB";
  else if (bytes < (1024 * 1024 * 1024))
    return String(bytes / 1024.0 / 1024.0) + " MB";
  else
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}





void setup() {
  Serial.begin(115200);

  My_timer = timerBegin(0, 80, true);
  timerAttachInterrupt(My_timer, &onTimer, true);
  timerAlarmWrite(My_timer, 12500, true);
  timerAlarmEnable(My_timer);
  dac.begin(DAC_ADDRESS);

  Serial.print("Firmware: ");
  Serial.println(FIRMWARE_VERSION);

  Serial.println("Booting ...");

  Serial.println("Mounting SPIFFS ...");
  if (!SPIFFS.begin(true)) {
    Serial.println("ERROR: Cannot mount SPIFFS, Rebooting");
    rebootESP("ERROR: Cannot mount SPIFFS, Rebooting");
  }

  Serial.print("SPIFFS Free: ");
  Serial.println(humanReadableSize((SPIFFS.totalBytes() - SPIFFS.usedBytes())));
  Serial.print("SPIFFS Used: ");
  Serial.println(humanReadableSize(SPIFFS.usedBytes()));
  Serial.print("SPIFFS Total: ");
  Serial.println(humanReadableSize(SPIFFS.totalBytes()));

  Serial.println(listFiles());

  Serial.println("Loading Configuration ...");

  config.ssid = default_ssid;
  config.wifipassword = default_wifipassword;
  config.httpuser = default_httpuser;
  config.httppassword = default_httppassword;
  config.webserverporthttp = default_webserverporthttp;

  Serial.print("\nConnecting to Wifi: ");
  WiFi.begin(config.ssid.c_str(), config.wifipassword.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n\nNetwork Configuration:");
  Serial.println("----------------------");
  Serial.print("         SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("  Wifi Status: ");
  Serial.println(WiFi.status());
  Serial.print("Wifi Strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.print("          MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("           IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("       Subnet: ");
  Serial.println(WiFi.subnetMask());
  Serial.print("      Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("        DNS 1: ");
  Serial.println(WiFi.dnsIP(0));
  Serial.print("        DNS 2: ");
  Serial.println(WiFi.dnsIP(1));
  Serial.print("        DNS 3: ");
  Serial.println(WiFi.dnsIP(2));
  Serial.println();

  Serial.println("Configuring Webserver ...");
  server = new AsyncWebServer(config.webserverporthttp);
  configureWebServer();

  Serial.println("Starting Webserver ...");
  server->begin();
}


void loop() {
    if (timerFlag) {


    if (onFlag) {
      if (file_opened == false){
        openFile("/Demo Mode BP.txt");
        file_opened = true;
      }else{
        if (file_on_flash.available()) {

        String dataString = file_on_flash.readStringUntil('\n');
        /*
          * Data in file is stored in mmHg e.g., 60.80
          * corresponds to blood pressure of the patient
          *
          * 0-3v == 0 - 300 mmHg
          */
        int intData = dataString.toFloat() * 100;
        /*
          * Value in file is stored as float with 2 decimal points.
          * Removing decimals by * 100 and storing as integer
          */
        int dacValue = map(intData, 0, 30000, 0, 4095);
        /*
          * 0 - 30,000 corresponds to 0-3v or 0-4096
          */
        transmitToDAC(dacValue);

      } else {
        Serial.println("EOF reached");
        file_on_flash.close();
        onFlag = false; 
      }
      }
    } else {
      if (file_on_flash.available()) {
        file_on_flash.close();
        Serial.println("Closed when OFF pressed");
        file_opened = false;
      }
    }
    timerFlag = false; 
  }

  if (shouldReboot) {
    rebootESP("Web Admin Initiated Reboot");
  } else if (shouldFormat) {
    shouldFormat = false;
    Serial.println("Formatting SPIFFS...");
    if (SPIFFS.format()) {
      Serial.println("SPIFFS formatted successfully.");
      shouldReboot = true;
    } else {
      Serial.println("SPIFFS formatting failed.");
    }
  }
}