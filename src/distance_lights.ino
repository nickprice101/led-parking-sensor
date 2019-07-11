    
/*---------------------------------------------------------------------------------------------
  Open Sound Control (OSC) library for the ESP8266/ESP32

  This version has been modified as part of an led parking sensor project by N Price (June 2019).
--------------------------------------------------------------------------------------------- */
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>

#include <Adafruit_NeoPixel.h>

char ssid[] = "**your_wifi_ssid**";          // your network SSID (name)
char pass[] = "**your_wifi_password**";               // your network password

/**************************** FOR OTA **************************************************/
#define SENSORNAME "parking-garage-lights"
#define OTApassword "**your_ota_password**" // change this to whatever password you want to use when you upload OTA
int OTAport = 8266;
int calibrationTime = 0;

// A UDP instance to let us send and receive packets over UDP
WiFiUDP Udp;
//const IPAddress outIp(192,168,xxx,xxx);     // remote IP (not needed for receive)
//const unsigned int outPort = 9999;          // remote port (not needed for receive)
const unsigned int localPort = 8888;        // local port to listen for UDP packets (here's where we send the packets)

const uint16_t PixelCount = 128; // number of pixels
#define PIN D4 // Pin where NeoPixels are connected

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(PixelCount, PIN, NEO_GRB + NEO_KHZ800);

//uint8_t rgb_values[3];

OSCErrorCode error;
unsigned int carDistance = 0;  
unsigned int oldCarDistance = 0;

int blockSize = 4;
double maxDistance = 200;
double minDistance = 30;
double warningDistance = 20;
double adjustedCarDistance = 0;
int flashDelay = 50; //time in ms
boolean lights_on = false;

int startRed = 0;
int startGreen = 255;
int startBlue = 0;

int endRed = 255;
int endGreen = 0;
int endBlue = 0;

int adjustedPixelCount = int((PixelCount/2) - blockSize);

void setup() {
  Serial.begin(115200);

  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname(SENSORNAME);
  ArduinoOTA.setPassword((const char *)OTApassword);

  Serial.print("calibrating sensor ");
  for (int i = 0; i < calibrationTime; i++) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("Starting Node named " + String(SENSORNAME));

  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.onStart([]() {
    Serial.println("Starting");
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

  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());

  strip.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();  // Turn OFF all pixels ASAP

  Serial.print("Number of pixels: ");
  Serial.println(strip.numPixels());
}

void distance(OSCMessage &msg) {
  carDistance = msg.getInt(0);
  Serial.print("/distance: ");
  Serial.println(carDistance);
}

void update_lights() {
              if ((carDistance < warningDistance) && (lights_on)) { //flash lights, don’t care if distance has changed
                                         delay(flashDelay);
                                         strip.clear(); // reset RAM values
                                         strip.show(); // turn off the lights
                                         delay(flashDelay);
                                         lights_on = false;
              }
              if (carDistance != oldCarDistance || (!lights_on)) { // if the distance has changed or lights are not on
                           double distanceFraction;
                           int startPixel;
                           strip.clear(); //reset RAM values
                           //turn on pixels based on distance
                           distanceFraction = min(max(1 - ((carDistance - minDistance)/(maxDistance - minDistance)),0.0),1.0);
                           startPixel = int(adjustedPixelCount * distanceFraction);
                           for (int pixel = startPixel; pixel < startPixel + blockSize; pixel++) {
                                         strip.setPixelColor(pixel, strip.Color(int(startRed - ((startRed-endRed) * distanceFraction)),int(startGreen - ((startGreen-endGreen) * distanceFraction)),int(startBlue - ((startBlue-endBlue) * distanceFraction))));
                                         strip.setPixelColor(PixelCount - pixel - 1, strip.Color(int(startRed - ((startRed-endRed) * distanceFraction)),int(startGreen - ((startGreen-endGreen) * distanceFraction)),int(startBlue - ((startBlue-endBlue) * distanceFraction))));
                           }
                           oldCarDistance = carDistance;
                           lights_on = true;
                           strip.show();
              }
}

void loop() {
  ArduinoOTA.handle();
  
  OSCMessage msg; // create an empty message for the incoming data
  int size = Udp.parsePacket();

  if (size > 0) {
    while (size--) {
      msg.fill(Udp.read());
    }
    if (!msg.hasError()) {
      msg.dispatch("/distance", distance);
    } else {
      error = msg.getError();
      Serial.print("error: ");
      Serial.println(error);
    }
  }
  if ((carDistance < 0) { // if carDistance is negative then the sensor is off
    if (carDistance != oldCarDistance)) { // only run once in off state
      strip.clear(); // reset RAM values
      strip.show(); // turn off the lights
      oldCarDistance = carDistance;
    }
  } else { // else update lights
      update_lights();
  }
}

/****reset***/
void software_Reset() // Restarts program from beginning but does not reset the peripherals and registers
{
  Serial.print("resetting");
  ESP.reset(); 
}
