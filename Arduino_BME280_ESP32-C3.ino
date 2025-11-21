#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <Update.h>

#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <ArduinoJson.h>
#include <esp_sntp.h>

// ---- I2C Pins für ESP32-C3 Zero ----
#define SDA_PIN 8
#define SCL_PIN 9

// ---- USER SETTINGS ----
const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_WIFIPASS";

const char* mqtt_server = "192.168.0.155";  //change here?
const int   mqtt_port   = 1883;
const char* mqtt_user   = "mqtt";
const char* mqtt_pass   = "YOUR_MQTT_PASS";

String deviceName = "esp32_bme280_Innen";
int telePeriod = 300;   // Sekunden  // its enough to know the data any 5 minutes.

// ---- GLOBALS ----
Adafruit_BME280 bme;
WiFiClient espClient;
PubSubClient client(espClient);
WebServer server(80);

unsigned long lastTele = 0;
unsigned long bootTime = millis();

// ----------------------------------------------------
void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

// ----------------------------------------------------
void initTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time");
  int retries = 0;
  while (!time(nullptr) && retries < 20) {
    Serial.print(".");
    retries++;
    delay(500);
  }
  Serial.println("\nTime synced.");
}

String getTimeString() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);

  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
           t->tm_year + 1900,
           t->tm_mon + 1,
           t->tm_mday,
           t->tm_hour,
           t->tm_min,
           t->tm_sec);

  return String(buf);
}

// ----------------------------------------------------
void reconnect() {
  while (!client.connected()) {
    String clientId = "esp32_bme280_Innen";  // feste ID

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("MQTT connected.");
    } else {
      Serial.print("Failed rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

// ----------------------------------------------------
void publishSensorData() {
  float temp = bme.readTemperature();
  float hum  = bme.readHumidity();
  float pres = bme.readPressure() / 100.0F;

  StaticJsonDocument<256> doc;

  doc["Time"] = getTimeString();
  JsonObject BME = doc.createNestedObject("BME280");
  BME["Temperature"] = temp;
  BME["Humidity"]    = hum;
  BME["Pressure"]    = pres;

  char out[256];
  serializeJson(doc, out);

  String topic = "tele/" + deviceName + "/SENSOR";
  client.publish(topic.c_str(), out, false);

  Serial.print("MQTT: ");
  Serial.println(out);
}

// ----------------------------------------------------
// WEB API ENDPOINTS
// ----------------------------------------------------

void handleJSON() {
  StaticJsonDocument<256> doc;

  doc["Time"] = getTimeString();
  JsonObject BME = doc.createNestedObject("BME280");
  BME["Temperature"] = bme.readTemperature();
  BME["Humidity"]    = bme.readHumidity();
  BME["Pressure"]    = bme.readPressure() / 100.0F;

  String json;
  serializeJsonPretty(doc, json);
  server.send(200, "application/json", json);
}

void handleInfo() {
  StaticJsonDocument<256> doc;

  doc["Device"] = deviceName;
  doc["IP"] = WiFi.localIP().toString();
  doc["RSSI"] = WiFi.RSSI();
  doc["Uptime_s"] = (millis() - bootTime) / 1000;
  doc["Time"] = getTimeString();

  String json;
  serializeJsonPretty(doc, json);
  server.send(200, "application/json", json);
}

void handleReboot() {
  server.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}

// OTA Upload Form
const char* updateForm =
"<form method='POST' action='/update' enctype='multipart/form-data'>"
"<input type='file' name='update'>"
"<input type='submit' value='Update'>"
"</form>";

void handleUpdateForm() {
  server.send(200, "text/html", updateForm);
}

void handleUpdate() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    Serial.setDebugOutput(true);
    Update.begin();
    Serial.printf("Update: %s\n", upload.filename.c_str());
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    Update.write(upload.buf, upload.currentSize);
  } 
  else if (upload.status == UPLOAD_FILE_END) {
    Update.end(true);
    Serial.printf("Update Success\n");
  }
}

void setupServer() {
  server.on("/", [](){ server.send(200, "text/plain", "ESP32-C3 OK"); });
  server.on("/json", handleJSON);
  server.on("/info", handleInfo);
  server.on("/reboot", handleReboot);

  // OTA
  server.on("/update", HTTP_GET, handleUpdateForm);
  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
  }, handleUpdate);

  server.begin();
  Serial.println("Web server running.");
}

// ----------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin(SDA_PIN, SCL_PIN);
  delay(10);

  Serial.println("Initializing BME280...");

  if (!bme.begin(0x76)) {
    if (!bme.begin(0x77)) {
      if (!bme.begin()) {
        Serial.println("!!! BME280 NICHT gefunden !!!");
        while (1) delay(100);
      }
    }
  }

  Serial.println("BME280 erfolgreich initialisiert!");

  setup_wifi();
  initTime();
  client.setServer(mqtt_server, mqtt_port);

  setupServer();

  Serial.println("Setup fertig.");
}

// ----------------------------------------------------
void loop() {
  server.handleClient();

  if (!client.connected()) reconnect();
  client.loop();

  if (millis() - lastTele > telePeriod * 1000UL) {
    lastTele = millis();
    publishSensorData();
  }
}
