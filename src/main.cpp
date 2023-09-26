/*********
  Rui Santos
  Complete project details at https://randomnerdtutorials.com  
*********/

// Libraries for SD card
#include "FS.h"
#include "SD.h"
#include <SPI.h>
#include <SPIFFS.h>

//DS18B20 libraries
#include <OneWire.h>
#include <DallasTemperature.h>

//Libraries to get time from NTP Server
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESPAsyncWebServer.h>

//Prototypes
void getReadings();
void getTimeStamp();
void logSDCard();
void writeFile(fs::FS &fs, const char * path, const char * message);
void appendFile(fs::FS &fs, const char * path, const char * message);

// Define deep sleep options
uint64_t uS_TO_S_FACTOR = 1000000;  // Conversion factor for micro seconds to seconds
// Sleep for 10 minutes = 600 seconds
uint64_t TIME_TO_SLEEP = 60;

unsigned long startTime;
unsigned long sleepDuration = 4 * 60 * 1000; // 4 minutes in milliseconds

// Replace with your network credentials
const char* ssid     = "WNet";
const char* password = "591GL5ENA1L";

// Define CS pin for the SD card module
#define SD_CS 5

// Save reading number on RTC memory
RTC_DATA_ATTR int readingID = 0;

String dataMessage;

// Data wire is connected to ESP32 GPIO 21
#define ONE_WIRE_BUS 4
// Setup a oneWire instance to communicate with a OneWire device
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

// Temperature Sensor variables
float temperatureC;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Variables to save date and time
String formattedDate;
String dayStamp;
String timeStamp;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void setup() {
  // Start serial communication for debugging purposes
  Serial.begin(115200);

  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");

  if(!SPIFFS.begin()){
  Serial.println("An Error has occurred while mounting SPIFFS");
  return;
  }

  // Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(7200);

  // Initialize SD card
  SD.begin(SD_CS);  
  if(!SD.begin(SD_CS)) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }
  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR - SD card initialization failed!");
    return;    // init failed
  }

  // If the data.txt file doesn't exist
  // Create a file on the SD card and write the data labels
  File file = SD.open("/data.txt");
  if(!file) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/data.txt", "Reading ID, Date, Hour, Temperature \r\n");
  }
  else {
    Serial.println("File already exists");  
  }
  file.close();

  // Enable Timer wake_up
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  // Start the DallasTemperature library
  sensors.begin(); 
  
  // Increment readingID on every new reading
  readingID++;

  initWebSocket();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/index.html");
  });

  server.on("/downloadCSV", HTTP_GET, [](AsyncWebServerRequest *request){
  File file = SD.open("/data.txt");
  if (!file) {
     request->send(404, "text/plain", "CSV file not found");
     } else {
      request->send(SD, "/data.txt", "text/csv", false);
      file.close();
    }
  });

  server.on("/clearCSV", HTTP_GET, [](AsyncWebServerRequest *request){
  if (SD.remove("/data.txt")) {
    request->send(200, "text/plain", "CSV file cleared successfully");
  } else {
    request->send(500, "text/plain", "Failed to clear CSV file");
  }
});

  Serial.println(WiFi.localIP());

  // Start server
  server.begin();
  
  // Store the current time as the start time
  startTime = millis();
  Serial.println("DONE! Going to sleep in 4 minutes.");
}

unsigned long lastExecutionTime = 0; // Initialize a variable to track the last execution time
const unsigned long delayInterval = 60000; // 1 minutes in milliseconds

void loop() {

  unsigned long currentTime = millis();

  // Check if it's time to go to sleep
  if (millis() - startTime >= sleepDuration) {
    Serial.println("Going to sleep now.");
    esp_deep_sleep_start();
  } 

 // If its not time to sleep do this: 
    if (currentTime - lastExecutionTime >= delayInterval) {
    lastExecutionTime = currentTime; // Update the last execution time
    getReadings();
    getTimeStamp();
    logSDCard();
  } 
}

float lastTemperatureSent = 0.0;

// Function to get temperature
void getReadings(){

  sensors.requestTemperatures(); 
  temperatureC = sensors.getTempCByIndex(0);

  if (temperatureC != lastTemperatureSent) {
    Serial.print("Temperature: ");
    Serial.println(temperatureC);
    lastTemperatureSent = temperatureC;

    // Send temperature data to all connected WebSocket clients
    String temperatureString = String(temperatureC);
    ws.textAll(temperatureString);
  }

}

// Function to get date and time from NTPClient
void getTimeStamp() {
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  // The formattedDate comes with the following format:
  // 2018-05-28T16:00:13Z
  // We need to extract date and time
  formattedDate = timeClient.getFormattedDate();
  Serial.println(formattedDate);

  // Extract date
  int splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  Serial.println(dayStamp);
  // Extract time
  timeStamp = formattedDate.substring(splitT+1, formattedDate.length()-1);
  Serial.println(timeStamp);
}

// Write the sensor readings on the SD card
void logSDCard() {
  dataMessage = String(readingID) + "," + String(dayStamp) + "," + String(timeStamp) + "," + 
                String(temperatureC) + "\r\n";
  Serial.print("Save data: ");
  Serial.println(dataMessage);
  appendFile(SD, "/data.txt", dataMessage.c_str());
  //Run WS Here
}

// Write to the SD card (DON'T MODIFY THIS FUNCTION)
void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

// Append data to the SD card (DON'T MODIFY THIS FUNCTION)
void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if(!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if(file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}