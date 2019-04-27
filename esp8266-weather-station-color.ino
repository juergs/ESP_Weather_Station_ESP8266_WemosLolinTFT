/**The MIT License (MIT) ********************************************************
Copyright (c) 2018 by Daniel Eichhorn
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
See more at https://blog.squix.org
            https://arduinojson.org/v6/api/jsondocument/

Aenderungen:  31.3.2019_js: Anpassungen an BME680 begonnen.
Workaround: TouchControllerWS::getPoint()   invert Xpos for touch.
********************************************************************************/

#include <FS.h>  //--- must be first

//--- configure your settings!
#include "settings.h"
#include "config_website.h"
#include <Arduino.h>
#include <SPI.h>
#if defined(ESP8266)
  #include <ESP8266WiFi.h>  //--- ESP8266 Core WiFi Library         
#else
  #include <WiFi.h>         //--- ESP32 Core WiFi Library    
#endif
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <XPT2046_Touchscreen.h>
#include "TouchControllerWS.h"
#include <JsonListener.h>
#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>
#include <Astronomy.h>
#include <MiniGrafx.h>
#include <Carousel.h>
#include <ILI9341_SPI.h>
#include "ArialRounded.h"
#include "moonphases.h"
#include "weathericons.h"
#include <ESPAsyncWebServer.h>     //Local WebServer used to serve the configuration portal
#include <ESPAsyncWiFiManager.h>   //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Install the following libraries through Arduino Library Manager
 * - Mini Grafx             by Daniel Eichhorn
 * - ESP8266 WeatherStation by Daniel Eichhorn
 * - Json Streaming Parser  by Daniel Eichhorn
 * - simpleDSTadjust        by neptune2
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
  //DNSServer dnsServer;
  //WebServer server(80);
  
  AsyncWebServer  server(80);
  DNSServer       dns;
  
  #define MINI_BLACK  0
  #define MINI_WHITE  1
  #define MINI_YELLOW 2
  #define MINI_BLUE   3
  
  #define MAX_FORECASTS_DEF 12

  //--- defines the colors usable in the paletted 16 color frame buffer
  uint16_t palette[] = {ILI9341_BLACK,    // 0
                        ILI9341_WHITE,    // 1
                        ILI9341_YELLOW,   // 2
                        0x7E3C
                        }; //3
  
  int SCREEN_WIDTH  = 240;
  int SCREEN_HEIGHT = 320;
  
  //--- limited to 4 colors, due to memory constraints
  int BITS_PER_PIXEL = 2;     // 2^2 =  4 colors
  
  //ADC_MODE(ADC_VCC);      //--- comment this line as ADC pin will be externally connected    
  int LDRReading;           //--- ldr data 
  
  ILI9341_SPI     tft = ILI9341_SPI(TFT_CS, TFT_DC);
  MiniGrafx       gfx   = MiniGrafx(&tft, BITS_PER_PIXEL, palette);
  Carousel        carousel(&gfx, 0, 0, 240, 100);

  XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
  TouchControllerWS touchController(&ts);

  OpenWeatherMapCurrentData   currentWeather;
  OpenWeatherMapForecastData  forecasts[MAX_FORECASTS_DEF];
  simpleDSTadjust             dstAdjusted(StartRule, EndRule);
  Astronomy::MoonData         moonData;
    
  void calibrationCallback(int16_t x, int16_t y);
  CalibrationCallback calibration = &calibrationCallback;
  
  //--- include two Adafruit libs which could be found at Github
  #define HAS_BME280      true
  #define HAS_BME680MCU   false

  #if HAS_BME280   
    #include <Adafruit_Sensor.h> //Adafruit sensor library
    #include <Adafruit_BME280.h> //Adafruit library for BME280 sensor      
    Adafruit_BME280   bme(BME_CS); //initiate BME280 sensor  
  #else if HAS_BME680MCU

   
  #endif 
  
  //--- globally  
  float humidity    = 0.0;    //--- keep measured indoor humidity
  float temperature = 0.0;    //--- keep measured indoor temperature

  //--- declare routine to draw indoor data screen
  void    readConfig();
  void    writeConfig();
  String  form_input(const String& name, const String& info, const String& value, const int length);
  String  form_checkbox(const String& name, const String& info, const bool checked);
  String  line_from_value(const String& name, const String& value);
  String  form_select_frame();
  void    notFound(AsyncWebServerRequest *request);
  void    configModeCallback (AsyncWiFiManager *myWiFiManager);
  void    drawIndoorData();
  void    ldr();            //--- get ambient illuminance
  void    updateData();
  void    drawProgress(uint8_t percentage, String text);
  void    drawTime();
  void    drawWifiQuality();
  void    drawCurrentWeather();
  void    drawForecast();
  void    drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
  void    drawAstronomy();
  void    drawCurrentWeatherDetail();
  void    drawLabelValue(uint8_t line, String label, String value);
  void    drawForecastTable(uint8_t start);
  void    drawAbout();
  void    drawSeparator(uint16_t y);
  String  getTime(time_t *timestamp);
  const char*   getMeteoconIconFromProgmem(String iconText);
  const char*   getMiniMeteoconIconFromProgmem(String iconText);
  const char*   PARAM_MESSAGE = "message";
  void    drawForecast1(MiniGrafx *display, CarouselState* state, int16_t x, int16_t y);
  void    drawForecast2(MiniGrafx *display, CarouselState* state, int16_t x, int16_t y);
  void    drawForecast3(MiniGrafx *display, CarouselState* state, int16_t x, int16_t y);

  FrameCallback frames_1[] = { drawForecast1 };
  FrameCallback frames_2[] = { drawForecast1, drawForecast2 };
  FrameCallback frames_3[] = { drawForecast1, drawForecast3, drawForecast2 };

  //--- how many different screens do we have?
  int     screenCount = 6;
  long    lastDownloadUpdate = millis();

  String    moonAgeImage = "";
  uint8_t   moonAge = 0;
  uint16_t  screen = 0;
  long      timerPress;
  bool      canBtnPress;
  time_t    dstOffset = 0;
  bool      config_needs_write = false;

  long      lastDrew = 0;
  bool      btnClick;
  uint8_t   MAX_TOUCHPOINTS = 10;
  TS_Point  points[10];
  uint8_t   currentTouchPoint = 0;

