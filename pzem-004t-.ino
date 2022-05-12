#include <Arduino.h>
#include <HardwareSerial.h>
#include <PZEM004T.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// ESP32 connections - SCL: D22; SDA: D21 of the ESP32.
#include "time.h"

void checkpqfunc();

//Pin definitions
const int relay = 5;
const int GREEN = 19;
const int RED = 18;

const int threshold_Vrms = 230;
const int undervoltage = 0.9 * threshold_Vrms;
const int overvoltage = 1.1*threshold_Vrms;

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

HardwareSerial PzemSerial2(2);     // Use hwserial UART2 at pins IO-16 (RX2) and IO-17 (TX2)
PZEM004T pzem(&PzemSerial2);

IPAddress ip(192,168,1,1);

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Insert your network credentials
#define WIFI_SSID "******"
#define WIFI_PASSWORD "******"

// Insert Firebase project API Key
#define API_KEY "****"

// Insert Authorized Email and Corresponding Password
#define USER_EMAIL "*******"
#define USER_PASSWORD "******"

// Insert RTDB URLefine the RTDB URL
#define DATABASE_URL "********"

// Define Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Variable to save USER UID
String uid;

// Database main path (to be updated in setup with the user UID)
String databasePath;

// Database child nodes
String voltPath = "/voltage";
String currPath = "/current";
String powPath = "/power";
String activePath = "/activeenergy";
String timePath = "/timestamp";

// Parent Node (to be updated in every loop)
String parentPath;

int timestamp;
FirebaseJson json;

const char* ntpServer = "pool.ntp.org";
float v;
float voltage;
float current;
float power;
float energy;

// Timer variables (send new readings every three minutes)
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 180000;
unsigned long previMillis = 0;
const long interval = 60000;

// Initialize WiFi
void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
  Serial.println();
}
// Function that gets current epoch time
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}
void setup() {
  Serial.begin(115200);
  pzem.setAddress(ip);
  initWiFi();
  configTime(0, 0, ntpServer);
  //--------------------------pin modes----------------------//
  pinMode(GREEN, OUTPUT);
  pinMode(RED, OUTPUT);
  pinMode(relay, OUTPUT);
  // ............................for the display...............................//

  //initialize display display with address 0x3C for 128x64
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  delay(2000);         // wait for initializing
  display.clearDisplay(); // clear display

  display.setTextSize(2);          // text size
  display.setTextColor(WHITE);     // text color
  display.setCursor(0, 10);        // position to display
  display.println("PQ Disturbances: Init!!!"); // text to display
  display.display();               // show on display

  //......................................./display.........................//
    
  // Assign the api key (required)
  config.api_key = API_KEY;
  
  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Assign the RTDB URL (required)
  config.database_url = DATABASE_URL;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  // Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  // Assign the maximum retry of token generation
  config.max_token_generation_retry = 5;

  // Initialize the library with the Firebase authen and config
  Firebase.begin(&config, &auth);

  // Getting the user UID might take a few seconds
  Serial.println("Getting User UID");
  while ((auth.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }
  // Print user UID
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);

  // Update database path
  databasePath = "/UsersData/" + uid + "/readings";
}
// PQ functions ..................................................................// 
void checkpqfunc(){ // include timing to meet the time requirements
  float v = pzem.voltage(ip);
  
  unsigned long currentMillis = millis();
  if(currentMillis - previMillis >= interval){
      previMillis = currentMillis;
       if (v <= undervoltage && v >= 0){ // undervoltage
       digitalWrite(relay, LOW);
       digitalWrite(GREEN, LOW);
       digitalWrite(RED, HIGH);

       display.clearDisplay();
       display.setCursor(0, 10);        // position to display
       display.println("Power Flow: ");
       display.print("Under voltage");
 
       display.display();
       }
     else if (v >= overvoltage){ // overvoltage
       digitalWrite(relay, LOW);
       digitalWrite(GREEN, LOW);
       digitalWrite(RED, HIGH);
       
       display.clearDisplay();
       display.setCursor(0, 10);        // position to display
       display.println("Power Flow: ");
       display.print("Over voltage");
 
       display.display();
       }
     else if (v <0){ // interruptions
       digitalWrite(relay, LOW);
       digitalWrite(GREEN, LOW);
       digitalWrite(RED, HIGH);
       
       display.clearDisplay();
       display.setCursor(0, 10);        // position to display
       display.print("Interruption");
 
       display.display();
       }
     else{
       digitalWrite(relay, HIGH);
       digitalWrite(GREEN, HIGH);
       digitalWrite(RED, LOW);
       
       display.clearDisplay();
       display.setCursor(0, 10);        // position to display
       display.println("Power Flow: ");
       display.print("Normal");
 
       display.display();
       }
   }
}
// -------------------------------------------//end of pq funcs-------------------//  
void loop() {
   //.......................power quality func calling .........................//    
    checkpqfunc();
   // ----------------------end of pq funcs calls ------------------------------//  
  if (Firebase.ready() && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0)){
    sendDataPrevMillis = millis();
    //Get current timestamp
    timestamp = getTime();
    Serial.print ("time: ");
    Serial.println (timestamp);
   
    parentPath= databasePath + "/" + String(timestamp);

    json.set(timePath, String(timestamp));
    json.set(voltPath.c_str(), String(pzem.voltage(ip)));
    json.set(currPath.c_str(), String(pzem.current(ip)));
    json.set(powPath.c_str(), String(pzem.power(ip)));
    json.set(activePath.c_str(), String(pzem.energy(ip)));
    Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json) ? "ok" : fbdo.errorReason().c_str());
  } 
}
