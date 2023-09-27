/**
 * @file main.cpp
 * @brief ESP32-based Temperature Monitoring and Data Logging System
 *
 * This code connects to a Wi-Fi network, reads temperature data from a DS18B20 sensor,
 * logs the data to an SD card, and provides a web interface with WebSocket support.
 * It also allows for deep sleep mode and button interrupt to wake up the device.
 */

#include "FS.h"
#include "SD.h"
#include <SPI.h>
#include <SPIFFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESPAsyncWebServer.h>

// Prototypes
void getReadings();
void getTimeStamp();
void logSDCard();
void writeFile(fs::FS &fs, const char *path, const char *message);
void appendFile(fs::FS &fs, const char *path, const char *message);

// Define deep sleep options
uint64_t uS_TO_S_FACTOR = 1000000; // Conversion factor for microseconds to seconds
// Sleep for 10 minutes = 600 seconds
uint64_t TIME_TO_SLEEP = 600;

unsigned long startTime;
// Delay before going to sleep
unsigned long sleepDelay = 5 * 60 * 1000; // 5 minutes in milliseconds

// Replace with your network credentials
const char *ssid = "SSID HIDDEN";
const char *password = "WIFI PASSWORD HIDDEN";

// Define CS pin for the SD card module
#define SD_CS 5

// Save reading number on RTC memory
RTC_DATA_ATTR int readingID = 0;

String dataMessage;

#define BUTTON_PIN GPIO_NUM_14 // GPIO 14
RTC_DATA_ATTR int buttonPressed = 0;

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

/**
 * @brief WebSocket event handler.
 *
 * Handles WebSocket events such as connection, disconnection, and data reception.
 *
 * @param server The AsyncWebSocket server object.
 * @param client The WebSocket client triggering the event.
 * @param type The type of WebSocket event.
 * @param arg Event argument.
 * @param data Event data.
 * @param len Length of event data.
 */
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

/**
 * @brief Initialize WebSocket.
 *
 * Sets up the WebSocket server and registers the event handler.
 */
void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

/**
 * @brief Arduino setup function.
 *
 * Initializes hardware, connects to Wi-Fi, sets up web server, and prepares for deep sleep.
 */
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(BUTTON_PIN, LOW);

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

  if (!SPIFFS.begin()) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone
  timeClient.setTimeOffset(7200);

  // Initialize SD card
  SD.begin(SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }
  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR - SD card initialization failed!");
    return; // init failed
  }

  // If the data.txt file doesn't exist, create it and write data labels
  File file = SD.open("/data.txt");
  if (!file) {
    Serial.println("File doesn't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/data.txt", "Reading ID, Date, Hour, Temperature \r\n");
  } else {
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

  // Configure web server routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html");
  });

  server.on("/downloadCSV", HTTP_GET, [](AsyncWebServerRequest *request) {
    File file = SD.open("/data.txt");
    if (!file) {
      request->send(404, "text/plain", "CSV file not found");
    } else {
      request->send(SD, "/data.txt", "text/csv", false);
      file.close();
    }
  });

  server.on("/clearCSV", HTTP_GET, [](AsyncWebServerRequest *request) {
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
  Serial.println("DONE! Going to sleep in 5 minutes.");
}

unsigned long lastExecutionTime = 0; // Initialize a variable to track the last execution time
const unsigned long delayInterval = 60000; // 1 minute in milliseconds

/**
 * @brief Button ISR (Interrupt Service Routine).
 *
 * Handles the button press interrupt and sets a flag.
 */
void IRAM_ATTR buttonISR() {
  // Button press detected, set the flag to prevent sleep
  buttonPressed = true;
}

/**
 * @brief Setup button interrupt.
 *
 * Attaches an interrupt handler to the button pin.
 */
void setupButtonInterrupt() {
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, CHANGE);
}

/**
 * @brief Arduino loop function.
 *
 * Monitors button press, logs temperature data, and manages deep sleep.
 */
void loop() {
  unsigned long currentTime = millis();

  if (buttonPressed) {
    // You can perform some action when the button is pressed here
    // For example, send a signal or perform a specific task
    Serial.println("Button pressed. Waking up.");
    buttonPressed = false; // Reset the flag after waking up
  }

  // Check if it's time to go to sleep
  if (millis() - startTime >= sleepDelay) {
    Serial.println("Going to sleep now.");
    esp_deep_sleep_start();
  }

  // If it's not time to sleep, periodically read and log data
  if (currentTime - lastExecutionTime >= delayInterval) {
    lastExecutionTime = currentTime; // Update the last execution time
    getReadings();
    getTimeStamp();
    logSDCard();
  }
}

float lastTemperatureSent = 0.0;

/**
 * @brief Get temperature readings from DS18B20 sensor.
 *
 * Reads temperature data from the DS18B20 sensor and sends it to connected WebSocket clients.
 */
void getReadings() {
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

/**
 * @brief Get date and time from NTP server.
 *
 * Retrieves date and time information from the NTP server and formats it for logging.
 */
void getTimeStamp() {
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  // The formattedDate comes with the following format: "2018-05-28T16:00:13Z"
  // We need to extract date and time
  formattedDate = timeClient.getFormattedDate();
  Serial.println(formattedDate);

  // Extract date
  int splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  Serial.println(dayStamp);
  // Extract time
  timeStamp = formattedDate.substring(splitT + 1, formattedDate.length() - 1);
  Serial.println(timeStamp);
}

/**
 * @brief Log sensor data to SD card.
 *
 * Formats sensor data and logs it to the SD card.
 */
void logSDCard() {
  dataMessage = String(readingID) + "," + String(dayStamp) + "," + String(timeStamp) + "," +
                String(temperatureC) + "\r\n";
  Serial.print("Save data: ");
  Serial.println(dataMessage);
  appendFile(SD, "/data.txt", dataMessage.c_str());
}

/**
 * @brief Write to the SD card.
 *
 * Writes data to the specified file on the SD card.
 *
 * @param fs The file system to use (e.g., SD or SPIFFS).
 * @param path The path to the file.
 * @param message The data to write.
 */
void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

/**
 * @brief Append data to the SD card.
 *
 * Appends data to the specified file on the SD card.
 *
 * @param fs The file system to use (e.g., SD or SPIFFS).
 * @param path The path to the file.
 * @param message The data to append.
 */
void appendFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}