//----------------------------------------------------------------
void setup() 
{
  Serial.begin(115200);  
  delay(1000);   
  Serial.println("*** Started!");

  //--- LED pin needs to set HIGH
  //--- use this pin to save energy
  //--- turn on the background LED  
  //Serial.println(TFT_LED);
  //pinMode(TFT_LED, OUTPUT);
  //digitalWrite(TFT_LED, HIGH);    // HIGH to Turn on;
  
  gfx.init();
  gfx.fillBuffer(MINI_BLACK);
  gfx.commit();

  Serial.println("Initializing touch screen...");
  ts.begin();

  ldr();
  
  drawProgress(70, "FileSystem: Mounting...");
  delay(1000);
  Serial.println("Mounting file system...");
  
  bool isFSMounted = SPIFFS.begin();
  if (!isFSMounted) 
  {
    Serial.println("Not mounted, formatting file system ...");
    drawProgress(90,"Formatting file system");
    SPIFFS.format();
  }
  drawProgress(100,"Formatting done");

  //--- get spiffs data 
  readConfig();

  gfx.fillBuffer(MINI_BLACK);
  gfx.setFont(ArialRoundedMTBold_14);

  drawProgress(10, "WiFiManager: Initializing...");
  Serial.println("WiFiManager! Setup!");
  delay(1000);

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  AsyncWiFiManager wifiManager(&server,&dns);
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  
  wifiManager.setAPCallback(configModeCallback);

  
  if (touchController.isTouched(1000)) 
  {
    drawProgress(10, "WiFiManager: Resetting...");
    delay(1000);
  
    if (!touchController.isTouched(1000)) 
    {
      drawProgress(10, "WiFiManager: Resetting...");
      delay(1000);
      wifiManager.resetSettings();
    } 
    else 
    {
      drawProgress(10, "WiFiManager: Initializing...");
      delay(1000);
    }
  }

  drawProgress(30, "WiFiManager: Auto Connect...");
  delay(1000);
 
  if(!wifiManager.autoConnect(WM_SSID, WM_PASS)) 
  {
    Serial.println("failed to connect and hit timeout");
    drawProgress(80, "WiFiManager: Failed, will reset...");
    delay(1000);
    //--- reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  drawProgress(50, "WiFiManager: Connected...");
  delay(1000);
  
  //---if you get here you have connected to the WiFi
  Serial.println("wlan connected...yeey :)");

  //SPIFFS.remove("/calibration.txt");
  boolean isCalibrationAvailable = touchController.loadCalibration();
  if (!isCalibrationAvailable) 
  {
    Serial.println("Calibration not available");
    touchController.startCalibration(&calibration);
    while (!touchController.isCalibrationFinished()) 
    {
      gfx.fillBuffer(0);
      gfx.setColor(MINI_YELLOW);
      gfx.setTextAlignment(TEXT_ALIGN_CENTER);
      gfx.drawString(120, 160, "Please calibrate\ntouch screen by\ntouch point");
      touchController.continueCalibration();
      gfx.commit();
      yield();
    }
    touchController.saveCalibration();
  }

  gfx.fillBuffer(MINI_BLACK);
  gfx.setFont(ArialRoundedMTBold_14);

  #if HAS_BME280 
    drawProgress(10, "Sensors: Scanning BME280...");
    delay(1000);
    if (!bme.begin()) 
    {  
      Serial.println("Could not find a valid BME280 sensor, check wiring!");
      screenCount = 5;
      drawProgress(50, "Sensors: BME280 not found...");
      delay(1000);
    } 
    else 
    {
      drawProgress(50, "Sensors: BME280 found...");
      delay(1000);
    }
  #elif HAS_BME680MCU
      //****  TODO
      drawProgress(10, "Sensors: Scanning BME880...");
      delay(1000);
  #else
      //****  TODO
  #endif   

  //Set the direction if the automatic transitioning
  carousel.setAutoTransitionBackwards();              //--- without transition goes wrong direction

  //Set the approx. time a transition will take
  carousel.setTimePerTransition(1000);                //--- without transition are 2x faster
  switch (FRAME_COUNT) 
  {
     case (1):
        carousel.disableAutoTransition();
        carousel.setFrames(frames_1, FRAME_COUNT);
        break;
     case (2):
        carousel.setFrames(frames_2, FRAME_COUNT);
        break;
     case (3):
        carousel.setFrames(frames_3, FRAME_COUNT);
        break;
     otherwise:
        carousel.setFrames(frames_3, FRAME_COUNT);
        break;
  }
  carousel.disableAllIndicators();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
     String content = "";
     content += FPSTR(WEB_PAGE_HEADER);
     content += FPSTR(WEB_ROOT_PAGE_CONTENT);
     content += FPSTR(WEB_PAGE_FOOTER_III);
     content.replace("{h}", "Hauptmenü");
     content.replace("{n}", "");
     content.replace("{t}", "");
     request->send(200, "text/html; charset=utf-8", content);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){

     String content = "";
     content += FPSTR(WEB_PAGE_HEADER);
     content += "<hr/><b>Open Weather Map</b><br/><br/>";
     content += "<form method='POST' action'/config'>";
     content += F("<table>");
     content += form_input(F("OPEN_WEATHER_MAP_APP_ID"), F("APP-ID: "), OPEN_WEATHER_MAP_APP_ID, 360);
     content += form_input(F("OPEN_WEATHER_MAP_LOCATION_ID"), F("LOCATION-ID: "), OPEN_WEATHER_MAP_LOCATION_ID, 360);
     content += form_input(F("DISPLAYED_CITY_NAME"), F("CITY-Name: "), DISPLAYED_CITY_NAME, 360);
     content += form_input(F("OPEN_WEATHER_MAP_LANGUAGE"), F("Language: "), OPEN_WEATHER_MAP_LANGUAGE, 360);
     content += F("</table><br/>");
     content += "<hr/><b>Wetter Station</b><br/><br/>";
     content += F("<table>");
     content += form_checkbox(F("IS_METRIC"), F("Metrisches System"), IS_METRIC);
     content += form_checkbox(F("IS_STYLE_12HR"), F("12h Zeitformat"), IS_STYLE_12HR);

     content += F("</table><br/>");
     content += F("<table>");
     content += form_select_frame();
     content += F("</table><br/>");

     content += F("<table>");
     content += form_input(F("NTP_SERVERS_1"), F("NTP Server 1: "), NTP_SERVERS_1, 80);
     content += form_input(F("NTP_SERVERS_2"), F("NTP Server 2: "), NTP_SERVERS_2, 80);
     content += form_input(F("NTP_SERVERS_3"), F("NTP Server 3: "), NTP_SERVERS_3, 80);
     content += form_input(F("UPDATE_INTERVAL_SECS"), F("Update Intervall (sec): "), String(UPDATE_INTERVAL_SECS), 80);
     content += F("</table><br/>");
     content += "<tr><td>&nbsp;</td><td><input type='submit' name='submit' value='Speichern' /></form></td></tr><br/><br/>";
     content += FPSTR(WEB_PAGE_FOOTER);
     content.replace("{h}", "Konfiguration");
     content.replace("{n}", "");
     content.replace("{t}", "");

     Serial.println("SERVER: html generated");

     request->send(200, "text/html; charset=utf-8", content);

  });

  server.on("/config", HTTP_POST, [](AsyncWebServerRequest * request){
     int params = request->params();
     String content = "";

     content += FPSTR(WEB_PAGE_HEADER);

     Serial.println("SERVER: react on submit: " + String(params));

     for(int i = 0; i < params; i++){
       AsyncWebParameter* p = request->getParam(i);
       if(p->isFile()){ //p->isPost() is also true
         Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
       } else if(p->isPost()){
         Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
         if (p->name() == "OPEN_WEATHER_MAP_APP_ID") {
               OPEN_WEATHER_MAP_APP_ID = p->value();
               Serial.println("Updated: OPEN_WEATHER_MAP_APP_ID = " + OPEN_WEATHER_MAP_APP_ID);
               content += line_from_value(F("APP-ID"), OPEN_WEATHER_MAP_APP_ID);
         }
         if (p->name() == "OPEN_WEATHER_MAP_LOCATION_ID") {
               OPEN_WEATHER_MAP_LOCATION_ID = p->value();
               Serial.println("Updated: OPEN_WEATHER_MAP_LOCATION_ID = " + OPEN_WEATHER_MAP_LOCATION_ID);
               content += line_from_value(F("LOCATION-ID"), OPEN_WEATHER_MAP_LOCATION_ID);
         }
         if (p->name() == "DISPLAYED_CITY_NAME") {
               DISPLAYED_CITY_NAME = p->value();
               Serial.println("Updated: DISPLAYED_CITY_NAME = " + DISPLAYED_CITY_NAME);
               content += line_from_value(F("CITY-Name"), DISPLAYED_CITY_NAME);
         }
         if (p->name() == "OPEN_WEATHER_MAP_LANGUAGE") {
               OPEN_WEATHER_MAP_LANGUAGE = p->value();
               Serial.println("Updated: OPEN_WEATHER_MAP_LANGUAGE = " + OPEN_WEATHER_MAP_LANGUAGE);
               content += line_from_value(F("Language"), OPEN_WEATHER_MAP_LANGUAGE);
         }
         if (p->name() == "IS_METRIC") {
               IS_METRIC = p->value().toInt();
               Serial.println("Updated: IS_METRIC = " + String(IS_METRIC));
               content += line_from_value(F("Metrisches System"), String(IS_METRIC));
         }
         if (p->name() == "IS_STYLE_12HR") {
               IS_STYLE_12HR = p->value().toInt();
               Serial.println("Updated: IS_STYLE_12HR = " + String(IS_STYLE_12HR));
               content += line_from_value(F("12h Zeitformat"), String(IS_STYLE_12HR));
         }

         if (p->name() == "FRAME_COUNT") {
               FRAME_COUNT = p->value().toInt();
               Serial.println("Updated: FRAME_COUNT = " + String(FRAME_COUNT));
               content += line_from_value(F("Number of Frames"), String(FRAME_COUNT));
         }

         if (p->name() == "NTP_SERVERS_1") {
               NTP_SERVERS_1 = p->value();
               Serial.println("Updated: NTP_SERVERS_1 = " + NTP_SERVERS_1);
               content += line_from_value(F("NTP Server 1"), NTP_SERVERS_1);
         }
         if (p->name() == "NTP_SERVERS_2") {
               NTP_SERVERS_2 = p->value();
               Serial.println("Updated: NTP_SERVERS_2 = " + NTP_SERVERS_2);
               content += line_from_value(F("NTP Server 2"), NTP_SERVERS_2);
         }
         if (p->name() == "NTP_SERVERS_3") {
               NTP_SERVERS_3 = p->value();
               Serial.println("Updated: NTP_SERVERS_3 = " + NTP_SERVERS_3);
               content += line_from_value(F("NTP Server 3"), NTP_SERVERS_3);
         }
         if (p->name() == "UPDATE_INTERVAL_SECS") {
               UPDATE_INTERVAL_SECS = p->value().toInt();
               Serial.println("Updated: UPDATE_INTERVAL_SECS = " + String(UPDATE_INTERVAL_SECS));
               content += line_from_value(F("Update Intervall (sec)"), String(UPDATE_INTERVAL_SECS));
         }
       } else {
         Serial.printf("GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
       }
     } // for(int i = 0; i < params; i++)

     content += FPSTR(WEB_PAGE_FOOTER_II);
     content.replace("{h}", "Konfiguration wurde gespeichert.");
     content.replace("{n}", "");
     content.replace("{t}", "");

     writeConfig();

     request->send(200, "text/html; charset=utf-8", content);

  }); // server.on

  /*
  server.on("/removeConfig", HTTP_GET, [](AsyncWebServerRequest *request){
     String content = "";
     content += FPSTR(WEB_PAGE_HEADER);
     content += "Konfiguration löschen";
     content += FPSTR(WEB_PAGE_FOOTER);
     content.replace("{h}", "Konfiguration löschen");
     content.replace("{n}", "");
     content.replace("{t}", "");
     request->send(200, "text/html; charset=utf-8", content);
  });
  */
  
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
     String content = "";
     content += FPSTR(WEB_PAGE_HEADER);
     content += FPSTR(WEB_RESET_CONTENT);
     content.replace("{h}", "");
     content.replace("{n}", "");
     content.replace("{t}", "");
     content.replace("{q}", "Wetterstation wirklich neu starten");
     content.replace("{b}", "Neu starten");
     content.replace("{c}", "Abbrechen");
     request->send(200, "text/html; charset=utf-8", content);
  });
  
  server.on("/reset", HTTP_POST, [](AsyncWebServerRequest * request){
     int params = request->params();
     String content = "";

     content += FPSTR(WEB_PAGE_HEADER);

     Serial.println("SERVER: react on submit: " + String(params));

     for(int i = 0; i < params; i++){
       AsyncWebParameter* p = request->getParam(i);
       if(p->isFile()){ //p->isPost() is also true
         Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
       } else if(p->isPost()){
         Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
         delay(1500);
         ESP.restart();
       } else {
         Serial.printf("GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
       }
     } // for(int i = 0; i < params; i++)

     request->send(200, "text/html; charset=utf-8", content);
     
  }); //--- of server.on

    server.onNotFound(notFound);

    server.begin();

  //--- update the weather information
  updateData();
  timerPress = millis();
  canBtnPress = true;
}
//----------------------------------------------------------------
void loop() 
{
  ldr(); // LED display brightness adjustment routine depending on the ambient illuminance
  //Serial.println(LDRReading); // see the measurement result on monitor 

  gfx.fillBuffer(MINI_BLACK);

  if (touchController.isTouched(0)) {
    TS_Point p = touchController.getPoint();

    if (p.y < 80) {
      IS_STYLE_12HR = IS_STYLE_12HR;
    } else {
      screen = (screen + 1) % screenCount;
    }
  } 

  if (screen == 0) 
  {
    drawTime();
    drawWifiQuality();
    int remainingTimeBudget = carousel.update();
    if (remainingTimeBudget > 0) 
    {
      //--- You can do some work here
      //--- Don't do stuff if you are below your time budget.
      delay(remainingTimeBudget);
    }
    drawCurrentWeather();
    drawAstronomy();
  } else if (screen == 1) 
  {
    drawCurrentWeatherDetail();
  } else if (screen == 2) 
  {
    drawForecastTable(0);
  } else if (screen == 3) 
  {
    drawForecastTable(4);
  } else if (screen == 4) 
  {  
    drawAbout();
  } else if (screen == 5) 
  { //indoor data screen added
    drawIndoorData();
  }
  
  gfx.commit();

  // Check if we should update weather information
  if (millis() - lastDownloadUpdate > 1000 * UPDATE_INTERVAL_SECS) 
  {
      updateData();
      lastDownloadUpdate = millis();
  }

  if (SLEEP_INTERVAL_SECS && millis() - timerPress >= SLEEP_INTERVAL_SECS * 1000)
  { // after 2 minutes go to sleep
    drawProgress(25,"Going to Sleep!");
    delay(1000);
    drawProgress(50,"Going to Sleep!");
    delay(1000);
    drawProgress(75,"Going to Sleep!");
    delay(1000);    
    drawProgress(100,"Going to Sleep!");
    // go to deepsleep for xx minutes or 0 = permanently
    ESP.deepSleep(0,  WAKE_RF_DEFAULT);                       // 0 delay = permanently to sleep
  }  
}
//-----------------------------------------------------------------------------------------
void updateData() 
{
  //--- update the internet based information and update screen
  gfx.fillBuffer(MINI_BLACK);
  gfx.setFont(ArialRoundedMTBold_14);

  drawProgress(10, "Aktuallisieren: Zeit...");
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS_1.c_str(), NTP_SERVERS_2.c_str(), NTP_SERVERS_3.c_str());
  while(!time(nullptr)) 
  {
    Serial.print("#");
    delay(100);
  }
  //--- calculate for time calculation how much the dst class adds.
  dstOffset = UTC_OFFSET * 3600 + dstAdjusted.time(nullptr) - time(nullptr);
  Serial.printf("Time difference for DST: %d", dstOffset);

  drawProgress(50, "Aktuallisieren: Wetterdaten...");
  OpenWeatherMapCurrent *currentWeatherClient = new OpenWeatherMapCurrent();
  currentWeatherClient->setMetric(IS_METRIC);
  currentWeatherClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient->updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);
  delete currentWeatherClient;
  currentWeatherClient = nullptr;

  drawProgress(70, "Aktuallisieren: Prognose...");
  OpenWeatherMapForecast *forecastClient = new OpenWeatherMapForecast();
  forecastClient->setMetric(IS_METRIC);
  forecastClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12, 0};
  forecastClient->setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient->updateForecastsById(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);
  delete forecastClient;
  forecastClient = nullptr;

  drawProgress(80, "Aktuallisieren: Astronomie...");
  Astronomy *astronomy = new Astronomy();
  moonData = astronomy->calculateMoonData(time(nullptr));
  float lunarMonth = 29.53;
  moonAge = moonData.phase <= 4 ? lunarMonth * moonData.illumination / 2 : lunarMonth - moonData.illumination * lunarMonth / 2;
  //moonAgeImage = String((char) (65 + ((uint8_t) ((26 * moonAge / 30) % 26))));
  moonAgeImage = String((char) (98 + ((uint8_t) ((26 * moonAge / 30) % 26))));
  delete astronomy;
  astronomy = nullptr;
  delay(1000);
}
//-----------------------------------------------------------------------------------------
void drawProgress(uint8_t percentage, String text) 
{
  //--- Progress bar helper
  gfx.fillBuffer(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(20, 5, ThingPulseLogo);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 90, "https://thingpulse.com");
  gfx.setColor(MINI_YELLOW);

  gfx.drawString(120, 146, text);
  gfx.setColor(MINI_WHITE);
  gfx.drawRect(10, 168, 240 - 20, 15);
  gfx.setColor(MINI_BLUE);
  gfx.fillRect(12, 170, 216 * percentage / 100, 11);

  gfx.commit();
}
//-----------------------------------------------------------------------------------------
void drawTime() 
{
  //--- draws the clock
  char time_str[11];
  char *dstAbbrev;
  time_t now = dstAdjusted.time(&dstAbbrev);
  struct tm * timeinfo = localtime (&now);

  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_WHITE);
  String date = WDAY_NAMES[timeinfo->tm_wday] + " " + MONTH_NAMES[timeinfo->tm_mon] + " " + String(timeinfo->tm_mday) + " " + String(1900 + timeinfo->tm_year);
  gfx.drawString(120, 6, date);

  gfx.setFont(ArialRoundedMTBold_36);

  if (IS_STYLE_12HR) {
    int hour = (timeinfo->tm_hour+11)%12+1;  // take care of noon and midnight
    sprintf(time_str, "%2d:%02d:%02d\n",hour, timeinfo->tm_min, timeinfo->tm_sec);
    gfx.drawString(120, 20, time_str);
  } else {
    sprintf(time_str, "%02d:%02d:%02d\n",timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    gfx.drawString(120, 20, time_str);
  }

  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setFont(ArialMT_Plain_10);
  gfx.setColor(MINI_BLUE);
  if (IS_STYLE_12HR) {
    sprintf(time_str, "%s\n%s", dstAbbrev, timeinfo->tm_hour>=12?"PM":"AM");
    gfx.drawString(195, 27, time_str);
  } else {
    sprintf(time_str, "%s", dstAbbrev);
    gfx.drawString(195, 27, time_str);  // Known bug: Cuts off 4th character of timezone abbreviation
  }
}
//-----------------------------------------------------------------------------------------
void drawCurrentWeather() 
{
  //--- draws current weather information
  gfx.setTransparentColor(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(0, 55, getMeteoconIconFromProgmem(currentWeather.icon));
  
  //--- weather Text
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_BLUE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.drawString(220, 65, DISPLAYED_CITY_NAME);

  gfx.setFont(ArialRoundedMTBold_36);
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
   
  gfx.drawString(220, 78, String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F"));

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_YELLOW);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.drawString(220, 118, currentWeather.description);
}
//-----------------------------------------------------------------------------------------
void drawForecast1(MiniGrafx *display, CarouselState* state, int16_t x, int16_t y) 
{
  drawForecastDetail(x + 10,  y + 165, 0);
  drawForecastDetail(x + 95,  y + 165, 1);
  drawForecastDetail(x + 180, y + 165, 2);
}
//-----------------------------------------------------------------------------------------
void drawForecast2(MiniGrafx *display, CarouselState* state, int16_t x, int16_t y) 
{
  drawForecastDetail(x + 10,  y + 165, 3);
  drawForecastDetail(x + 95,  y + 165, 4);
  drawForecastDetail(x + 180, y + 165, 5);
}
//-----------------------------------------------------------------------------------------
void drawForecast3(MiniGrafx *display, CarouselState* state, int16_t x, int16_t y) 
{
  drawForecastDetail(x + 10,  y + 165, 6);
  drawForecastDetail(x + 95,  y + 165, 7);
  drawForecastDetail(x + 180, y + 165, 8);
}
//-----------------------------------------------------------------------------------------
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) 
{
  // helper for the forecast columns
  gfx.setColor(MINI_YELLOW);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  time_t time = forecasts[dayIndex].observationTime + dstOffset;
  struct tm * timeinfo = localtime (&time);
  gfx.drawString(x + 25, y - 15, WDAY_NAMES[timeinfo->tm_wday] + " " + String(timeinfo->tm_hour) + ":00");

  gfx.setColor(MINI_WHITE);
  gfx.drawString(x + 25, y, String(forecasts[dayIndex].temp, 1) + (IS_METRIC ? "°C" : "°F"));

  gfx.drawPalettedBitmapFromPgm(x, y + 15, getMiniMeteoconIconFromProgmem(forecasts[dayIndex].icon));
  gfx.setColor(MINI_BLUE);
  gfx.drawString(x + 25, y + 60, String(forecasts[dayIndex].rain, 1) + (IS_METRIC ? "mm" : "in"));
}
//-----------------------------------------------------------------------------------------

