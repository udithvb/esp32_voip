#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include "HTTPUpdateServer.h"
#include <WiFiUdp.h>
#include "NTP.h"
#include "ConfigManager.h"
#include "voipphone.h"
#include "ClickButton.h"
// #include <Adafruit_NeoPixel.h>
// #include <Dusk2Dawn.h>
#include <TimeLib.h>
#include "pages.h"
#include <EEPROM.h>
#include <SPIFFS.h>

#define WS2812PIN 32
#define NUMPIXELS 3
// Adafruit_NeoPixel pixels(NUMPIXELS, WS2812PIN, NEO_GRB + NEO_KHZ400);

#define mDNSUpdate(c)  do {} while(0)
using WebServerClass = WebServer;
using HTTPUpdateServerClass = HTTPUpdateServer;

HTTPUpdateServer httpUpdate;
VOIPPhone doorphone;
bool doorphonerunning = false;

ClickButton button1(4, LOW, CLICKBTN_PULLUP);
ClickButton button2(2, LOW, CLICKBTN_PULLUP);
ClickButton button3(15, LOW, CLICKBTN_PULLUP);

bool islighton=false;
bool isbusy=false;

//
// Config manager...
//

const char *settingsHTML = (char *)"/settings.html";
const char *stylesCSS = (char *)"/styles.css";
const char *mainJS = (char *)"/main.js";

struct Config {
  char device_name[32];
  char telnr_1[32];
  char telnr_2[32];
  char telnr_3[32];
  char sip_user[32];
  char sip_pass[32];
  char sip_ip[32];
  uint8_t mic_gain;
  uint8_t amp_gain;
  int tz_dst;
  int tz_std;
  float lat;
  float lon;
  uint8_t light_r;
  uint8_t light_g;
  uint8_t light_b;
  uint8_t ring_r;
  uint8_t ring_g;
  uint8_t ring_b;
  bool echocompensation;
  uint8_t echodamping;
  uint echothreshold;
} config;

struct Metadata {
  int8_t version;
} meta;

ConfigManager configManager;

WiFiUDP wifiUdp;
NTP ntp(wifiUdp);

void APCallback(WebServer *server) {
    server->on("/styles.css", HTTPMethod::HTTP_GET, [server](){
        //configManager.streamFile(stylesCSS, mimeCSS);
        server->send_P(200,mimeCSS,CSS_styles);
    });

    DebugPrintln(F("AP Mode Enabled. You can call other functions that should run after a mode is enabled ... "));
}


void APICallback(WebServer *server) {
  server->on("/disconnect", HTTPMethod::HTTP_GET, [server](){
    configManager.clearWifiSettings(false);
  });

  server->on("/settings.html", HTTPMethod::HTTP_GET, [server](){
    //configManager.streamFile(settingsHTML, mimeHTML);
    server->send_P(200,mimeHTML,HTML_settings);
  });

  // NOTE: css/js can be embedded in a single page HTML
  server->on("/styles.css", HTTPMethod::HTTP_GET, [server](){
    //configManager.streamFile(stylesCSS, mimeCSS);
    server->send_P(200,mimeCSS,CSS_styles);
  });

  server->on("/main.js", HTTPMethod::HTTP_GET, [server](){
    //configManager.streamFile(mainJS, mimeJS);
    server->send_P(200,mimeJS,JS_main);
  });
  
  server->on("/datetime", HTTPMethod::HTTP_GET, [server](){
    server->send(200, "text/plain", ntp.formattedTime("%d.%m.%Y %X"));
  });
  
  server->on("/debug", HTTPMethod::HTTP_GET, [server](){
    String output = String(ntp.formattedTime("%d.%m.%Y %X"))+"</br>";
    // Dusk2Dawn location = Dusk2Dawn(config.lat, config.lon, config.tz_std/60);
    // int sr_min = location.sunrise( ntp.year(), ntp.month(), ntp.day(), ntp.isDST() );
    // int ss_min = location.sunset( ntp.year(), ntp.month(), ntp.day(), ntp.isDST() );
    int min = ntp.hours()*60+ntp.minutes();
    output += "Now:"+String(min)+"</br>";
    // output += "Sunrise:"+String(sr_min)+"</br>";
    // output += "Sunset:"+String(ss_min)+"</br>";
    // output += "LED0:"+String(pixels.getPixelColor(0));
    // output += "LED1:"+String(pixels.getPixelColor(1));
    // output += "LED2:"+String(pixels.getPixelColor(2));
    server->send(200, mimeHTML, output);
  });

  server->on("/restart", HTTPMethod::HTTP_GET, [server](){
    ESP.restart();
    server->send(200, "text/plain", "restarting...");
  });

  server->on("/dial1", HTTPMethod::HTTP_GET, [server](){
    doorphone.dial(config.telnr_1,config.device_name);
    server->send(200, "text/plain", "ok");
  });

  server->on("/dial2", HTTPMethod::HTTP_GET, [server](){
    doorphone.dial(config.telnr_2,config.device_name);
    server->send(200, "text/plain", "ok");
  });

  server->on("/dial3", HTTPMethod::HTTP_GET, [server](){
    doorphone.dial(config.telnr_3,config.device_name);
    server->send(200, "text/plain", "ok");
  });

  httpUpdate.setup(server);
}

