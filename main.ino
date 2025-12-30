#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi credentials
const char* ssid = "---";
const char* password = "12345678";

// ThingsBoard
const char* mqtt_server = "-";
const char* token = "-";
WiFiClient espClient;
PubSubClient client(espClient);

// Pin definitions
#define ONE_WIRE_BUS 33
#define RESET_BUTTON 25
#define BUZZER 26
#define LED_BATTERY 14
#define VOLTAGE_PIN 34

// TFT Display pins (defined in User_Setup.h or here)
TFT_eSPI tft = TFT_eSPI();

// ADS1115 setup
Adafruit_ADS1115 ads;

// DS18B20 setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Voltage divider constants (68K and 33K)
const float R1 = 14700.0;
const float R2 = 10000.0;
const float VREF = 3.3;
const float LOW_BATTERY_THRESHOLD = 6.50;

// ===== BATTERY PERCENTAGE CONSTANTS =====
const float BATTERY_MAX_VOLTAGE = 8.4;  // 100% battery
const float BATTERY_MIN_VOLTAGE = 6.0;  // 0% battery

// ===== CALIBRATION CONSTANTS FOR pH (ADS1115 16-bit) =====
const float pH_slope = -0.0860;           // V per pH (from calibration)
const float pH_intercept = 4.33986;       // Intercept from calibration
const float pH_offset = 0.70;             // Final offset correction
const float ADS_VREF = 4.096;             // ADS1115 reference voltage (GAIN_ONE)

// ***** UBAH NILAI INI UNTUK KALIBRASI pH 7 *****
const float pH7_voltage = 3.3;            // Tegangan saat pH 7 (dalam Volt)
// ************************************************

// Global variables
float temperature = 0.0;
float pH_value = 0.0;
float turbidity = 0.0;
float tds_value = 0.0;
float battery_voltage = 0.0;
int battery_percentage = 0;  // ===== VARIABLE BARU UNTUK PERSENTASE BATERAI =====
bool iot_enabled = true;
bool is_suitable = false;

// Stability check variables
float prev_temp = 0, prev_pH = 0, prev_turb = 0, prev_tds = 0;
unsigned long stability_start = 0;
bool is_stable = false;
const int STABILITY_TOLERANCE = 5;
const int STABILITY_DURATION = 20000;

// Timing variables for realtime upload
unsigned long last_upload_time = 0;
const unsigned long UPLOAD_INTERVAL = 1000;

// State machine
enum SystemState {
  STATE_READING,
  STATE_EVALUATING,
  STATE_RESULT_DISPLAY
};
SystemState current_state = STATE_READING;

// ===== RPC CALLBACK FUNCTION =====
void onRpcMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("RPC received on topic: ");
  Serial.println(topic);
  
  // Parse payload
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Message: ");
  Serial.println(message);
  
  // Check if it's a reset command
  if (message.indexOf("\"method\":\"reset\"") > -1) {
    Serial.println("Reset command received via RPC!");
    performReset();
  }
}

// ===== RESET FUNCTION =====
void performReset() {
  Serial.println("Performing system reset...");
  
  // Reset state variables
  is_stable = false;
  stability_start = 0;
  current_state = STATE_READING;
  
  // Clear display
  tft.fillScreen(TFT_BLACK);
  
  // Optional: Send response back to ThingsBoard
  if (client.connected()) {
    String response = "{\"reset\":\"success\"}";
    client.publish("v1/devices/me/rpc/response/1", response.c_str());
  }
  
  Serial.println("Reset complete, returning to reading state");
}