void drawAstronomy() 
{
  //--- draw moonphase and sunrise/set and moonrise/set
  gfx.setFont(MoonPhases_Regular_36);
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.drawString(120, 275, moonAgeImage);

  gfx.setColor(MINI_WHITE);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(120, 250, MOON_PHASES[moonData.phase]);
  
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(5, 250, SUN_MOON_TEXT[0]);
  gfx.setColor(MINI_WHITE);
  time_t time = currentWeather.sunrise + dstOffset;
  gfx.drawString(5, 276, SUN_MOON_TEXT[1] + ":");
  gfx.drawString(45, 276, getTime(&time));
  time = currentWeather.sunset + dstOffset;
  gfx.drawString(5, 291, SUN_MOON_TEXT[2] + ":");
  gfx.drawString(45, 291, getTime(&time));

  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(235, 250, SUN_MOON_TEXT[3]);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(235, 276, String(moonAge) + "d");
  gfx.drawString(235, 291, String(moonData.illumination * 100, 0) + "%");
  gfx.drawString(200, 276, SUN_MOON_TEXT[4] + ":");
  gfx.drawString(200, 291, SUN_MOON_TEXT[5] + ":");
}
//-----------------------------------------------------------------------------------------
void drawCurrentWeatherDetail() 
{
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 2, "Aktuelle Wetterdaten");

  //gfx.setTransparentColor(MINI_BLACK);
  //gfx.drawPalettedBitmapFromPgm(0, 20, getMeteoconIconFromProgmem(conditions.weatherIcon));

  String degreeSign = "°F";
  if (IS_METRIC) 
  {
    degreeSign = "°C";
  }
  // String weatherIcon;
  // String weatherText;
  drawLabelValue(0, "Temperatur:", currentWeather.temp + degreeSign);
  drawLabelValue(1, "Winsgeschwindigkeit:", String(currentWeather.windSpeed, 1) + (IS_METRIC ? "m/s" : "mph") );
  drawLabelValue(2, "Windrichtung:", String(currentWeather.windDeg, 1) + "°");
  drawLabelValue(3, "Luftfeuchte:", String(currentWeather.humidity) + "%");
  drawLabelValue(4, "Luftdruck:", String(currentWeather.pressure) + "hPa");
  drawLabelValue(5, "Wolken:", String(currentWeather.clouds) + "%");
  drawLabelValue(6, "Sichtweite:", String(currentWeather.visibility) + "m");


  /*gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(15, 185, "Description: ");
  gfx.setColor(MINI_WHITE);
  gfx.drawStringMaxWidth(15, 200, 240 - 2 * 15, forecasts[0].forecastText);*/
}
//-----------------------------------------------------------------------------------------
void drawLabelValue(uint8_t line, String label, String value) {
  const uint8_t labelX = 15;
  const uint8_t valueX = 150;
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(labelX, 30 + line * 15, label);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(valueX, 30 + line * 15, value);
}
//-----------------------------------------------------------------------------------------
// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality() 
{
  int32_t dbm = WiFi.RSSI();
  if(dbm <= -100) {
      return 0;
  } else if(dbm >= -50) 
  {
      return 100;
  } else {
      return 2 * (dbm + 100);
  }
}
//-----------------------------------------------------------------------------------------
void drawWifiQuality() 
{
  int8_t quality = getWifiQuality();
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);  
  gfx.drawString(228, 9, String(quality) + "%");
  for (int8_t i = 0; i < 4; i++) 
  {
    for (int8_t j = 0; j < 2 * (i + 1); j++) 
    {
      if (quality > i * 25 || j == 0) 
      {
        gfx.setPixel(230 + 2 * i, 18 - j);
      }
    }
  }
}
//-----------------------------------------------------------------------------------------
void drawForecastTable(uint8_t start) 
{
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 2, "Prognose");
  uint16_t y = 0;

  String degreeSign = "°F";
  if (IS_METRIC) 
  {
    degreeSign = "°C";
  }
  for (uint8_t i = start; i < start + 4; i++) {
    gfx.setTextAlignment(TEXT_ALIGN_LEFT);
    y = 45 + (i - start) * 75;
    if (y > 320) {
      break;
    }
    gfx.setColor(MINI_WHITE);
    gfx.setTextAlignment(TEXT_ALIGN_CENTER);
    time_t time = forecasts[i].observationTime + dstOffset;
    struct tm * timeinfo = localtime (&time);
    gfx.drawString(120, y - 15, WDAY_NAMES[timeinfo->tm_wday] + " " + String(timeinfo->tm_hour) + ":00");

   
    gfx.drawPalettedBitmapFromPgm(0, 15 + y, getMiniMeteoconIconFromProgmem(forecasts[i].icon));
    gfx.setTextAlignment(TEXT_ALIGN_LEFT);
    gfx.setColor(MINI_YELLOW);
    gfx.setFont(ArialRoundedMTBold_14);
    gfx.drawString(10, y, forecasts[i].main);
    gfx.setTextAlignment(TEXT_ALIGN_LEFT);
    
    gfx.setColor(MINI_BLUE);
    gfx.drawString(50, y, "T:");
    gfx.setColor(MINI_WHITE);
    gfx.drawString(70, y, String(forecasts[i].temp, 0) + degreeSign);
    
    gfx.setColor(MINI_BLUE);
    gfx.drawString(50, y + 15, "H:");
    gfx.setColor(MINI_WHITE);
    gfx.drawString(70, y + 15, String(forecasts[i].humidity) + "%");

    gfx.setColor(MINI_BLUE);
    gfx.drawString(50, y + 30, "P: ");
    gfx.setColor(MINI_WHITE);
    gfx.drawString(70, y + 30, String(forecasts[i].rain, 2) + (IS_METRIC ? "mm" : "in"));

    gfx.setColor(MINI_BLUE);
    gfx.drawString(130, y, "Pr:");
    gfx.setColor(MINI_WHITE);
    gfx.drawString(170, y, String(forecasts[i].pressure, 0) + "hPa");
    
    gfx.setColor(MINI_BLUE);
    gfx.drawString(130, y + 15, "WSp:");
    gfx.setColor(MINI_WHITE);
    gfx.drawString(170, y + 15, String(forecasts[i].windSpeed, 0) + (IS_METRIC ? "m/s" : "mph") );

    gfx.setColor(MINI_BLUE);
    gfx.drawString(130, y + 30, "WDi: ");
    gfx.setColor(MINI_WHITE);
    gfx.drawString(170, y + 30, String(forecasts[i].windDeg, 0) + "°");

  }
}
//-----------------------------------------------------------------------------------------
void drawAbout() 
{
  int xPos = 6;
  gfx.fillBuffer(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(20, 5, ThingPulseLogo);
  
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 90, "https://thingpulse.com");
  
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  
  drawLabelValue(xPos++, "Heap Mem:", String(ESP.getFreeHeap() / 1024)+"kb");
  drawLabelValue(xPos++, "Flash Mem:", String(ESP.getFlashChipRealSize() / 1024 / 1024) + "MB");
  drawLabelValue(xPos++, "WiFi Strength:", String(WiFi.RSSI()) + "dB");
  drawLabelValue(xPos++, "WiFi IP:", WiFi.localIP().toString() );
  drawLabelValue(xPos++, "Chip ID:", String(ESP.getChipId()));
  //drawLabelValue(xPos++, "VCC: ", String(ESP.getVcc() / 1024.0) +"V");
  drawLabelValue(xPos++, "LDR value: ", String(LDRReading)); //supply voltage value indication changed to LDR measured value at A0 pin
  drawLabelValue(xPos++, "CPU Freq.: ", String(ESP.getCpuFreqMHz()) + "MHz");
  char time_str[15];
  const uint32_t millis_in_day = 1000 * 60 * 60 * 24;
  const uint32_t millis_in_hour = 1000 * 60 * 60;
  const uint32_t millis_in_minute = 1000 * 60;
  uint8_t days = millis() / (millis_in_day);
  uint8_t hours = (millis() - (days * millis_in_day)) / millis_in_hour;
  uint8_t minutes = (millis() - (days * millis_in_day) - (hours * millis_in_hour)) / millis_in_minute;
  sprintf(time_str, "%2dd%2dh%2dm", days, hours, minutes);
  drawLabelValue(xPos++, "Uptime: ", time_str);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(++xPos, 250, "Last Reset: ");
  gfx.setColor(MINI_WHITE);
  gfx.drawStringMaxWidth(15, 265, 240 - 2 * 15, ESP.getResetInfo());
}
//-----------------------------------------------------------------------------------------
void calibrationCallback(int16_t x, int16_t y) 
{
  gfx.setColor(1);
  gfx.fillCircle(x, y, 10);
}
//-----------------------------------------------------------------------------------------
String getTime(time_t *timestamp) 
{
  struct tm *timeInfo = gmtime(timestamp);
  char buf[6];
  sprintf(buf, "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
  return String(buf);
}
//-----------------------------------------------------------------------------------------
// additional screen draws current indoor conditions
void drawIndoorData() 
{
  humidity = bme.readHumidity();
  temperature = bme.readTemperature()-2.0;//here you may add or deduct the temperature calibration factor, it is 2.0 in my case 
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 2, "Zimmer Konditionen");
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.drawString(0, 24, "Temperatur");
  gfx.setFont(ArialRoundedMTBold_36);
  gfx.setColor(MINI_YELLOW);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.drawString(120, 46, String(temperature) + "°C"); //value in Celsius degrees
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(0, 99, "Luftfeuchte");
  gfx.setFont(ArialRoundedMTBold_36);
  gfx.setColor(MINI_BLUE);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.drawString(120, 121, String(humidity) + "%");
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(0, 174, "Luftdruck");
  gfx.setFont(ArialRoundedMTBold_36);
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.drawString(120, 196, String(bme.readPressure()*0.007501) + " mm");//remove the factor if value in mbars is needed
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(0, 249, "Höhe über NN");
  gfx.setFont(ArialRoundedMTBold_36);
  gfx.setColor(MINI_YELLOW);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.drawString(120, 271, String(bme.readAltitude(1013.25)) + " m"); //change the factor in brackets depending on sea level elevation of your place, normally available in Internet
}
//-----------------------------------------------------------------------------------------
void ldr() 
{
    LDRReading = analogRead(LDR_PIN);
    
    if (LDRReading < 100) {
    analogWrite(TFT_LED, 100); // the most dark screen
    
  } else if (LDRReading < 250) 
  {
    analogWrite(TFT_LED, 250);
    
  } else if (LDRReading < 500) 
  {
    analogWrite(TFT_LED, 500);

  } else if (LDRReading < 750) 
  {
    analogWrite(TFT_LED, 700);

  } else if (LDRReading < 950) 
  {
    analogWrite(TFT_LED, 850);

  } else {
    analogWrite(TFT_LED, 1000); // the most bright screen
  }
}
//-----------------------------------------------------------------------------------------
void configModeCallback (AsyncWiFiManager *myWiFiManager) 
{
  Serial.println("WiFiManager: Entered config mode");
  drawProgress(50, "WiFiManager: Configuration...");
  Serial.println(WiFi.softAPIP());
  //--- if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
}
//-----------------------------------------------------------------------------------------
void notFound(AsyncWebServerRequest *request) 
{
    request->send(404, "text/plain", "Not found");
}
//-----------------------------------------------------------------------------------------

