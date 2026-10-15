/*
  ========================================================================
  🌱 ระบบวัดสภาพดินแบบพกพา (Node-RED / MQTT Version + Soft Reset) 📊
  ========================================================================
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <SD.h>

// ---------------- 📶 Wi-Fi Credentials ----------------
char ssid[] = "Popo";
char pass[] = "21092549za";

// ---------------- 🌐 MQTT Server Settings ----------------
const char* mqtt_server = "broker.hivemq.com"; 
const int   mqtt_port   = 1883;
const char* mqtt_topic  = "sensor/data"; 
const char* mqtt_user   = "mon";     // ใส่ User ของ Thaiappify
const char* mqtt_pass   = "mon";     // ใส่ Password ของ Thaiappify

// ---------------- 📌 Pin Definitions ----------------
#define SOIL_PIN            34
#define DHT_PIN             16
#define MEASURE_BUTTON_PIN  27  // ปุ่มสั่งวัดค่าใหม่ (Active HIGH)
#define RESET_BUTTON_PIN    4   // ปุ่มสั่ง Soft Reset (GPIO 4, Active HIGH)
#define SD_CS_PIN            5
#define DHT_TYPE          DHT22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ---------------- 🌾 Soil Sensor Calibration ----------------
const int DRY_VALUE = 2560;   
const int WET_VALUE = 942;   

// ---------------- ⏱️ Debounce Time Variables ----------------
unsigned long lastMeasureDebounceTime = 0;
unsigned long lastResetDebounceTime   = 0;
const unsigned long DEBOUNCE_DELAY    = 800; // เวลาหน่วงดักปุ่มกระดอนเป็น 800ms

// ---------------- 🛠️ Objects & Status Flags ----------------
WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHT_PIN, DHT_TYPE);
BH1750 lightMeter;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const char* LOG_FILE = "/log.csv";
bool oledReady = false;
bool sdReady = false;
bool bh1750Ready = false;

// Custom Icons Bitmap (16x16 px)
const unsigned char epd_bitmap_soil[] PROGMEM = { 0x01, 0x80, 0x03, 0xc0, 0x07, 0xe0, 0x03, 0xc0, 0x03, 0xc0, 0x11, 0x88, 0x39, 0x9c, 0x7d, 0xbe, 0x7d, 0xbe, 0x3f, 0xfc, 0x1f, 0xf8, 0x03, 0xc0, 0x03, 0xc0, 0x7f, 0xfe, 0xff, 0xff, 0xff, 0xff };
const unsigned char epd_bitmap_temp[] PROGMEM = { 0x07, 0x00, 0x08, 0x80, 0x09, 0x80, 0x08, 0x80, 0x09, 0x80, 0x08, 0x80, 0x09, 0x80, 0x08, 0x80, 0x11, 0x40, 0x23, 0x20, 0x27, 0x20, 0x27, 0x20, 0x23, 0x20, 0x11, 0x40, 0x0e, 0x00, 0x00, 0x00 };
const unsigned char epd_bitmap_hum[] PROGMEM  = { 0x01, 0x00, 0x02, 0x80, 0x04, 0x40, 0x08, 0x20, 0x10, 0x10, 0x20, 0x08, 0x20, 0x08, 0x08, 0x40, 0x40, 0x04, 0x80, 0x02, 0x80, 0x02, 0x83, 0xc2, 0x44, 0x22, 0x28, 0x14, 0x10, 0x08, 0x0f, 0xf0 };
const unsigned char epd_bitmap_sun[] PROGMEM  = { 0x01, 0x80, 0x11, 0x88, 0x00, 0x00, 0x03, 0xc0, 0x0e, 0x70, 0x1c, 0x38, 0x38, 0x1c, 0xef, 0xf7, 0xef, 0xf7, 0x38, 0x1c, 0x1c, 0x38, 0x0e, 0x70, 0x03, 0xc0, 0x00, 0x00, 0x11, 0x88, 0x01, 0x80 };

void bootScreen(String statusText) {
  if (!oledReady) return;
  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(12, 8); display.println("SOIL MONITOR");
  display.setCursor(12, 18); display.println("NODE-RED SYSTEM");
  display.drawLine(10, 30, 118, 30, SSD1306_WHITE);
  display.setCursor(10, 42); display.print("> "); display.print(statusText);
  display.display();
}

void drawResultScreen(int soilPercent, float temperature, float humidity, float lux, bool dhtOk, bool luxOk, bool wifiOnline, bool justSynced, int syncedCount) {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(2, 2); display.print("MONITOR");
  display.setCursor(70, 2);
  display.print(wifiOnline ? "[MQTT]" : (sdReady ? "[SD]" : "[OFF]"));
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  display.drawBitmap(2, 15, epd_bitmap_soil, 16, 16, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(24, 15); display.print(soilPercent);
  display.setTextSize(1); display.print("%");
  display.drawLine(0, 33, 127, 33, SSD1306_WHITE);

  display.drawBitmap(2, 35, epd_bitmap_temp, 16, 16, SSD1306_WHITE);
  display.setCursor(18, 40);
  if (dhtOk) display.printf("%.1fC", temperature); else display.print("N/A");

  display.drawBitmap(2, 49, epd_bitmap_hum, 16, 16, SSD1306_WHITE);
  display.setCursor(18, 53);
  if (dhtOk) display.printf("%.1f%%", humidity); else display.print("N/A");

  display.drawLine(64, 34, 64, 63, SSD1306_WHITE);

  display.drawBitmap(68, 35, epd_bitmap_sun, 16, 16, SSD1306_WHITE);
  display.setCursor(86, 40);
  if (luxOk) {
    if (lux >= 1000) display.printf("%.1fk", lux / 1000.0);
    else display.printf("%.0f", lux);
  } else display.print("N/A");

  display.setCursor(68, 53);
  if (wifiOnline) {
    if (justSynced && syncedCount > 0) display.printf("+%dsync", syncedCount);
    else display.print("Online");
  } else display.print(sdReady ? "Saved" : "No SD");

  display.display();
}

void saveToSD(int soil, float temp, float hum, float lux) {
  if (!sdReady) return;
  File f = SD.open(LOG_FILE, FILE_APPEND);
  if (!f) { sdReady = false; return; }
  f.print(millis()); f.print(",");
  f.print(soil);     f.print(",");
  f.print(temp);     f.print(",");
  f.print(hum);      f.print(",");
  f.println(lux, 2); // บันทึกลง SD ล็อกทศนิยม 2 ตำแหน่ง
  f.close();
  Serial.println("💾 บันทึกลง SD Card เรียบร้อยแล้ว");
}

bool publishMQTT(int soil, float temp, float hum, float lux) {
  if (!client.connected()) return false;
  
  StaticJsonDocument<200> doc;
  doc["soil"] = soil;
  doc["temp"] = serialized(String(temp, 1)); // ล็อกทศนิยม 1 ตำแหน่ง
  doc["hum"]  = serialized(String(hum, 1));  // ล็อกทศนิยม 1 ตำแหน่ง
  doc["lux"]  = serialized(String(lux, 2));  // ล็อกทศนิยม Lux ให้เหลือ 2 ตำแหน่งเป๊ะเสมอ

  char buffer[200];
  serializeJson(doc, buffer);
  
  Serial.print("📤 ส่งข้อมูลออก MQTT: ");
  Serial.println(buffer);

  return client.publish(mqtt_topic, buffer);
}

int syncOfflineData() {
  if (!sdReady || !SD.exists(LOG_FILE)) return 0;
  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) return 0;

  int count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int i1 = line.indexOf(',');
    int i2 = line.indexOf(',', i1 + 1);
    int i3 = line.indexOf(',', i2 + 1);
    int i4 = line.indexOf(',', i3 + 1);
    if (i1 < 0 || i2 < 0 || i3 < 0 || i4 < 0) continue;

    int soil   = line.substring(i1 + 1, i2).toInt();
    float temp = line.substring(i2 + 1, i3).toFloat();
    float hum  = line.substring(i3 + 1, i4).toFloat();
    float lux  = line.substring(i4 + 1).toFloat();

    if (publishMQTT(soil, temp, hum, lux)) {
      count++;
      delay(50);
    }
  }
  f.close();

  if (count > 0) {
    SD.remove(LOG_FILE);
    Serial.printf("☁️ Sync ข้อมูลลง Node-RED สำเร็จ: %d รายการ\n", count);
  }
  return count;
}

void reconnectMQTT() {
  int attempts = 0;
  while (!client.connected() && attempts < 3) {
    Serial.print("🔄 กำลังเชื่อมต่อ MQTT Broker...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println(" ✅ สำเร็จ!");
    } else {
      Serial.print(" ❌ ล้มเหลว (rc=");
      Serial.print(client.state());
      Serial.println(")");
      delay(1000);
      attempts++;
    }
  }
}

void sendSensorData() {
  int rawSoil = analogRead(SOIL_PIN);
  int soilPercent = map(rawSoil, DRY_VALUE, WET_VALUE, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  bool dhtOk = !(isnan(temperature) || isnan(humidity));
  if (!dhtOk) { temperature = 0.0; humidity = 0.0; }

  float lux = 0.0;
  bool luxOk = false;
  if (bh1750Ready) {
    lux = lightMeter.readLightLevel();
    if (lux >= 0) luxOk = true;
  }

  Serial.printf("🌱 Soil: %d%% | 🌡️ Temp: %.1fC | 💧 Hum: %.1f%% | ☀️ Lux: %.2f\n", soilPercent, temperature, humidity, lux);

  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    reconnectMQTT();
  }

  bool online = (WiFi.status() == WL_CONNECTED && client.connected());
  int syncedCount = 0;

  saveToSD(soilPercent, temperature, humidity, lux);

  if (online) {
    publishMQTT(soilPercent, temperature, humidity, lux);
    syncedCount = syncOfflineData();
  } else {
    Serial.println("⚠️ ไม่สามารถส่ง MQTT ได้ (สถานะ Offline / เกาะ Broker ไม่สำเร็จ)");
  }

  drawResultScreen(soilPercent, temperature, humidity, lux, dhtOk, luxOk, online, syncedCount > 0, syncedCount);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledReady = true;
    bootScreen("Init Sensors...");
  }

  dht.begin();
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) bh1750Ready = true;

  pinMode(MEASURE_BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLDOWN);

  bootScreen("Mounting SD...");
  if (SD.begin(SD_CS_PIN)) sdReady = true;

  bootScreen("Connecting WiFi...");
  WiFi.begin(ssid, pass);
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    timeout++;
  }

  client.setServer(mqtt_server, mqtt_port);

  if (WiFi.status() == WL_CONNECTED) {
    bootScreen("MQTT Connecting...");
    reconnectMQTT();
  } else {
    bootScreen("Offline Mode");
  }
  delay(1000);

  sendSensorData();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) reconnectMQTT();
    client.loop();
  }

  // 1. ตรวจจับปุ่มสั่งวัดค่าใหม่ (GPIO 27 - Active HIGH + Debounce)
  if (digitalRead(MEASURE_BUTTON_PIN) == HIGH) {
    if ((millis() - lastMeasureDebounceTime) > DEBOUNCE_DELAY) {
      lastMeasureDebounceTime = millis();
      Serial.println("🔘 กดปุ่มสั่งวัดค่าใหม่...");
      sendSensorData();
      
      while (digitalRead(MEASURE_BUTTON_PIN) == HIGH) {
        delay(20);
      }
      delay(50);
    }
  }

  // 2. ตรวจจับปุ่มสั่ง Soft Reset (GPIO 4 - Active HIGH + Debounce + Clear Dashboard)
  if (digitalRead(RESET_BUTTON_PIN) == HIGH) {
    if ((millis() - lastResetDebounceTime) > DEBOUNCE_DELAY) {
      lastResetDebounceTime = millis();
      Serial.println("🔄 กำลังรีเซ็ตระบบและล้างข้อมูล Dashboard...");
      bootScreen("Resetting...");

      // [เพิ่ม] ยิง MQTT แจ้ง Node-RED และ Dashboard ให้ล้างตารางก่อน restart
      if (WiFi.status() == WL_CONNECTED) {
        if (!client.connected()) reconnectMQTT();
        if (client.connected()) {
          client.publish(mqtt_topic, "{\"action\":\"reset\"}");
          delay(200); // รอให้ส่ง MQTT ออกสำเร็จ
        }
      }

      delay(300);
      ESP.restart(); // สั่งรีสตาร์ทชิป ESP32
    }
  }
}