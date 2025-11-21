#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <ArduinoJson.h>
#include <esp_sntp.h>

// ---- I2C Pins für ESP32-C3 Zero ----
#define SDA_PIN 8
#define SCL_PIN 9

// ---- USER SETTINGS ----
const char* ssid     = "SESSIONID";
const char* password = "WIFI PASSWORD";

const char* mqtt_server = "192.168.0.155"; //your mqtt server 
const int   mqtt_port   = 1883; // standard
const char* mqtt_user   = "your_mqttuser";
const char* mqtt_pass   = "your_pass";

String deviceName = "esp32_bme280";
int telePeriod = 10;   // Sekunden

// ---- GLOBALS ----
Adafruit_BME280 bme;
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastTele = 0;

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

// ----------------------------------------------------
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting MQTT... ");
    String clientId = "ESP32C3-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("OK");
    } else {
      Serial.print("Failed rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

// ----------------------------------------------------
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
void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin(SDA_PIN, SCL_PIN);
  delay(10);

  Serial.println("Initializing BME280...");

  // Sicherer Init-Block
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

  Serial.println("Setup fertig.");
}

// ----------------------------------------------------
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  if (millis() - lastTele > telePeriod * 1000UL) {
    lastTele = millis();
    publishSensorData();
  }
}