void readConfig() 
{
    //--- read config from spiffs                                       *
    String json_string = "";
  
    gfx.fillBuffer(MINI_BLACK);
    gfx.setFont(ArialRoundedMTBold_14);

    drawProgress(10, "Configuration: opening...");
    delay(1000);
    
    if (SPIFFS.exists("/config.json")) 
    {
      //file exists, reading and loading
      File configFile = SPIFFS.open("/config.json", "r");
      drawProgress(30, "Configuration: reading JSON...");
      delay(1000);
      if (configFile) {
        size_t size = configFile.size();
        
        //--- allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        DynamicJsonDocument jsonDoc(1000);
        
        auto jsonError = deserializeJson(jsonDoc, buf.get());
        
        if(!jsonError) {
          drawProgress(60, "Configuration: filling vars...");
          delay(1000);
          JsonObject json = jsonDoc.as<JsonObject>();
        
          #define setFromJSON(key) if (json.containsKey(#key)) key = json[#key];
          #define strcpyFromJSON(key) if (json.containsKey(#key)) strcpy(key, json[#key]);
          #define StringFromJSON(key) if (json.containsKey(#key)) key = json[#key].as<String>();

          StringFromJSON(OPEN_WEATHER_MAP_APP_ID);
          StringFromJSON(OPEN_WEATHER_MAP_LOCATION_ID);
          StringFromJSON(DISPLAYED_CITY_NAME);
          StringFromJSON(OPEN_WEATHER_MAP_LANGUAGE);
          setFromJSON(FRAME_COUNT);
          setFromJSON(IS_METRIC);
          setFromJSON(IS_STYLE_12HR);
          StringFromJSON(NTP_SERVERS_1);
          StringFromJSON(NTP_SERVERS_2);
          StringFromJSON(NTP_SERVERS_3);
          setFromJSON(UPDATE_INTERVAL_SECS);
          
          #undef setFromJSON
          #undef strcpyFromJSON
          #undef StringFromJSON
        } else 
        {
          Serial.println("JSON: error while parsing");
        }
      }
    } else 
    {
      Serial.println("JSON: error while reading");
    }

    drawProgress(60, "Configuration: done...");
    delay(1000);
}
//-----------------------------------------------------------------------------------------
//--- write config to spiffs                                        *
void writeConfig() 
{
  String json_string = "";

  json_string.reserve(1000);

  json_string = "{";
  
  #define copyToJSON_Bool(varname) json_string +="\""+String(#varname)+"\":"+(varname ? "1":"0")+",";
  #define copyToJSON_Int(varname) json_string +="\""+String(#varname)+"\":"+String(varname)+",";
  #define copyToJSON_String(varname) json_string +="\""+String(#varname)+"\":\""+String(varname)+"\",";

  copyToJSON_String(OPEN_WEATHER_MAP_APP_ID);
  copyToJSON_String(OPEN_WEATHER_MAP_LOCATION_ID);
  copyToJSON_String(DISPLAYED_CITY_NAME);
  copyToJSON_String(OPEN_WEATHER_MAP_LANGUAGE);
  copyToJSON_Int(FRAME_COUNT);
  copyToJSON_Bool(IS_METRIC);
  copyToJSON_Bool(IS_STYLE_12HR);
  copyToJSON_String(NTP_SERVERS_1);
  copyToJSON_String(NTP_SERVERS_2);
  copyToJSON_String(NTP_SERVERS_3);
  copyToJSON_Int(UPDATE_INTERVAL_SECS);

  #undef copyToJSON_Bool
  #undef copyToJSON_Int
  #undef copyToJSON_String

  json_string.remove(json_string.length() - 1);
  json_string += "}";

  File configFile = SPIFFS.open("/config.json", "w");

  if (configFile) {
    configFile.print(json_string);
    configFile.close();
    config_needs_write = false;
//    flashwrites ++; 
    Serial.println("JSON: configuaration saved");

  } else 
  {
    Serial.println("JSON: error while saving");
  }
}   //--- end save
//-----------------------------------------------------------------------------------------
String form_input(const String& name, const String& info, const String& value, const int length) 
{
  String s = F("<tr><td>{i} </td><td><input type='text' name='{n}' id='{n}' placeholder='{i}' value='{v}' maxlength='{l}'/></td></tr>");
  s.replace("{i}",info);
  s.replace("{n}",name);
  s.replace("{v}",value);
  s.replace("{l}",String(length));
  return s;
}
//-----------------------------------------------------------------------------------------
String form_checkbox(const String& name, const String& info, const bool checked) 
{
  String s = "";
//  s = F("<label for='{n}'><input type='checkbox' name='{n}' value='1' id='{n}' {c}/><input type='hidden' name='{n}' value='0' /> {i}</label><br/>");
  s = F("<label for='{n}'><input type='checkbox' name='{n}' value='1' id='{n}' {c}/> {i}</label><br/>");
  s.replace("{i}", info);
  if (checked) {
    s.replace("{c}", F(" checked='checked'"));
  } else {
    s.replace("{c}", "");
  };
  s.replace("{n}", name);
  return s;
}
//-----------------------------------------------------------------------------------------
String line_from_value(const String& name, const String& value) 
{
  String s = F("<br>{n}: {v}");
  s.replace("{n}", name);
  s.replace("{v}", value);
  return s;
}
//-----------------------------------------------------------------------------------------
String form_select_frame() 
{
  String s_select = F("selected='selected'");
  String s = F("<tr><td>{t}</td><td><select name='FRAME_COUNT'><option value='1' {s_1}>one Frame</option><option value='2' {s_2}>two Frames</option><option value='3' {s_3}>three Frames</option></select></td></tr>");

  s.replace("{t}", "Number of Frames:");
  if(String(FRAME_COUNT) == "1") {
    s.replace(F("{s_1}"), s_select);
  } else if(String(FRAME_COUNT) == "2") {
    s.replace(F("{s_2}"), s_select);
  } else if(String(FRAME_COUNT) == "3") {
    s.replace(F("{s_3}"), s_select);
  }
  s.replace(F("{s_1}"), "");
  s.replace(F("{s_2}"), "");
  s.replace(F("{s_3}"), "");
  return s;
}
//-----------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------
