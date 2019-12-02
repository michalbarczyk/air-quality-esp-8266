#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
//#include <OneWire.h>
//#include <DallasTemperature.h>
#include <FastLED.h>

//------------------------------------------
//DS18B20
#define ONE_WIRE_BUS D3 //Pin to which is attached a temperature sensor
#define ONE_WIRE_MAX_DEV 15 //The maximum number of devices

#define LED_PIN 5
#define NUM_LEDS 10

CRGB leds[NUM_LEDS];

#include "DHT.h"        // including the library of DHT11 temperature and humidity sensor
#define DHTTYPE DHT11   // DHT 11

#define dht_dpin D4
DHT dht(dht_dpin, DHTTYPE); 

//------------------------------------------
//WIFI
const char* ssid = "Android_MB";
const char* password = "1234567890";

//------------------------------------------
//HTTP
ESP8266WebServer server(80);

#define  MEAN_NUMBER  10
#define  MAX_PM   0
#define  MIN_PM   32767

int incomingByte = 0; // for incoming serial data
const int MAX_FRAME_LEN = 64;
char frameBuf[MAX_FRAME_LEN];
int detectOff = 0;
int frameLen = MAX_FRAME_LEN;
bool inFrame = false;
char printbuf[256];
char webFrameBuf[64];
unsigned int calcChecksum = 0;
unsigned int pm1_0=0, pm2_5=0, pm10_0=0;
unsigned int tmp_max_pm1_0, tmp_max_pm2_5, tmp_max_pm10_0; 
unsigned int tmp_min_pm1_0, tmp_min_pm2_5, tmp_min_pm10_0; 
byte i=0;



struct PMS7003_framestruct {
    byte  frameHeader[2];
    unsigned int  frameLen = MAX_FRAME_LEN;
    unsigned int  concPM1_0_CF1;
    unsigned int  concPM2_5_CF1;
    unsigned int  concPM10_0_CF1;
    unsigned int  checksum;
} thisFrame;

/************/
char airReport[256];
int pm01value = 0;
int pm02_5value = 0;
int pm10value = 0;
float temp = 0.0;
float humid = 0.0;


void HandleRoot(){
  String message = "";
  
  Serial.println("\nSending air params.");

  message += "<table border='1'>\r\n";
  message += "<tr><td>Particle</td><td>Pollution level</td></tr>\r\n";
  sprintf(webFrameBuf, "%d", pm01value);
  message += "<tr><td>";
  message += "PM 1.0";
  message += "</td>";
  message += "<td>";
  message += webFrameBuf;
  message += "</td></tr>";
  sprintf(webFrameBuf, "%d", pm02_5value);
  message += "<tr><td>";
  message += "PM 2.5";
  message += "</td>";
  message += "<td>";
  message += webFrameBuf;
  message += "</td></tr>";
  sprintf(webFrameBuf, "%d", pm10value);
  message += "<tr><td>";
  message += "PM 10.0";
  message += "</td>";
  message += "<td>";
  message += webFrameBuf;
  message += "</td></tr>";
  message += "\r\n";
  message += "</table>\r\n";

  message += "<br/>";

  message += "<table border='1'>\r\n";
  message += "<tr><td>Temperature</td>";
  sprintf(webFrameBuf, "%.1f C", temp);
  message += "<td>";
  message += webFrameBuf;
  message += "</td></tr>";
  message += "<tr><td>Humidity</td>";
  sprintf(webFrameBuf, "%.1f %%", humid);
  message += "<td>";
  message += webFrameBuf;
  message += "</td></tr>";
  message += "\r\n";
  message += "</table>\r\n";
  
  server.send(200, "text/html", message );
}

void HandleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/html", message);
}

void setup() {

  sprintf(airReport, "INFO: Measurement in progress");
    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
    
    Serial.begin(9600);
    while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
    }
    
    //setup DHT
    dht.begin();

    Serial.println("\n Serial communication initialized \n");

      //Setup WIFI
    WiFi.begin(ssid, password);
    Serial.println("");
  
    //Wait for WIFI connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  
    server.on("/", HandleRoot);
    server.onNotFound( HandleNotFound );
    server.begin();
    Serial.println("HTTP server started at ip " + WiFi.localIP().toString() );
    
}

bool pms7003_read() {
    bool packetReceived = false;
    calcChecksum = 0;
    while (!packetReceived) {
        if (Serial.available() > 32) {
            int drain = Serial.available();

            for (int i = drain; i > 0; i--) {
                Serial.read();
            }
        }
        if (Serial.available() > 0) {

            incomingByte = Serial.read();

            if (!inFrame) {
                if (incomingByte == 0x42 && detectOff == 0) {
                    frameBuf[detectOff] = incomingByte;
                    thisFrame.frameHeader[0] = incomingByte;
                    calcChecksum = incomingByte; // Checksum init!
                    detectOff++;
                }
                else if (incomingByte == 0x4D && detectOff == 1) {
                    frameBuf[detectOff] = incomingByte;
                    thisFrame.frameHeader[1] = incomingByte;
                    calcChecksum += incomingByte;
                    inFrame = true;
                    detectOff++;
                }
                else {
                    Serial.print("----- Frame syncing... -----");
                    Serial.print(incomingByte, HEX);
                    Serial.println();
                }
            }
            else {
                frameBuf[detectOff] = incomingByte;
                calcChecksum += incomingByte;
                detectOff++;
                unsigned int  val = (frameBuf[detectOff-1]&0xff)+(frameBuf[detectOff-2]<<8);
                switch (detectOff) {
                    case 4:
                        thisFrame.frameLen = val;
                        frameLen = val + detectOff;
                        break;
                    case 6:
                        thisFrame.concPM1_0_CF1 = val;
                        break;
                    case 8:
                        thisFrame.concPM2_5_CF1 = val;
                        break;
                    case 10:
                        thisFrame.concPM10_0_CF1 = val;
                        break;
                    case 32:
                        thisFrame.checksum = val;
                        calcChecksum -= ((val>>8)+(val&0xFF));
                        break;
                    default:
                        break;
                }
                if (detectOff >= frameLen) {
       
                    packetReceived = true;
                    detectOff = 0;
                    inFrame = false;
                }
            }
        } 
    }
    return (calcChecksum == thisFrame.checksum);
}