// ===== FUNGSI UNTUK MENGHITUNG PERSENTASE BATERAI =====
int calculateBatteryPercentage(float voltage) {
  // Konversi tegangan ke persentase (0-100%)
  // 8.4V = 100%, 6.0V = 0%
  float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0;
  
  // Batasi nilai antara 0-100
  if (percentage > 100.0) percentage = 100.0;
  if (percentage < 0.0) percentage = 0.0;
  
  return (int)percentage;
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Drinking Water Suitability Detection System ===");
  
  // Pin modes
  pinMode(RESET_BUTTON, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_BATTERY, OUTPUT);
  digitalWrite(BUZZER, LOW);
  digitalWrite(LED_BATTERY, LOW);
  
  // Initialize TFT
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  
  // Display title
  displayTitle();
  delay(2000);
  
  // Initialize sensors
  initializeSensors();
  
  // Connect to WiFi (3 attempts)
  connectWiFi();
  
  // Main loop will handle the rest
}

void loop() {
  // Maintain MQTT connection
  if (iot_enabled) {
    if (!client.connected()) {
      reconnectMQTT();
    }
    client.loop();
  }
  
  // Read battery voltage
  checkBattery();
  
  // Read all sensors
  readSensors();
  
  // Upload to ThingsBoard every 1 second (realtime)
  if (iot_enabled && client.connected()) {
    unsigned long current_time = millis();
    if (current_time - last_upload_time >= UPLOAD_INTERVAL) {
      uploadToThingsBoard();
      last_upload_time = current_time;
    }
  }
  
  // State machine handling
  switch (current_state) {
    case STATE_READING:
      // Display sensor readings
      displaySensorReadings();
      
      // Check for stability
      if (!is_stable) {
        if (checkStability()) {
          is_stable = true;
          Serial.println("Data stable, evaluating...");
          current_state = STATE_EVALUATING;
          delay(500);
        }
      }
      break;
      
    case STATE_EVALUATING:
      // Evaluate water suitability
      evaluateWater();
      
      // Display result
      displayResult();
      
      // Play buzzer based on result
      playBuzzer();
      
      // Move to result display state
      current_state = STATE_RESULT_DISPLAY;
      Serial.println("Waiting for reset button...");
      break;
      
    case STATE_RESULT_DISPLAY:
      // Wait for reset button (physical button)
      if (digitalRead(RESET_BUTTON) == LOW) {
        // Reset pressed
        Serial.println("Reset button pressed, restarting...");
        delay(300); // Debounce
        performReset();
      }
      break;
  }
  
  delay(100);
}

void displayTitle() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Drinking Water", 120, 100);
  tft.drawString("Suitability", 120, 130);
  tft.drawString("Detection System", 120, 160);
}

void initializeSensors() {
  bool success = false;
  
  while (!success) {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Inisialisasi...", 120, 120);
    Serial.println("Initializing sensors...");
    
    // Initialize I2C
    Wire.begin(21, 22);
    
    // Initialize ADS1115
    if (!ads.begin(0x49)) {
      Serial.println("Failed to initialize ADS1115!");
      delay(1000);
      continue;
    }
    Serial.println("ADS1115 initialized");
    ads.setGain(GAIN_ONE);  // +/- 4.096V range
    
    // Initialize DS18B20
    sensors.begin();
    if (sensors.getDeviceCount() == 0) {
      Serial.println("No DS18B20 found!");
      delay(1000);
      continue;
    }
    Serial.println("DS18B20 initialized");
    
    success = true;
  }
  
  Serial.println("All sensors initialized successfully");
  delay(500);
}

void connectWiFi() {
  int attempts = 0;
  WiFi.mode(WIFI_STA);
  
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Connecting WiFi...", 120, 120);
  Serial.println("Connecting to WiFi...");
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED && attempts < 3) {
    delay(3000);
    attempts++;
    Serial.print("Attempt ");
    Serial.print(attempts);
    Serial.println("/3");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    
    // Setup MQTT
    client.setServer(mqtt_server, 1883);
    
    // ===== SET RPC CALLBACK =====
    client.setCallback(onRpcMessage);
    
    reconnectMQTT();
    
    iot_enabled = true;
  } else {
    Serial.println("WiFi connection failed, IoT disabled");
    tft.fillScreen(TFT_BLACK);
    tft.drawString("IoT disabled", 120, 120);
    delay(2000);
    iot_enabled = false;
  }
}

