/*
 * ============================================================
 * SMART CAGE - ML INTEGRATED (SIC Phase 3)
 * ESP32 + DHT11 + MQ2 Gas Sensor + MQTT + ML Prediction
 * ============================================================
 * 
 * FLOW:
 * 1. ESP32 baca sensor (DHT11 + MQ2)
 * 2. Publish data ke MQTT topics terpisah
 * 3. Dashboard ML terima data → Predict kondisi
 * 4. Dashboard publish prediksi ke prediction topics
 * 5. ESP32 subscribe predictions → Trigger output
 * 
 * OUTPUT DHT11 (Suhu/Kelembapan):
 * - Ideal  → LED Hijau ON, Relay OFF (kipas nyala)
 * - Panas  → LED Merah kedip, Buzzer ON, Relay OFF
 * - Dingin → LED Kuning kedip, Buzzer ON, Relay ON
 * 
 * OUTPUT MQ2 (Gas):
 * - Aman    → LED Hijau ON (MQ2 LOW)
 * - Waspada → LED Kuning kedip, Buzzer beep (MQ2 HIGH)
 * - Bahaya  → LED Merah cepat, Buzzer continuous (MQ2 HIGH + Suhu > 55°C)
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
#include <ESP32Servo.h>

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
#define PIN_MQ2     15    // MQ2 Gas Sensor Digital Output
#define PIN_SERVO   13    // Servo Motor for Door
#define PIN_BTN_DOOR 33   // Manual Button for Door Control

// LDR Light Control
#define PIN_LDR         32    // LDR Sensor (Analog Input)
#define PIN_LDR_RELAY   27    // Relay untuk lampu (dikontrol LDR)
#define PIN_LDR_BUTTON  5     // Button manual untuk lampu (prioritas)

// -------------------------
// WIFI & MQTT CONFIG
// -------------------------
const char* ssid = "SIC-UG";
const char* password = "jayajayajaya";
const char* mqtt_server = "broker.hivemq.com";

// Topics for DHT11 (Temperature/Humidity)
const char* topic_data = "final-project/Mahasiswa-Berpola-Pikir/smartcage/data";
const char* topic_prediction = "final-project/Mahasiswa-Berpola-Pikir/smartcage/prediction";

// Topics for MQ2 (Gas Sensor) - SEPARATE
const char* topic_gas_data = "final-project/Mahasiswa-Berpola-Pikir/smartcage/gas/data";
const char* topic_gas_prediction = "final-project/Mahasiswa-Berpola-Pikir/smartcage/gas/prediction";

// Topics for LDR (Light Sensor)
const char* topic_ldr_data = "final-project/Mahasiswa-Berpola-Pikir/smartcage/ldr/data";
const char* topic_ldr_prediction = "final-project/Mahasiswa-Berpola-Pikir/smartcage/ldr/prediction";

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

const long intervalSensor = 1500;   // Baca & publish sensor every 1.5s (safe for HiveMQ)
const long intervalBlink = 300;     // LED blink
const long intervalBuzzer = 500;    // Buzzer beep
const long intervalLCD = 100;       // LCD update

// -------------------------
// VARIABEL STATUS DHT11
// -------------------------
float suhu = 0.0;
float kelembapan = 0.0;
String mlPrediction = "Waiting";
float mlConfidence = 0.0;
bool ledBlinkState = LOW;
bool buzzerState = LOW;
String lastLcdLine1 = "";
String lastLcdLine2 = "";

// -------------------------
// VARIABEL STATUS MQ2 GAS
// -------------------------
bool gasDetected = false;           // MQ2 state (HIGH = gas detected)
String gasKondisi = "Aman";         // Aman, Waspada, Bahaya
String mlGasPrediction = "Waiting"; // ML gas prediction
float mlGasConfidence = 0.0;
bool gasLedBlinkState = LOW;
bool gasBuzzerState = LOW;
unsigned long previousMillisGasBlink = 0;
unsigned long previousMillisGasBuzzer = 0;

// -------------------------
// SERVO DOOR CONTROL
// -------------------------
Servo doorServo;
bool doorOpen = false;              // Current door state (false=closed, true=open)
int currentServoPos = 150;          // Current servo position (starts closed at 180)
unsigned long lastButtonPress = 0;  // Debounce timer
const long debounceDelay = 500;     // Button debounce delay
const int DOOR_CLOSED_POS = 160;    // Closed = 180 degrees
const int DOOR_OPEN_POS = 0;        // Open = 0 degrees

// -------------------------
// LDR + BUTTON CONTROL
// -------------------------
// Threshold untuk kondisi cahaya (ESP32 12-bit ADC: 0-4095)
#define LDR_THRESHOLD_TERANG 1365   // 0-1365 = Terang
#define LDR_THRESHOLD_REDUP  2730   // 1366-2730 = Redup
                                    // 2731-4095 = Gelap

// LDR timing & state
unsigned long previousMillisLDR = 0;
const long intervalLDR = 100;       // Baca LDR setiap 100ms

// LDR button debounce
unsigned long ldrButtonDebounceTime = 0;
const long ldrButtonDebounceDelay = 50;
int lastLdrButtonState = HIGH;
int ldrButtonState = HIGH;
unsigned long ldrButtonPressStart = 0;

// LDR state
int ldrValue = 100;
String ldrCondition = "";
bool ldrRelayState = false;
bool ldrManualMode = false;   // true = button mengontrol relay lampu

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
// MQTT CALLBACK (laporan)
// -------------------------
void callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    Serial.println("Failed to parse ML prediction!");
    return;
  }
  
  String topicStr = String(topic);
  
  // Check if it's DHT11 or Gas prediction
  if (topicStr == topic_prediction) {
    // DHT11 prediction
    String kondisi = doc["kondisi"];
    float confidence = doc["confidence"];
    
    mlPrediction = kondisi;
    mlConfidence = confidence;
    
    Serial.println("=== ML DHT11 PREDICTION ===");
    Serial.print("Kondisi: ");
    Serial.println(kondisi);
    Serial.print("Confidence: ");
    Serial.print(confidence);
    Serial.println("%");
  }
  else if (topicStr == topic_gas_prediction) {
    // Gas prediction
    String kondisi = doc["kondisi"];
    float confidence = doc["confidence"];
    
    mlGasPrediction = kondisi;
    mlGasConfidence = confidence;
    
    Serial.println("=== ML GAS PREDICTION ===");
    Serial.print("Kondisi: ");
    Serial.println(kondisi);
    Serial.print("Confidence: ");
    Serial.print(confidence);
    Serial.println("%");
  }
  else if (topicStr == topic_ldr_prediction) {
    // LDR prediction
    String kondisi = doc["kondisi"];
    float confidence = doc["confidence"];
    
    Serial.println("=== ML LDR PREDICTION ===");
    Serial.print("Kondisi: ");
    Serial.println(kondisi);
    Serial.print("Confidence: ");
    Serial.print(confidence);
    Serial.println("%");
    
    // Auto control relay based on ML prediction
    // Gelap = turn ON light, Terang/Redup = turn OFF (if in auto mode)
    if (!ldrManualMode) {
      if (kondisi == "Gelap" && !ldrRelayState) {
        ldrRelayState = true;
        digitalWrite(PIN_LDR_RELAY, HIGH);
        Serial.println("[ML] Light Relay ON (Gelap detected)");
      } else if (kondisi != "Gelap" && ldrRelayState) {
        ldrRelayState = false;
        digitalWrite(PIN_LDR_RELAY, LOW);
        Serial.println("[ML] Light Relay OFF (Terang/Redup detected)");
      }
    }
  }
}

// -------------------------
// MQTT RECONNECT
// -------------------------
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected!");
      
      // Subscribe to both prediction topics
      client.subscribe(topic_prediction);
      Serial.print("Subscribed: ");
      Serial.println(topic_prediction);
      
      client.subscribe(topic_gas_prediction);
      Serial.print("Subscribed: ");
      Serial.println(topic_gas_prediction);
      
      client.subscribe(topic_ldr_prediction);
      Serial.print("Subscribed: ");
      Serial.println(topic_ldr_prediction);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 2s");
      delay(2000);
    }
  }
}

// -------------------------
// SETUP (laporan) opsional
// -------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // ===== LCD STARTUP SEQUENCE =====
  
  // Step 1: Initialize LCD & Show Starting
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("SmartCage ML");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(1500);
  
  // Step 2: Initialize Sensors
  dht.begin();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Checking Sensor");
  lcd.setCursor(0, 1);
  lcd.print("DHT11 & MQ2...");
  delay(1000);
  
  // Setup pins
  pinMode(PIN_MERAH, OUTPUT);
  pinMode(PIN_KUNING, OUTPUT);
  pinMode(PIN_HIJAU, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_MQ2, INPUT);
  pinMode(PIN_BTN_DOOR, INPUT_PULLUP);  // Button with internal pull-up
  
  // LDR Control pins
  pinMode(PIN_LDR_RELAY, OUTPUT);
  pinMode(PIN_LDR_BUTTON, INPUT_PULLUP);  // Button with internal pull-up
  digitalWrite(PIN_LDR_RELAY, LOW);       // Start with light OFF
  
  // Setup Servo - start at closed position (180 degrees)
  doorServo.attach(PIN_SERVO);
  doorServo.write(DOOR_CLOSED_POS);
  currentServoPos = DOOR_CLOSED_POS;
  doorOpen = false;
  delay(500);
  
  // Default state
  digitalWrite(PIN_MERAH, LOW);
  digitalWrite(PIN_KUNING, LOW);
  digitalWrite(PIN_HIJAU, LOW);
  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  
  // Check DHT11
  float testTemp = dht.readTemperature();
  bool dhtOk = !isnan(testTemp);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(dhtOk ? "DHT11: OK" : "DHT11: FAIL");
  lcd.setCursor(0, 1);
  lcd.print("MQ2: OK");
  delay(1500);
  
  // Step 3: Setup OLED
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed!");
  }
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  
  // Step 4: Connect WiFi
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  lcd.setCursor(0, 1);
  lcd.print(ssid);
  
  WiFi.begin(ssid, password);
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 30) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1);
    lcd.print(ssid);
    delay(1500);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi FAILED!");
    delay(2000);
  }
  
  // Step 5: Connect MQTT
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting MQTT");
  lcd.setCursor(0, 1);
  lcd.print("broker.hivemq");
  
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  // Try MQTT connection
  int mqttAttempts = 0;
  while (!client.connected() && mqttAttempts < 5) {
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT connected!");
      client.subscribe(topic_prediction);
      client.subscribe(topic_gas_prediction);
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MQTT Connected!");
      lcd.setCursor(0, 1);
      lcd.print("Ready to GO!");
      delay(1500);
    } else {
      Serial.print("MQTT failed, rc=");
      Serial.println(client.state());
      mqttAttempts++;
      delay(1000);
    }
  }
  
  if (!client.connected()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("MQTT FAILED!");
    lcd.setCursor(0, 1);
    lcd.print("Retry in loop");
    delay(2000);
  }
  
  // Step 6: All Ready!
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready!");
  lcd.setCursor(0, 1);
  lcd.print("ML+Gas Active");
  delay(2000);
  lcd.clear();
}

// -------------------------
// LOOP UTAMA (laporan)
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

    // Read DHT11
    suhu = dht.readTemperature();
    kelembapan = dht.readHumidity();

    if (!isnan(suhu) && !isnan(kelembapan)) {
      Serial.print("DHT11: T=");
      Serial.print(suhu);
      Serial.print("C, H=");
      Serial.print(kelembapan);
      Serial.println("%");

      // Publish DHT11 data
      StaticJsonDocument<256> doc;
      doc["temp"] = suhu;
      doc["humidity"] = kelembapan;

      char payload[256];
      serializeJson(doc, payload);
      client.publish(topic_data, payload);
    }

    // Read MQ2 - LOW = Gas Detected (standard MQ2 digital output)
    // Most MQ2 modules: LOW when gas detected, HIGH when no gas
    gasDetected = (digitalRead(PIN_MQ2) == LOW);
    
    // Determine gas kondisi
    // Aman: No gas (MQ2 HIGH)
    // Waspada: Gas detected (MQ2 LOW)
    // Bahaya: Gas detected + Suhu > 55°C (kebakaran)
    if (!gasDetected) {
      gasKondisi = "Aman";
    } else if (gasDetected && suhu > 55.0) {
      gasKondisi = "Bahaya";
    } else {
      gasKondisi = "Waspada";
    }

    Serial.print("MQ2: Gas=");
    Serial.print(gasDetected ? "YES" : "NO");
    Serial.print(", Status=");
    Serial.println(gasKondisi);

    // Publish Gas data
    StaticJsonDocument<256> gasDoc;
    gasDoc["gas_detected"] = gasDetected;
    gasDoc["temp"] = suhu;
    gasDoc["kondisi_lokal"] = gasKondisi;

    char gasPayload[256];
    serializeJson(gasDoc, gasPayload);
    client.publish(topic_gas_data, gasPayload);

    // Update OLED
    updateOLED();
  }

  // ===================================================================
  // FASE 2: TRIGGER OUTPUT BERDASARKAN ML PREDICTION (DHT11)
  // ===================================================================
  if (mlGasPrediction != "Bahaya" && mlGasPrediction != "Waspada") {
    // Only handle DHT11 if gas is safe
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
      handleWaiting(currentMillis);
    }
  }

  // ===================================================================
  // FASE 3: TRIGGER OUTPUT BERDASARKAN ML PREDICTION (GAS)
  // Gas alerts override temperature alerts
  // ===================================================================
  if (mlGasPrediction == "Bahaya") {
    handleGasBahaya(currentMillis);
  }
  else if (mlGasPrediction == "Waspada") {
    handleGasWaspada(currentMillis);
  }

  // ===================================================================
  // FASE 4: UPDATE LCD
  // ===================================================================
  updateLCD(currentMillis);
  
  // ===================================================================
  // FASE 5: DOOR CONTROL (Button + Auto on Bahaya/Panas)
  // ===================================================================
  handleDoorControl(currentMillis);
  
  // ===================================================================
  // FASE 6: LDR LIGHT CONTROL (Button Manual + Auto)
  // ===================================================================
  handleLDRControl(currentMillis);
}

// ===================================================================
// FUNGSI HANDLER OUTPUT DHT11
// ===================================================================

void handleIdeal(unsigned long currentMillis) {
  digitalWrite(PIN_HIJAU, HIGH);
  digitalWrite(PIN_MERAH, LOW);
  digitalWrite(PIN_KUNING, LOW);
  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_BUZZER, LOW);
}

void handlePanas(unsigned long currentMillis) {
  digitalWrite(PIN_HIJAU, LOW);
  digitalWrite(PIN_KUNING, LOW);
  
  if (currentMillis - previousMillisBlink >= intervalBlink) {
    previousMillisBlink = currentMillis;
    ledBlinkState = !ledBlinkState;
    digitalWrite(PIN_MERAH, ledBlinkState);
  }
  
  digitalWrite(PIN_RELAY, LOW);
  
  if (currentMillis - previousMillisBuzzer >= intervalBuzzer) {
    previousMillisBuzzer = currentMillis;
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState);
  }
}

void handleDingin(unsigned long currentMillis) {
  digitalWrite(PIN_HIJAU, LOW);
  digitalWrite(PIN_MERAH, LOW);
  
  if (currentMillis - previousMillisBlink >= intervalBlink) {
    previousMillisBlink = currentMillis;
    ledBlinkState = !ledBlinkState;
    digitalWrite(PIN_KUNING, ledBlinkState);
  }
  
  digitalWrite(PIN_RELAY, HIGH);
  
  if (currentMillis - previousMillisBuzzer >= intervalBuzzer) {
    previousMillisBuzzer = currentMillis;
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState);
  }
}

void handleWaiting(unsigned long currentMillis) {
  digitalWrite(PIN_HIJAU, LOW);
  digitalWrite(PIN_MERAH, LOW);
  digitalWrite(PIN_KUNING, LOW);
  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_BUZZER, LOW);
}

// ===================================================================
// FUNGSI HANDLER OUTPUT GAS
// ===================================================================

void handleGasWaspada(unsigned long currentMillis) {
  digitalWrite(PIN_HIJAU, LOW);
  digitalWrite(PIN_MERAH, LOW);
  
  if (currentMillis - previousMillisGasBlink >= 400) {
    previousMillisGasBlink = currentMillis;
    gasLedBlinkState = !gasLedBlinkState;
    digitalWrite(PIN_KUNING, gasLedBlinkState);
  }
  
  if (currentMillis - previousMillisGasBuzzer >= 800) {
    previousMillisGasBuzzer = currentMillis;
    gasBuzzerState = !gasBuzzerState;
    digitalWrite(PIN_BUZZER, gasBuzzerState);
  }
}

void handleGasBahaya(unsigned long currentMillis) {
  digitalWrite(PIN_HIJAU, LOW);
  digitalWrite(PIN_KUNING, LOW);
  
  // LED Merah blink cepat
  if (currentMillis - previousMillisGasBlink >= 100) {
    previousMillisGasBlink = currentMillis;
    gasLedBlinkState = !gasLedBlinkState;
    digitalWrite(PIN_MERAH, gasLedBlinkState);
  }
  
  // Buzzer continuous
  digitalWrite(PIN_BUZZER, HIGH);
  
  // Relay ON (matikan kipas)
  digitalWrite(PIN_RELAY, HIGH);
}

// ===================================================================
// FUNGSI UPDATE DISPLAY
// ===================================================================

void updateOLED() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Smart Cage ML+Gas");
  oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  
  // DHT11 Info
  oled.setCursor(0, 13);
  oled.print("T:");
  oled.print(suhu, 1);
  oled.print("C H:");
  oled.print(kelembapan, 0);
  oled.println("%");
  
  oled.setCursor(0, 23);
  oled.print("ML:");
  oled.println(mlPrediction);
  
  oled.drawLine(0, 33, 127, 33, SSD1306_WHITE);
  
  // Gas Info
  oled.setCursor(0, 36);
  oled.print("Gas:");
  oled.print(gasDetected ? "YES" : "NO");
  oled.print(" ");
  oled.println(gasKondisi);
  
  oled.setCursor(0, 46);
  oled.print("ML:");
  oled.println(mlGasPrediction);
  
  oled.display();
}

void updateLCD(unsigned long currentMillis) {
  if (currentMillis - previousMillisLCD >= intervalLCD) {
    previousMillisLCD = currentMillis;
    
    // Rotate between 3 screens: DHT11, Gas, LDR (every 2 seconds)
    int screenNum = (currentMillis / 2000) % 3;
    
    String line1, line2;
    
    if (screenNum == 0) {
      // Screen 1: DHT11 Temperature & Humidity
      line1 = "T:" + String(suhu, 1) + "C H:" + String(kelembapan, 0) + "%";
      line2 = "ML:" + mlPrediction;
    } 
    else if (screenNum == 1) {
      // Screen 2: Gas Sensor
      line1 = "Gas:" + String(gasDetected ? "Y" : "N") + " " + gasKondisi;
      line2 = "ML:" + mlGasPrediction;
    }
    else {
      // Screen 3: LDR Light Control Status
      String ldrMode = ldrManualMode ? "MANUAL" : "AUTO";
      line1 = "Light:" + ldrMode;
      line2 = "LDR:" + String(ldrValue) + " " + (ldrRelayState ? "ON" : "OFF");
    }
    
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

// ===================================================================
// FUNGSI DOOR CONTROL (SERVO) - Smooth Movement
// ===================================================================

// Open door: move servo from 180 to 0 degrees (smooth)
void openDoor() {
  if (!doorOpen) {
    Serial.println("Opening door... (160 -> 0)");
    for (int pos = currentServoPos; pos >= DOOR_OPEN_POS; pos--) {
      doorServo.write(pos);
      delay(15);  // Smooth movement speed
    }
    currentServoPos = DOOR_OPEN_POS;
    doorOpen = true;
    Serial.println("Door OPENED");
  }
}

// Close door: move servo from 0 to 180 degrees (smooth)
void closeDoor() {
  if (doorOpen) {
    Serial.println("Closing door... (0 -> 160)");
    for (int pos = currentServoPos; pos <= DOOR_CLOSED_POS; pos++) {
      doorServo.write(pos);
      delay(15);  // Smooth movement speed
    }
    currentServoPos = DOOR_CLOSED_POS;
    doorOpen = false;
    Serial.println("Door CLOSED");
  }
}

// Main door control handler
void handleDoorControl(unsigned long currentMillis) {
  // ===== MANUAL BUTTON CHECK =====
  // Button is active LOW (INPUT_PULLUP: pressed = LOW)
  if (digitalRead(PIN_BTN_DOOR) == LOW) {
    // Debounce check - only trigger once per press
    if (currentMillis - lastButtonPress >= debounceDelay) {
      lastButtonPress = currentMillis;
      
      // Toggle door state
      if (doorOpen) {
        closeDoor();
      } else {
        openDoor();
      }
    }
  }
  
  // ===== AUTOMATIC OPEN ON BAHAYA OR PANAS =====
  // Auto-open door for emergency (Bahaya gas or Panas temperature)
  if (!doorOpen) {
    if (mlGasPrediction == "Bahaya" || mlPrediction == "Panas") {
      Serial.println("AUTO: Emergency detected - Opening door!");
      openDoor();
    }
  }
}

// ===================================================================
// FUNGSI LDR LIGHT CONTROL (Button Priority + Auto)
// ===================================================================

void handleLDRControl(unsigned long currentMillis) {
  // ===== HANDLE BUTTON DENGAN DEBOUNCE =====
  int reading = digitalRead(PIN_LDR_BUTTON);
  
  if (reading != lastLdrButtonState) {
    ldrButtonDebounceTime = currentMillis;
  }
  
  if ((currentMillis - ldrButtonDebounceTime) > ldrButtonDebounceDelay) {
    if (reading != ldrButtonState) {
      ldrButtonState = reading;
      
      // Button ditekan (LOW karena INPUT_PULLUP)
      if (ldrButtonState == LOW) {
        // Aktifkan manual mode dan toggle relay
        ldrManualMode = true;
        ldrRelayState = !ldrRelayState;  // Toggle ON/OFF
        digitalWrite(PIN_LDR_RELAY, ldrRelayState ? HIGH : LOW);
        
        Serial.println("\n****************************************");
        Serial.println(">>> LDR BUTTON PRESSED - MANUAL MODE");
        Serial.print(">>> Light Relay: ");
        Serial.println(ldrRelayState ? "ON" : "OFF");
        Serial.println("****************************************\n");
      }
    }
  }
  lastLdrButtonState = reading;
  
  // ===== LONG PRESS UNTUK KEMBALI KE AUTO MODE =====
  if (ldrButtonState == LOW) {
    if (ldrButtonPressStart == 0) {
      ldrButtonPressStart = currentMillis;
    } else if ((currentMillis - ldrButtonPressStart) > 3000) {
      // Long press 3 detik -> kembali ke Auto
      ldrManualMode = false;
      ldrButtonPressStart = 0;
      Serial.println("\n========================================");
      Serial.println(">>> LDR KEMBALI KE AUTO MODE");
      Serial.println("========================================\n");
    }
  } else {
    ldrButtonPressStart = 0;
  }
  
  // ===== BACA LDR DAN KONTROL RELAY =====
  if (currentMillis - previousMillisLDR >= intervalLDR) {
    previousMillisLDR = currentMillis;
    
    ldrValue = analogRead(PIN_LDR);
    
    bool ldrWantsRelayOn;
    
    if (ldrValue <= LDR_THRESHOLD_TERANG) {
      ldrCondition = "Terang";
      ldrWantsRelayOn = false;  // Terang = Lampu OFF
    } 
    else if (ldrValue <= LDR_THRESHOLD_REDUP) {
      ldrCondition = "Redup";
      ldrWantsRelayOn = false;  // Redup = Lampu OFF
    } 
    else {
      ldrCondition = "Gelap";
      ldrWantsRelayOn = true;   // Gelap = Lampu ON
    }
    
    // Update relay HANYA jika AUTO mode (bukan manual)
    if (!ldrManualMode) {
      if (ldrWantsRelayOn != ldrRelayState) {
        ldrRelayState = ldrWantsRelayOn;
        digitalWrite(PIN_LDR_RELAY, ldrRelayState ? HIGH : LOW);
        
        Serial.println("----------------------------------------");
        Serial.print("[LDR AUTO] Light Relay ");
        Serial.println(ldrRelayState ? "ON! (Gelap)" : "OFF!");
        Serial.println("----------------------------------------");
      }
    }
    
    // Print status setiap 1 detik (10 iterasi @ 100ms)
    static int ldrPrintCount = 0;
    ldrPrintCount++;
    if (ldrPrintCount >= 10) {
      ldrPrintCount = 0;
      Serial.print("[LDR ");
      Serial.print(ldrManualMode ? "MANUAL" : "AUTO");
      Serial.print("] Val:");
      Serial.print(ldrValue);
      Serial.print(" | ");
      Serial.print(ldrCondition);
      Serial.print(" | Light:");
      Serial.println(ldrRelayState ? "ON" : "OFF");
      
      // Publish LDR data to MQTT
      StaticJsonDocument<256> ldrDoc;
      ldrDoc["ldr_value"] = ldrValue;
      ldrDoc["kondisi_lokal"] = ldrCondition;
      ldrDoc["manual_mode"] = ldrManualMode;
      ldrDoc["relay_state"] = ldrRelayState;
      
      char ldrPayload[256];
      serializeJson(ldrDoc, ldrPayload);
      client.publish(topic_ldr_data, ldrPayload);
    }
  }
}
