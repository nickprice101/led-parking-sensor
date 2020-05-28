    
// VERSION: v3
/*---------------------------------------------------------------------------------------------
  Open Sound Control (OSC) library for the ESP8266/ESP32
  Example for receiving open sound control (OSC) messages on the ESP8266/ESP32
--------------------------------------------------------------------------------------------- */
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <ESP8266WebServer.h>

#include <Adafruit_NeoPixel.h>

char wifi_ssid[] = "**your_wifi_ssid**";            // your network SSID (name)
char wifi_password[] = "**your_wifi_password**";  // your network password

/**************************** FOR OTA **************************************************/
#define SENSORNAME "parking-garage-sensor"
#define OTApassword "**your_ota_password**" // change this to whatever password you want to use when you upload OTA
int OTAport = 8266;
int calibrationTime = 0;

// A UDP instance to let us send and receive packets over UDP
WiFiUDP Udp;
//const IPAddress outIp(192,168,0,0);     // remote IP (not needed for receive)
//const unsigned int outPort = 9999;          // remote port (not needed for receive)
const unsigned int localPort = 8888;        // local port to listen for UDP packets (here's where we send the packets)

const uint16_t PixelCount = 112; // number of pixels
#define PIN D4 // Pin where NeoPixels are connected

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(PixelCount, PIN, NEO_GRB + NEO_KHZ800);

//uint8_t rgb_values[3];

OSCErrorCode error;
unsigned int carDistance = 0;  
unsigned int oldCarDistance = 0;
unsigned int timeout = 10; // time of no distance change before lights off (seconds)

int blockSize = 14;
double maxDistance = 250;
double minDistance = 45;
double warningDistance = 35;
double adjustedCarDistance = 0;
int flashDelay = 50; //time in ms
int lastUpdate = 0; //last time the lights were changed.
boolean lights_on = false;
boolean manualOverride = false;

double distanceFraction;
int startPixel;

int startRed = 0;
int startGreen = 255;
int startBlue = 0;

int endRed = 255;
int endGreen = 0;
int endBlue = 0;

int adjustedPixelCount = int((PixelCount/2) - blockSize);

MDNSResponder mdns;
ESP8266WebServer server(80);
String webSite="";
String webSiteComplete="";

void setup() {
  webSite +="<html><head><title>Parking Sensor</title><style>body{ background-color: #ffffff; font-family: Arial, Helvetica, Sans-Serif; Color: #111111; }</style>";
  webSite +="<script>setTimeout(function(){ window.location.href = './'; }, 5000);</script></head>"; //reset/reload the URL after 5 seconds.
  webSite +="<body><table width=50% align=center>";
  webSite +="<tr><td width=40% rowspan=4><img src='http://<location of a nice picture>/images/parking.png' width=100%></td><td colspan=2 align=center><h2>Parking Sensor</h2></td></tr>";
  webSite +="<tr><td align=left>Home</td><td align=center><a href='./'><button>HOME</button></a></td>";
  webSite +="<tr><td align=left>LED Lights</td><td align=center><a href='lightsOn'><button>ON</button></a>&nbsp;<a href='lightsOff'><button>OFF</button></a></td></tr>";
  webSite +="<tr><td align=left>Object Distance</td><td align=center>";
  
  Serial.begin(115200);
  //Wait for the serial connection
  delay(100);

  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname(SENSORNAME);
  ArduinoOTA.setPassword((const char *)OTApassword);

  Serial.println("Starting Node named " + String(SENSORNAME));

  // Connect to WiFi network
  setup_wifi();

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

 if (mdns.begin("esp8266", WiFi.localIP())) {
   Serial.println("MDNS responder started");
  }
    server.on("/", [](){
    server.send(200, "text/html", webSiteComplete);
    });
  server.on("/lightsOn", [](){
    server.send(200, "text/html", webSiteComplete);
    all_lights_on(); //turn on all lights
  });
  server.on("/lightsOff", [](){
    server.send(200, "text/html", webSiteComplete);
    all_lights_off(); //turn off all lights
  });
    
  server.begin();
  Serial.println("HTTP server started");
  
}

void distance(OSCMessage &msg) {
  carDistance = msg.getInt(0);
  Serial.print("/distance: ");
  Serial.println(carDistance);
}

/********************************** START SETUP WIFI*****************************************/
void setup_wifi() {

  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
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
                           //double distanceFraction;
                           //int startPixel;
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

void all_lights_on() {
  manualOverride=true;
  strip.clear(); //reset RAM values
  for (int pixel = 1; pixel <= PixelCount; pixel++) {
    strip.setPixelColor(pixel, strip.Color(255,255,255));
  }
  strip.show();
}

void all_lights_off() {
  manualOverride = false;
  strip.clear(); // reset RAM values
  strip.show(); // turn off the lights
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  
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
  if (!manualOverride) { //if no manual override
    if ((carDistance < 0) || ((millis() - lastUpdate) > (timeout * 1000)) && (oldCarDistance == carDistance)) { // if carDistance is negative (then the sensor is off) or timeout has been reached and distance hasn't changed 
        all_lights_off();
    } else if ((carDistance != oldCarDistance) || (carDistance < warningDistance)) { // else update lights if the distance reported has changed or we need to flash
        update_lights();
        if (carDistance >= warningDistance) lastUpdate = millis(); //only update if warning distance has not been reached.
    }
  }
  webSiteComplete = webSite + String(carDistance) + " cm</td></tr></table></body></html>";
}
/****reset***/
void software_Reset() // Restarts program from beginning but does not reset the peripherals and registers
{
  Serial.print("resetting");
  ESP.reset(); 
}
