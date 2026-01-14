/*
 * Arduino Uno - LDR + Button Toggle Relay
 * 
 * LDR: Pin A0 (Analog Input)
 * Button: Pin 2 (Toggle ON/OFF dengan prioritas)
 * Relay: Pin 7
 * 
 * Kondisi Cahaya (Arduino ADC 0-1023):
 *   - Terang: 0-340 (Relay OFF)
 *   - Redup: 341-680 (Relay OFF)
 *   - Gelap: 681-1023 (Relay ON)
 * 
 * Button:
 *   - Klik -> Toggle Relay ON/OFF (prioritas di atas LDR)
 *   - Saat manual aktif, LDR tidak mengontrol relay
 *   - Tahan 3 detik -> Kembali ke auto mode
 * 
 * Note: LDR value 0 = Terang, 1023 = Gelap
 */

#define LDR_PIN 23
#define BUTTON_PIN 5
#define RELAY_PIN 27

// Threshold untuk kondisi cahaya
#define THRESHOLD_TERANG 340
#define THRESHOLD_REDUP 680

// Timing
unsigned long previousMillis = 0;
const long readInterval = 100;

// Button debounce
unsigned long lastDebounceTime = 0;
const long debounceDelay = 50;
int lastButtonState = HIGH;
int buttonState = HIGH;

// State
String lastCondition = "";
bool relayState = false;

// Prioritas Button
bool buttonManualMode = false;  // true = button mengontrol relay

void setup() {
  Serial.begin(9600);
  
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);
  
  Serial.println("========================================");
  Serial.println("  LDR + Button Toggle Relay");
  Serial.println("========================================");
  Serial.println("LDR Auto Mode:");
  Serial.println("  Terang (0-340)   -> Relay OFF");
  Serial.println("  Redup  (341-680) -> Relay OFF");
  Serial.println("  Gelap  (681-1023)-> Relay ON");
  Serial.println("----------------------------------------");
  Serial.println("Button (Pin 2):");
  Serial.println("  Klik -> Toggle Relay ON/OFF");
  Serial.println("  Tahan 3 detik -> Kembali Auto Mode");
  Serial.println("========================================\n");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // === Handle Button dengan Debounce ===
  int reading = digitalRead(BUTTON_PIN);
  
  if (reading != lastButtonState) {
    lastDebounceTime = currentMillis;
  }
  
  if ((currentMillis - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      
      // Button ditekan (LOW karena INPUT_PULLUP)
      if (buttonState == LOW) {
        // Aktifkan manual mode dan toggle relay
        buttonManualMode = true;
        relayState = !relayState;  // Toggle ON/OFF
        digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
        
        Serial.println("\n****************************************");
        Serial.println(">>> BUTTON PRESSED - MANUAL MODE");
        Serial.print(">>> Relay: ");
        Serial.println(relayState ? "ON" : "OFF");
        Serial.println("****************************************\n");
      }
    }
  }
  lastButtonState = reading;
  
  // === Long press untuk kembali ke Auto Mode ===
  static unsigned long buttonPressStart = 0;
  if (buttonState == LOW) {
    if (buttonPressStart == 0) {
      buttonPressStart = currentMillis;
    } else if ((currentMillis - buttonPressStart) > 3000) {
      // Long press 3 detik -> kembali ke Auto
      buttonManualMode = false;
      buttonPressStart = 0;
      Serial.println("\n========================================");
      Serial.println(">>> KEMBALI KE AUTO MODE (LDR Control)");
      Serial.println("========================================\n");
    }
  } else {
    buttonPressStart = 0;
  }
  
  // === Baca LDR dan kontrol relay ===
  if (currentMillis - previousMillis >= readInterval) {
    previousMillis = currentMillis;
    
    int ldrValue = analogRead(LDR_PIN);
    
    String condition;
    bool ldrWantsRelayOn;
    
    if (ldrValue <= THRESHOLD_TERANG) {
      condition = "Terang";
      ldrWantsRelayOn = false;
    } 
    else if (ldrValue <= THRESHOLD_REDUP) {
      condition = "Redup";
      ldrWantsRelayOn = false;
    } 
    else {
      condition = "Gelap";
      ldrWantsRelayOn = true;
    }
    
    // Update relay HANYA jika AUTO mode (bukan manual)
    if (!buttonManualMode) {
      if (ldrWantsRelayOn != relayState) {
        relayState = ldrWantsRelayOn;
        digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
        
        Serial.println("----------------------------------------");
        Serial.print("[AUTO] RELAY ");
        Serial.println(relayState ? "ON! (Gelap)" : "OFF!");
        Serial.println("----------------------------------------");
      }
    }
    
    // Print status
    Serial.print("[");
    Serial.print(buttonManualMode ? "MANUAL" : "AUTO");
    Serial.print("] LDR: ");
    Serial.print(ldrValue);
    Serial.print(" | ");
    Serial.print(condition);
    Serial.print(" | Relay: ");
    Serial.println(relayState ? "ON" : "OFF");
    
    lastCondition = condition;
  }
}