void clearEEPROM() {
    EEPROM.begin(512);  // Initialize EEPROM with size
    for (int i = 0; i < 512; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    EEPROM.end();
}

void setup() {
  Serial.begin(115200);


  // clearEEPROM();
  // Serial.println("Cleared EEPROM");


  // Add WiFi event handler for both AP and STA modes
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
      switch(event) {
          // Existing AP events
          case SYSTEM_EVENT_AP_START:
              Serial.println("AP Started");
              break;
          case SYSTEM_EVENT_AP_STACONNECTED:
              Serial.printf("Device connected. MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                  info.sta_connected.mac[0], info.sta_connected.mac[1],
                  info.sta_connected.mac[2], info.sta_connected.mac[3],
                  info.sta_connected.mac[4], info.sta_connected.mac[5]);
              break;
          case SYSTEM_EVENT_AP_STADISCONNECTED:
              Serial.printf("Device disconnected. MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                  info.sta_disconnected.mac[0], info.sta_disconnected.mac[1],
                  info.sta_disconnected.mac[2], info.sta_disconnected.mac[3],
                  info.sta_disconnected.mac[4], info.sta_disconnected.mac[5]);
              break;
          case SYSTEM_EVENT_AP_PROBEREQRECVED:
              Serial.println("Probe request received");
              break;

          // New STA events
          case SYSTEM_EVENT_STA_START:
              Serial.println("STA Started");
              break;
          case SYSTEM_EVENT_STA_CONNECTED:
              Serial.println("Connected to WiFi network");
              break;
          case SYSTEM_EVENT_STA_GOT_IP:
              Serial.print("Got IP: ");
              Serial.println(WiFi.localIP());
              break;
          case SYSTEM_EVENT_STA_DISCONNECTED:
              Serial.println("Disconnected from WiFi network");
              // Optionally attempt to reconnect
              WiFi.reconnect();
              break;
          case SYSTEM_EVENT_STA_LOST_IP:
              Serial.println("Lost IP address");
              break;
      }
  });
  
  // Memory in KB
  Serial.printf("Free Heap: %.2f KB\n", ESP.getFreeHeap() / 1024.0);
  Serial.printf("Minimum Free Heap: %.2f KB\n", ESP.getMinFreeHeap() / 1024.0);
  Serial.printf("Heap Size: %.2f KB\n", ESP.getHeapSize() / 1024.0);
  Serial.printf("Chip Revision: %d\n", ESP.getChipRevision());
  
  Serial.printf("WiFi Status: %d\n", WiFi.status());
  
  // pixels.begin();
  // pixels.setPixelColor(0, pixels.Color(255, 0, 0));
  // pixels.setPixelColor(1, pixels.Color(0, 255, 0));
  // pixels.setPixelColor(2, pixels.Color(0, 0, 255));
  // pixels.show();
  
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  pinMode(4,INPUT_PULLUP);
  pinMode(2,INPUT_PULLUP);
  pinMode(15,INPUT_PULLUP);

  strcpy(config.device_name,"\0");
  strcpy(config.telnr_1,"\0");
  strcpy(config.telnr_2,"\0");
  strcpy(config.telnr_3,"\0");
  strcpy(config.sip_ip,"\0");
  strcpy(config.sip_user,"\0");
  strcpy(config.sip_pass,"\0");

  meta.version = 1;
  config.tz_dst = 120;
  config.tz_std = 60;
  // Setup config manager
  configManager.setAPName("kubo32");
  configManager.setAPFilename("/index.html");
  // Settings variables 
  configManager.addParameter("device_name", config.device_name, 32);
  configManager.addParameter("telnr_1", config.telnr_1, 32);
  configManager.addParameter("telnr_2", config.telnr_2, 32);
  configManager.addParameter("telnr_3", config.telnr_3, 32);
  configManager.addParameter("sip_ip", config.sip_ip, 32);
  configManager.addParameter("sip_user", config.sip_user, 32);
  configManager.addParameter("sip_pass", config.sip_pass, 32);
  configManager.addParameter("mic_gain", &config.mic_gain);
  configManager.addParameter("amp_gain", &config.amp_gain);
  configManager.addParameter("tz_dst", &config.tz_dst);
  configManager.addParameter("tz_std", &config.tz_std);
  configManager.addParameter("lat", &config.lat);
  configManager.addParameter("lon", &config.lon);
  configManager.addParameter("light_r", &config.light_r);
  configManager.addParameter("light_g", &config.light_g);
  configManager.addParameter("light_b", &config.light_b);
  configManager.addParameter("ring_r", &config.ring_r);
  configManager.addParameter("ring_g", &config.ring_g);
  configManager.addParameter("ring_b", &config.ring_b);
  configManager.addParameter("echocompensation", &config.echocompensation);
  configManager.addParameter("echodamping", &config.echodamping);
  configManager.addParameter("echothreshold", &config.echothreshold);
  // Meta Settings
  configManager.addParameter("version", &meta.version, get);
  // Init Callbacks
  configManager.setAPCallback(APCallback);
  configManager.setAPICallback(APICallback);
  configManager.begin(config);

  ntp.ruleDST("CEST", Last, Sun, Mar, 2, config.tz_dst); // last sunday in march 2:00, timezone +120min (+1 GMT + 1h summertime offset)
  ntp.ruleSTD("CET", Last, Sun, Oct, 3, config.tz_std); // last sunday in october 3:00, timezone +60min (+1 GMT)
  Serial.println("End of setup");


  // // Format SPIFFS if mount fails
  // if(!SPIFFS.begin(true)) {
  //   Serial.println("SPIFFS Mount Failed, formatting...");
  //   SPIFFS.format();
  //   if(!SPIFFS.begin(true)) {
  //     Serial.println("SPIFFS Mount Failed after formatting!");
  //     return;
  //   }
  // }
  // Serial.println("SPIFFS Mount Successful");



}

