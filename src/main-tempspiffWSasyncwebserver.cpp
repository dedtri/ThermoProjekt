// /*********
//   Rui Santos
//   Complete project details at https://RandomNerdTutorials.com  
// *********/

// #include <WiFi.h>
// #include <ESPAsyncWebServer.h>
// #include <SPIFFS.h>

// #include <OneWire.h>
// #include <DallasTemperature.h>

// // GPIO where the DS18B20 is connected to
// const int oneWireBus = 4;     

// // Setup a oneWire instance to communicate with any OneWire devices
// OneWire oneWire(oneWireBus);

// // Pass our oneWire reference to Dallas Temperature sensor 
// DallasTemperature sensors(&oneWire);

// // Replace with your network credentials
// const char* ssid = "E308";
// const char* password = "98806829";

// // Create AsyncWebServer object on port 80
// AsyncWebServer server(80);
// AsyncWebSocket ws("/ws");

// String readBMEDS18B20Temperature();

// void handleWebSocketData(AsyncWebSocketClient *client, uint8_t *data, size_t len) {
//   String command = String((char *)data);

//   if (command == "get_temperature") {
//     float temperatureC = sensors.getTempCByIndex(0);
//     String temperatureString = String(temperatureC);
//     client->text(temperatureString);
//   }
// }

// void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
//              void *arg, uint8_t *data, size_t len) {
//   switch (type) {
//     case WS_EVT_CONNECT:
//       Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
//       break;
//     case WS_EVT_DISCONNECT:
//       Serial.printf("WebSocket client #%u disconnected\n", client->id());
//       break;
//     case WS_EVT_DATA:
//       handleWebSocketData(client, data, len);
//       break;
//     case WS_EVT_PONG:
//     case WS_EVT_ERROR:
//       break;
//   }
// }

// void initWebSocket() {
//   ws.onEvent(onEvent);
//   server.addHandler(&ws);
// }

// void setup() {
//   // Start the Serial Monitor
//   Serial.begin(115200);
//   // Start the DS18B20 sensor
//   sensors.begin();

//   if(!SPIFFS.begin()){
//   Serial.println("An Error has occurred while mounting SPIFFS");
//   return;
// }

// WiFi.begin(ssid, password);
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(1000);
//     Serial.println("Connecting to WiFi..");
//   }

//   Serial.println(WiFi.localIP());

//   initWebSocket();

//   // Route for root / web page
//   server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
//     request->send(SPIFFS, "/index.html");
//   });
//   server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
//     request->send_P(200, "text/plain", readBMEDS18B20Temperature().c_str());
//   });

//   // Start server
//   server.begin();
// } 

// float lastTemperatureSent = 0.0;

// void loop() {
//   sensors.requestTemperatures(); 
//   float temperatureC = sensors.getTempCByIndex(0);

//   if (temperatureC != lastTemperatureSent) {
//     Serial.print("New temperature: ");
//     Serial.println(temperatureC);
//     lastTemperatureSent = temperatureC;

//     // Send temperature data to all connected WebSocket clients
//     String temperatureString = String(temperatureC);
//     ws.textAll(temperatureString);
//   }

//   Serial.print(temperatureC);
//   Serial.println("ºC");
//   delay(1000);
// }

// String readBMEDS18B20Temperature() {
//   // Read temperature as Celsius (the default)
//   float t = sensors.getTempCByIndex(0);
//   // Convert temperature to Fahrenheit
//   //t = 1.8 * t + 32;
//   if (isnan(t)) {
//     Serial.println("Failed to read from BMEDS18B20 sensor!");
//     return "";
//   }
//   else {
//     Serial.println(t);
//     return String(t);
//   }
// }