void prepareAirMeasurement() {
  if(i==0) { 
    tmp_max_pm1_0  = MAX_PM;
    tmp_max_pm2_5  = MAX_PM;
    tmp_max_pm10_0 = MAX_PM;
    tmp_min_pm1_0  = MIN_PM;
    tmp_min_pm2_5  = MIN_PM;
    tmp_min_pm10_0 = MIN_PM;
  }
  if (pms7003_read()) {
    tmp_max_pm1_0  = max(thisFrame.concPM1_0_CF1, tmp_max_pm1_0);
    tmp_max_pm2_5  = max(thisFrame.concPM2_5_CF1, tmp_max_pm2_5);
    tmp_max_pm10_0 = max(thisFrame.concPM10_0_CF1, tmp_max_pm10_0);
    tmp_min_pm1_0  = min(thisFrame.concPM1_0_CF1, tmp_min_pm1_0);
    tmp_min_pm2_5  = min(thisFrame.concPM2_5_CF1, tmp_min_pm2_5);
    tmp_min_pm10_0 = min(thisFrame.concPM10_0_CF1, tmp_min_pm10_0);
    pm1_0 += thisFrame.concPM1_0_CF1;
    pm2_5 += thisFrame.concPM2_5_CF1;
    pm10_0 += thisFrame.concPM10_0_CF1;
    i++;
    Serial.print("O");
  }
  else {
    Serial.print("*");
  }
  
  if(i==MEAN_NUMBER) {

    pm01value = (pm1_0-tmp_max_pm1_0-tmp_min_pm1_0)/(MEAN_NUMBER-2);
    pm02_5value = (pm2_5-tmp_max_pm2_5-tmp_min_pm2_5)/(MEAN_NUMBER-2);
    pm10value = (pm10_0-tmp_max_pm10_0-tmp_min_pm10_0)/(MEAN_NUMBER-2);
  sprintf(printbuf, "");
  sprintf(printbuf, "%s PM1.0 = %02d, PM2.5 = %02d, PM10 = %02d", 
          printbuf, pm01value, pm02_5value, pm10value);
  sprintf(printbuf, "%s [max = %02d,%02d,%02d, min = %02d,%02d,%02d]", printbuf,
    tmp_max_pm1_0, tmp_max_pm2_5, tmp_max_pm10_0,
    tmp_min_pm1_0, tmp_min_pm2_5, tmp_min_pm10_0);  
  Serial.println();
  Serial.println(printbuf);
  sprintf(airReport, "%s", printbuf);
  pm1_0=pm2_5=pm10_0=i=0;
  delay(1000);
  }
}

#define BLUE_PM10 10 //color lvls used currently for all 3 of measurements
#define GREEN_PM10 30
#define YELLOW_PM10 50
#define ORANGE_PM10 70
#define RED_PM10 90
void updatePM10Led() {
    if (pm10value < BLUE_PM10) {
      leds[0] = CRGB(0, 0, 133); 
    } else if (pm10value < GREEN_PM10) {
      leds[0] = CRGB(0, 133, 0);
    } else if (pm10value < YELLOW_PM10) {
      leds[0] = CRGB(133, 133, 0);
    } else if (pm10value < ORANGE_PM10) {
      leds[0] = CRGB(133, 33, 0);
    } else if (pm10value < RED_PM10) {
      leds[0] = CRGB(133, 0, 0);
    } else {
      leds[0] = CRGB(133, 133, 133);
    }
}

void updatePM02_5Led() {
    if (pm02_5value < BLUE_PM10) {
      leds[1] = CRGB(0, 0, 133); 
    } else if (pm02_5value < GREEN_PM10) {
      leds[1] = CRGB(0, 133, 0);
    } else if (pm02_5value < YELLOW_PM10) {
      leds[1] = CRGB(133, 133, 0);
    } else if (pm02_5value < ORANGE_PM10) {
      leds[1] = CRGB(133, 33, 0);
    } else if (pm02_5value < RED_PM10) {
      leds[1] = CRGB(133, 0, 0);
    } else {
      leds[1] = CRGB(133, 133, 133);
    }
}

void updatePM01Led() {
    if (pm01value < BLUE_PM10) {
      leds[2] = CRGB(0, 0, 133); 
    } else if (pm01value < GREEN_PM10) {
      leds[2] = CRGB(0, 133, 0);
    } else if (pm01value < YELLOW_PM10) {
      leds[2] = CRGB(133, 133, 0);
    } else if (pm01value < ORANGE_PM10) {
      leds[2] = CRGB(133, 33, 0);
    } else if (pm01value < RED_PM10) {
      leds[2] = CRGB(133, 0, 0);
    } else {
      leds[2] = CRGB(133, 133, 133);
    }
}

void updateLeds() {
    updatePM10Led();
    updatePM02_5Led();
    updatePM01Led();
    FastLED.show(); 
}

void measureTH() {
    humid = dht.readHumidity();
    temp = dht.readTemperature();  
}

void loop () {
  
   server.handleClient();
   prepareAirMeasurement();
   measureTH();
   updateLeds();
   
}
