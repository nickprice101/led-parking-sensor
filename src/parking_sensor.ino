/*
  This script is designed to take several readings from the MaxBotix sonar and generate a mode/median.
  Author: Jason Lessels
  Date created: 2011/June/06
  License: GPL (=>2)
  This work has been compiled using many sources mainly posts/wiki posts from;
  Allen, Bruce (2009/July/23) and Gentles, Bill (2010/Nov/12)
*/
#include <Maxbotix.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>

/************ WIFI and MQTT INFORMATION (CHANGE THESE FOR YOUR SETUP) ******************/
#define wifi_ssid "**your_wifi_ssid**" //type your WIFI information inside the quotes
#define wifi_password "**your_wifi_password**"
#define mqtt_server "**mqtt_server_ip**"
#define mqtt_user "**mqtt_username**"
#define mqtt_password "**mqtt_password**"
#define mqtt_port 1883 //mqtt port

/************* MQTT TOPICS (change these topics as you wish)  **************************/
#define parking_state_topic "parking/garage"
#define parking_set_topic "parking/garage/set"

const char* on_cmd = "ON";
const char* off_cmd = "OFF";

/**************************** FOR OTA **************************************************/
#define SENSORNAME "parking-garage-sensor"
#define OTApassword "**your_ota_password**" // change this to whatever password you want to use when you upload OTA
int OTAport = 8266;

//Set the pin to receive the signal.
const int pwPin = 4;
//variables needed to store values
int arraysize = 9;  //quantity of values to find the median (sample size). Needs to be an odd number

//declare an array to store the samples. not necessary to zero the array values here, it just makes the code clearer
int rangevalue[] = {  0, 0, 0, 0, 0, 0, 0, 0, 0};

char message_buff[100];
int calibrationTime = 0;
const int BUFFER_SIZE = 300;
#define MQTT_MAX_PACKET_SIZE 512

bool stateOn = true;
long pulse;
//int modE;
int distance = 0;
int old_distance = 0;

WiFiClient espClient;
PubSubClient client(espClient);
//setup OSC UDP connection variables
WiFiUDP Udp;                                // A UDP instance to let us send and receive packets over UDP
const IPAddress outIp(192,168,xxx,xxx);     // remote IP of the lights
const unsigned int outPort = 8888;          // remote port to receive OSC
const unsigned int localPort = 9999;        // local port to listen for OSC packets (actually not used for sending)

void setup()
{
  //Open up a serial connection
  Serial.begin(115200);
  //Wait for the serial connection
  delay(100);
 
  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname(SENSORNAME);
  ArduinoOTA.setPassword((const char *)OTApassword);

  Serial.print("calibrating sensor ");
  for (int i = 0; i < calibrationTime; i++) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("Starting Node named " + String(SENSORNAME));
  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback); //received messages

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
  Serial.println("Ready");
  Serial.print("IPess: ");
  Serial.println(WiFi.localIP());
  reconnect();

  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());
}

/*-----------Functions------------*/
//Function to print the arrays.
void printArray(int *a, int n)
{
  for (int i = 0; i < n; i++)
  {
    Serial.print(a[i], DEC);
    Serial.print(' ');
  }
  Serial.println();
}

//Sorting function
// sort function (Author: Bill Gentles, Nov. 12, 2010)
void isort(int *a, int n) {
  //  *a is an array pointer function
  for (int i = 1; i < n; ++i)
  {
    int j = a[i];
    int k;
    for (k = i - 1; (k >= 0) && (j < a[k]); k--)
    {
      a[k + 1] = a[k];
    }
    a[k + 1] = j;
  }
}

//Mode function, returning the mode or median.
int mode(int *x, int n) {
  int i = 0;
  int count = 0;
  int maxCount = 0;
  int mode = 0;
  int bimodal;
  int prevCount = 0;

  while (i < (n - 1)) {
    prevCount = count;
    count = 0;
    
    while (x[i] == x[i + 1]) {
      count++;
      i++;
    }

    if (count > prevCount & count > maxCount) {
      mode = x[i];
      maxCount = count;
      bimodal = 0;
    }
    if (count == 0) {
      i++;
    }

    if (count == maxCount) { //If the dataset has 2 or more modes.
      bimodal = 1;
    }

    if (mode == 0 || bimodal == 1) { //Return the median if there is no mode.
      mode = x[(n / 2)];
    }
    return mode;
  }
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

/********************************** START CALLBACK*****************************************/
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  if (!processJson(message)) {
    return;
  }

  sendState();
}



/********************************** START PROCESS JSON*****************************************/
bool processJson(char* message) {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial.println("parseObject() failed");
    return false;
  }

  if (root.containsKey("state")) {
    if (strcmp(root["state"], on_cmd) == 0) {
      stateOn = true;
    }
    else if (strcmp(root["state"], off_cmd) == 0) {
      stateOn = false;
    }
  }

  return true;
}

/********************************** START SEND STATE*****************************************/
void sendState() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (stateOn) ? on_cmd : off_cmd;
  root["distance"] = distance;

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  Serial.println(buffer);
  client.publish(parking_state_topic, buffer, true);
}

//************************************************************************************************
//Main loop where the action takes place
//************************************************************************************************
void loop()
{
  ArduinoOTA.handle();
  
  if (!client.connected()) {
    // reconnect();
    software_Reset();
  }
  client.loop();
  
  pinMode(pwPin, INPUT);
  
  for (int i = 0; i < arraysize; i++)
  {
    pulse = pulseIn(pwPin, HIGH);
    rangevalue[i] = pulse / 58;
    delay(10);
  }

  if (stateOn) {
    //Serial.print("Unsorted: ");
    //printArray(rangevalue, arraysize);
    isort(rangevalue, arraysize);
  
    //Serial.print("Sorted: ");
    //printArray(rangevalue, arraysize);
    distance = mode(rangevalue, arraysize);
    //Serial.print("The mode/median is: ");
    //Serial.print(distance);
    //Serial.println();
    if (distance != old_distance) { //distance has changed
        OSCMessage msg("/distance");
        msg.add(distance);
        Udp.beginPacket(outIp, outPort);
        msg.send(Udp);
        Udp.endPacket();
        msg.empty();
        
        delay(200);
        sendState();
        old_distance = distance;
    }
  } else {
    OSCMessage msg("/distance");
    msg.add(-999); // send off message to lights
    Udp.beginPacket(outIp, outPort);
    msg.send(Udp);
    Udp.endPacket();
    msg.empty();
    
    delay(1000); // we can wait longer within this cycle
  }
}

/********************************** START RECONNECT*****************************************/
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(SENSORNAME, mqtt_user, mqtt_password)) {
      Serial.println("connected");
      client.subscribe(parking_set_topic);
      sendState();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/****reset***/
void software_Reset() // Restarts program from beginning but does not reset the peripherals and registers
{
Serial.print("resetting");
ESP.reset(); 
}