void reconnectMQTT() {
  if (client.connect("ESP32Client", token, NULL)) {
    Serial.println("Connected to ThingsBoard");
    
    // ===== SUBSCRIBE TO RPC TOPIC =====
    client.subscribe("v1/devices/me/rpc/request/+");
    Serial.println("Subscribed to RPC topic");
  } else {
    Serial.println("Failed to connect to ThingsBoard");
  }
}

void checkBattery() {
  int raw_adc = analogRead(VOLTAGE_PIN);
  float adc_voltage = (raw_adc / 4095.0) * VREF;
  battery_voltage = adc_voltage * ((R1 + R2) / R2);
  
  // ===== HITUNG PERSENTASE BATERAI =====
  battery_percentage = calculateBatteryPercentage(battery_voltage);
  
  // Debug output
  Serial.print("Battery Voltage: ");
  Serial.print(battery_voltage);
  Serial.print("V | Percentage: ");
  Serial.print(battery_percentage);
  Serial.println("%");
  
  if (battery_voltage < LOW_BATTERY_THRESHOLD) {
    digitalWrite(LED_BATTERY, HIGH);
  } else {
    digitalWrite(LED_BATTERY, LOW);
  }
}

void readSensors() {
  // Read temperature
  sensors.requestTemperatures();
  temperature = sensors.getTempCByIndex(0);
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");
  
  // ===== READ pH FROM ADS1115 A0 (16-bit) =====
  int16_t adc0 = ads.readADC_SingleEnded(0);  // Raw 16-bit value (0-32767)
  
  // Convert ADC value to voltage using ADS1115 reference
  float voltage_pH = ads.computeVolts(adc0);  // Uses ADS_VREF (4.096V)
  
  // Apply calibration: voltage → pH
  float pH_raw = (voltage_pH - pH_intercept) / pH_slope;
  
  // Apply offset correction
  pH_value = pH_raw - pH_offset;
  
  // Optional: Apply calibration shift based on pH 7 reference
  float pH7_expected = (pH7_voltage - pH_intercept) / pH_slope - pH_offset;
  float pH_correction = 7.0 - pH7_expected;  // Shift to make pH7_voltage = pH 7.0
  pH_value += pH_correction;
  
  Serial.print("pH ADC: ");
  Serial.print(adc0);
  Serial.print(" | Voltage: ");
  Serial.print(voltage_pH, 4);
  Serial.print("V | pH: ");
  Serial.println(pH_value, 2);
  
  // Read Turbidity from ADS1115 A1
  int16_t adc1 = ads.readADC_SingleEnded(1);
  // Map from 16-bit range: 0-10400 (approx for 640 in 10-bit scaled to 16-bit)
  turbidity = map(adc1, 0, 10400, 100, 0);
  turbidity = constrain(turbidity, 0, 100);
  Serial.print("Turbidity ADC: ");
  Serial.print(adc1);
  Serial.print(" | Turbidity: ");
  Serial.print(turbidity);
  Serial.println(" NTU");
  
  // Read TDS from ADS1115 A2
  int16_t adc2 = ads.readADC_SingleEnded(2);
  float voltage_tds = ads.computeVolts(adc2);
  float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
  float compensationVoltage = voltage_tds / compensationCoefficient;
  tds_value = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage 
              - 255.86 * compensationVoltage * compensationVoltage 
              + 857.39 * compensationVoltage) * 0.5;
  Serial.print("TDS ADC: ");
  Serial.print(adc2);
  Serial.print(" | Voltage: ");
  Serial.print(voltage_tds);
  Serial.print("V | TDS: ");
  Serial.print(tds_value);
  Serial.println(" ppm");
}

void displaySensorReadings() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextDatum(TL_DATUM);
  
  tft.drawString("Temp: " + String(temperature, 1) + " C", 10, 20);
  tft.drawString("pH: " + String(pH_value, 2), 10, 60);
  tft.drawString("Turb: " + String(turbidity, 1) + " NTU", 10, 100);
  tft.drawString("TDS: " + String(tds_value, 0) + " ppm", 10, 140);
  
  if (!is_stable) {
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Stabilizing...", 120, 200);
  }
  
  // Display low battery warning if needed
  if (battery_voltage < LOW_BATTERY_THRESHOLD) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Low Battery", 120, 230);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }
}

