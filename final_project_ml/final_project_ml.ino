/*
 * ============================================================
 * SMART CAGE - ML INTEGRATED (SIC Phase 3)
 * ESP32 + DHT11 + MQTT + ML Prediction
 * ============================================================
 * 
 * FLOW:
 * 1. ESP32 baca sensor (DHT11)
 * 2. Publish data ke MQTT topic /data
 * 3. Dashboard ML terima data → Predict kondisi
 * 4. Dashboard publish prediksi ke topic /prediction
 * 5.ESP32 subscribe /prediction → Trigger output sesuai ML result
 * 
 * OUTPUT berdasarkan ML PREDICTION (bukan if-else):
 * - Ideal  → LED Hijau ON, Relay OFF (kipas nyala)
 * - Panas  → LED Merah kedip, Buzzer ON, Relay OFF
 * - Dingin → LED Kuning kedip, Buzzer ON, Relay ON
 * ============================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// -------------------------
// DEFINISI PIN
// -------------------------
#define PIN_MERAH   14
#define PIN_KUNING  12
#define PIN_HIJAU   18
#define PIN_RELAY   25
#define PIN_BUZZER  26
#define DHTPIN      4
#define DHTTYPE     DHT11

// -------------------------
// WIFI & MQTT CONFIG
// -------------------------
const char* ssid = "SIC-UG";
const char* password = "gunadarma";
const char* mqtt_server = "broker.hivemq.com";

const char* topic_data = "final-project/Mahasiswa-Berpola-Pikir/smartcage/data";
const char* topic_prediction = "final-project/Mahasiswa-Berpola-Pikir/smartcage/prediction";

String clientId = "SmartCageESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);

WiFiClient espClient;
PubSubClient client(espClient);

// -------------------------
// OBJEK SENSOR & DISPLAY
// -------------------------
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// -------------------------
// VARIABEL TIMER
// -------------------------
unsigned long previousMillisSensor = 0;
unsigned long previousMillisBlink = 0;
unsigned long previousMillisBuzzer = 0;
unsigned long previousMillisLCD = 0;

const long intervalSensor = 2000;  // Baca sensor every 2s
const long intervalBlink = 300;     // LED blink
const long intervalBuzzer = 500;    // Buzzer beep
const long intervalLCD = 100;       // LCD update

// -------------------------
// VARIABEL STATUS
// -------------------------
float suhu = 0.0;
float kelembapan = 0.0;
String mlPrediction = "Waiting";    // Prediksi dari ML
float mlConfidence = 0.0;           // Confidence dari ML
bool ledBlinkState = LOW;
bool buzzerState = LOW;
String lastLcdLine1 = "";
String lastLcdLine2 = "";

// -------------------------
// SETUP WIFI
// -------------------------
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// -------------------------
// MQTT CALLBACK
// -------------------------
void callback(char* topic, byte* payload, unsigned int length) {
  // Parse JSON dari dashboard ML
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    Serial.println("Failed to parse ML prediction!");
    return;
  }
  
  // Extract ML prediction
  String kondisi = doc["kondisi"];
  float confidence = doc["confidence"];
  
  mlPrediction = kondisi;
  mlConfidence = confidence;
  
  Serial.println("======================");
  Serial.println("ML PREDICTION RECEIVED:");
  Serial.print("Kondisi: ");
  Serial.println(kondisi);
  Serial.print("Confidence: ");
  Serial.print(confidence);
  Serial.println("%");
  Serial.println("======================");
}

// -------------------------
// MQTT RECONNECT
// -------------------------
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected!");
      
      // Subscribe to prediction topic
      client.subscribe(topic_prediction);
      Serial.print("Subscribed to: ");
      Serial.println(topic_prediction);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 2 seconds");
      delay(2000);
    }
  }
}

// -------------------------
// SETUP
// -------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin();
  dht.begin();

  // Setup LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Cage ML");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  // Setup WiFi & MQTT
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);  // Set callback for ML prediction

  // Setup OLED
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed!");
  }
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  // Setup pins
  pinMode(PIN_MERAH, OUTPUT);
  pinMode(PIN_KUNING, OUTPUT);
  pinMode(PIN_HIJAU, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  
  // Default state
  digitalWrite(PIN_MERAH, LOW);
  digitalWrite(PIN_KUNING, LOW);
  digitalWrite(PIN_HIJAU, LOW);
  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  lcd.clear();
  lcd.print("ML System Ready");
  delay(1000);
  lcd.clear();
}

// -------------------------
// LOOP UTAMA
// -------------------------
void loop() {
  // MQTT connection check
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long currentMillis = millis();

  // ===================================================================
  // FASE 1: BACA SENSOR & PUBLISH (Every 2 detik)
  // ===================================================================
  if (currentMillis - previousMillisSensor >= intervalSensor) {
    previousMillisSensor = currentMillis;

    suhu = dht.readTemperature();
    kelembapan = dht.readHumidity();

    if (isnan(suhu) || isnan(kelembapan)) {
      Serial.println("Sensor read error!");
      return;
    }

    Serial.print("Sensor → T=");
    Serial.print(suhu);
    Serial.print("°C, H=");
    Serial.print(kelembapan);
    Serial.println("%");

    // Publish raw sensor data to ML dashboard
    StaticJsonDocument<256> doc;
    doc["temp"] = suhu;
    doc["humidity"] = kelembapan;

    char payload[256];
    serializeJson(doc, payload);
    client.publish(topic_data, payload);
    
    Serial.print("Published to ML: ");
    Serial.println(payload);

    // Update OLED
    updateOLED();
  }

  // ===================================================================
  // FASE 2: TRIGGER OUTPUT BERDASARKAN ML PREDICTION
  // ===================================================================
  
  if (mlPrediction == "Ideal") {
    handleIdeal(currentMillis);
  } 
  else if (mlPrediction == "Panas") {
    handlePanas(currentMillis);
  } 
  else if (mlPrediction == "Dingin") {
    handleDingin(currentMillis);
  }
  else {
    // Waiting for ML prediction
    handleWaiting(currentMillis);
  }

  // ===================================================================
  // FASE 3: UPDATE LCD
  // ===================================================================
  updateLCD(currentMillis);
}

// ===================================================================
// FUNGSI HANDLER OUTPUT (BERDASARKAN ML PREDICTION)
// ===================================================================

void handleIdeal(unsigned long currentMillis) {
  // LED Hijau ON, Merah & Kuning OFF
  digitalWrite(PIN_HIJAU, HIGH);
  digitalWrite(PIN_MERAH, LOW);
  digitalWrite(PIN_KUNING, LOW);
  
  // Relay OFF = Kipas NYALA
  digitalWrite(PIN_RELAY, LOW);
  
  // Buzzer OFF
  digitalWrite(PIN_BUZZER, LOW);
}

void handlePanas(unsigned long currentMillis) {
  // LED Hijau & Kuning OFF
  digitalWrite(PIN_HIJAU, LOW);
  digitalWrite(PIN_KUNING, LOW);
  
  // LED Merah BLINK
  if (currentMillis - previousMillisBlink >= intervalBlink) {
    previousMillisBlink = currentMillis;
    ledBlinkState = !ledBlinkState;
    digitalWrite(PIN_MERAH, ledBlinkState);
  }
  
  // Relay OFF = Kipas NYALA (untuk cooling)
  digitalWrite(PIN_RELAY, LOW);
  
  // Buzzer BEEP
  if (currentMillis - previousMillisBuzzer >= intervalBuzzer) {
    previousMillisBuzzer = currentMillis;
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState);
  }
}

void handleDingin(unsigned long currentMillis) {
  // LED Hijau & Merah OFF
  digitalWrite(PIN_HIJAU, LOW);
  digitalWrite(PIN_MERAH, LOW);
  
  // LED Kuning BLINK
  if (currentMillis - previousMillisBlink >= intervalBlink) {
    previousMillisBlink = currentMillis;
    ledBlinkState = !ledBlinkState;
    digitalWrite(PIN_KUNING, ledBlinkState);
  }
  
  // Relay ON = Kipas MATI (untuk warming)
  digitalWrite(PIN_RELAY, HIGH);
  
  // Buzzer BEEP
  if (currentMillis - previousMillisBuzzer >= intervalBuzzer) {
    previousMillisBuzzer = currentMillis;
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState);
  }
}

void handleWaiting(unsigned long currentMillis) {
  // All LEDs OFF, waiting for ML
  digitalWrite(PIN_HIJAU, LOW);
  digitalWrite(PIN_MERAH, LOW);
  digitalWrite(PIN_KUNING, LOW);
  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_BUZZER, LOW);
}

// ===================================================================
// FUNGSI UPDATE DISPLAY
// ===================================================================

void updateOLED() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Smart Cage ML");
  oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  
  oled.setCursor(0, 15);
  oled.print("Suhu: ");
  oled.print(suhu);
  oled.println(" C");
  
  oled.setCursor(0, 25);
  oled.print("Kelembapan: ");
  oled.print(kelembapan);
  oled.println("%");
  
  oled.setCursor(0, 40);
  oled.print("ML: ");
  oled.println(mlPrediction);
  
  oled.setCursor(0, 50);
  oled.print("Conf: ");
  oled.print(mlConfidence);
  oled.println("%");
  
  oled.display();
}

void updateLCD(unsigned long currentMillis) {
  if (currentMillis - previousMillisLCD >= intervalLCD) {
    previousMillisLCD = currentMillis;
    
    String line1 = "ML: " + mlPrediction;
    String line2 = String(suhu, 1) + "C " + String(kelembapan, 1) + "%";
    
    // Only update if changed
    if (line1 != lastLcdLine1 || line2 != lastLcdLine2) {
      lcd.setCursor(0, 0);
      lcd.print("                ");
      lcd.setCursor(0, 0);
      lcd.print(line1);
      
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print(line2);
      
      lastLcdLine1 = line1;
      lastLcdLine2 = line2;
    }
  }
}