void loop() {
  // Serial.println("started loop");
  if (WiFi.status() == WL_CONNECTED ) {
    // Serial.println("wifi.status -> wl_connected");
    // ntp.update();
    if(strlen(config.sip_ip)<7) {
      Serial.println("Configuration fault.");
    } else {
      if( !doorphonerunning ) {

        Serial.println("printing config below :");
        Serial.println(config.sip_ip);
        Serial.println(config.sip_user);
        Serial.println(config.sip_pass);
        Serial.println("Online. Starting sip module...");

        if(int result = doorphone.begin(config.sip_ip,config.sip_user,config.sip_pass)==VOIPPHONE_OK) {
          doorphone.setAmpGain(config.amp_gain);
          doorphone.setMicGain(config.mic_gain);
          doorphone.setEchoCompensation(config.echocompensation,config.echothreshold,config.echodamping);
          Serial.println("[OK]");
        } else {
          Serial.print("[ERROR CODE:"+(String)result+"]");
        }
        doorphonerunning = true;
        /*pixels.setPixelColor(0, pixels.Color(255, 255, 255));
        pixels.setPixelColor(1, pixels.Color(255, 255, 255));
        pixels.setPixelColor(2, pixels.Color(255, 255, 255));
        pixels.show();
        pixels.clear();
        pixels.show();
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
        pixels.setPixelColor(1, pixels.Color(0, 0, 0));
        pixels.setPixelColor(2, pixels.Color(0, 0, 0));*/
        // pixels.clear();
        // pixels.show();
      }
      button1.Update();
      button2.Update();
      button3.Update();
      if(button1.clicks==1) {
        Serial.println("Button1 pressed. Dialing...");
        doorphone.dial(config.telnr_1,config.device_name);
        // pixels.setPixelColor(2, pixels.Color(config.ring_r, config.ring_g, config.ring_b));
        // pixels.show();
        isbusy = true;
      } else if(button2.clicks==1) {
        Serial.println("Button2 pressed. Dialing...");
        doorphone.dial(config.telnr_2,config.device_name);
        // pixels.setPixelColor(1, pixels.Color(config.ring_r, config.ring_g, config.ring_b));
        // pixels.show();
        isbusy = true;
      } else if(button3.clicks==1) {
        Serial.println("Button3 pressed. Dialing...");
        doorphone.dial(config.telnr_3,config.device_name);
        // pixels.setPixelColor(0, pixels.Color(config.ring_r, config.ring_g, config.ring_b));
        // pixels.show();
        isbusy = true;
      }
    }
  }
  
  configManager.loop();
  doorphone.loop();

  // hangup
  if(isbusy && !doorphone.isBusy()) {
    isbusy = false;
    // pixels.clear();
    // pixels.show();
    islighton = false;
  }

  // Dusk2Dawn location = Dusk2Dawn(config.lat, config.lon, config.tz_std/60);
  // int sr_min = location.sunrise( ntp.year(), ntp.month(), ntp.day(), ntp.isDST() );
  // int ss_min = location.sunset( ntp.year(), ntp.month(), ntp.day(), ntp.isDST() );
  // int min = ntp.hours()*60+ntp.minutes();
  // if(min > ss_min || min < sr_min) { // lights on
  //   if (!islighton) {
  //     islighton = true;
  //     // pixels.setPixelColor(0, pixels.Color(config.light_r, config.light_g, config.light_b));
  //     // pixels.setPixelColor(1, pixels.Color(config.light_r, config.light_g, config.light_b));
  //     // pixels.setPixelColor(2, pixels.Color(config.light_r, config.light_g, config.light_b));
  //     // pixels.show();
  //   }
  // } else { // lights off
  //   if (islighton) {
  //     islighton = false;
  //     // pixels.clear();
  //     // pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  //     // pixels.setPixelColor(1, pixels.Color(0, 0, 0));
  //     // pixels.setPixelColor(2, pixels.Color(0, 0, 0));
  //     // pixels.show();
  //   }
  // }

  // Serial.println("End of loop");
}