bool checkStability() {
  if (abs(temperature - prev_temp) <= STABILITY_TOLERANCE &&
      abs(pH_value - prev_pH) <= STABILITY_TOLERANCE &&
      abs(turbidity - prev_turb) <= STABILITY_TOLERANCE &&
      abs(tds_value - prev_tds) <= STABILITY_TOLERANCE) {
    
    if (stability_start == 0) {
      stability_start = millis();
    } else if (millis() - stability_start >= STABILITY_DURATION) {
      return true;
    }
  } else {
    stability_start = 0;
  }
  
  prev_temp = temperature;
  prev_pH = pH_value;
  prev_turb = turbidity;
  prev_tds = tds_value;
  
  return false;
}

void evaluateWater() {
  // WHO Standards:
  // pH: 6.5 - 8.5
  // Turbidity: < 5 NTU
  // TDS: < 600 ppm (recommended), < 1000 ppm (acceptable)
  // Temperature: not a direct health concern but affects taste
  
  is_suitable = true;
  
  if (pH_value < 6.5 || pH_value > 8.5) {
    is_suitable = false;
    Serial.println("FAIL: pH out of range");
  }
  
  if (turbidity > 5) {
    is_suitable = false;
    Serial.println("FAIL: Turbidity too high");
  }
  
  if (tds_value > 1000) {
    is_suitable = false;
    Serial.println("FAIL: TDS too high");
  }
  
  Serial.print("Water suitability: ");
  Serial.println(is_suitable ? "LAYAK" : "TIDAK LAYAK");
}

void displayResult() {
  tft.fillScreen(TFT_BLACK);
  
  if (is_suitable) {
    // Green border
    tft.drawRect(0, 0, 240, 240, TFT_GREEN);
    tft.drawRect(1, 1, 238, 238, TFT_GREEN);
    tft.drawRect(2, 2, 236, 236, TFT_GREEN);
    
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(4);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("LAYAK", 120, 120);
  } else {
    // Red border
    tft.drawRect(0, 0, 240, 240, TFT_RED);
    tft.drawRect(1, 1, 238, 238, TFT_RED);
    tft.drawRect(2, 2, 236, 236, TFT_RED);
    
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("TIDAK", 120, 110);
    tft.drawString("LAYAK", 120, 140);
  }
  
  // Display low battery warning if needed
  if (battery_voltage < LOW_BATTERY_THRESHOLD) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Low Battery", 120, 230);
  }
}

void playBuzzer() {
  if (is_suitable) {
    // One long beep
    digitalWrite(BUZZER, HIGH);
    delay(1000);
    digitalWrite(BUZZER, LOW);
  } else {
    // Three short beeps
    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER, HIGH);
      delay(200);
      digitalWrite(BUZZER, LOW);
      delay(200);
    }
  }
}

void uploadToThingsBoard() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  
  if (client.connected()) {
    String payload = "{";
    payload += "\"temperature\":" + String(temperature, 2) + ",";
    payload += "\"pH\":" + String(pH_value, 2) + ",";
    payload += "\"turbidity\":" + String(turbidity, 2) + ",";
    payload += "\"tds\":" + String(tds_value, 2) + ",";
    payload += "\"battery\":" + String(battery_voltage, 2) + ",";
    // ===== TAMBAHKAN PERSENTASE BATERAI KE PAYLOAD =====
    payload += "\"battery_percentage\":" + String(battery_percentage) + ",";
    payload += "\"suitable\":";
    payload += is_suitable ? "true" : "false";
    payload += "}";
    
    Serial.println("Uploading to ThingsBoard: " + payload);
    client.publish("v1/devices/me/telemetry", payload.c_str());
  }
}