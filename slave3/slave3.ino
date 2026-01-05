#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>

// ===============================
// Konfigurasi ESP-NOW
// ===============================
uint8_t masterAddress[] = {0x88, 0x57, 0x21, 0x94, 0x69, 0x10}; 

typedef struct struct_message {
    int senderID;
    int light;
    int soil;
    int danger; // 0: Aman, 1: Overvoltage
    int door;
    float current;
    float energy; // Digunakan untuk mengirim data Voltage (Vin)
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// ===============================
// Notes Definitions
// ===============================
#define a3f 208
#define b3f 233
#define b3 247
#define c4 261
#define c4s 277
#define d4 294
#define e4f 311
#define e4 329
#define f4 349
#define g4 392
#define a4f 415
#define a4 440
#define b4f 466
#define b4 493
#define c5 523
#define c5s 554
#define d5 587
#define e5f 622
#define e5 659
#define f5 698
#define f5s 740
#define a5f 831
#define rest 0

// ===============================
// Global Variables & Mutex
// ===============================
SemaphoreHandle_t xMutex;

// Voltmeter Data
float vout = 0.0;
float vin  = 0.0;
int adcValue = 0;
bool overVoltageDetected = false;

// Pins & Config
const int ANALOG_INPUT = 34;
const int RELAY_PIN = 18;
const int BUZZER_PIN = 19;
const float R1 = 30000.0;
const float R2 = 7500.0;
const float VOLTAGE_THRESHOLD = 5.0;

// Music Config
int beatlength = 120;
float beatseparationconstant = 0.3;

// WiFi & Web Server Config
const char *ssid = "ESP32-Voltmeter";
const char *password = "12345678";
WebServer server(80);

// ===============================
// Song Data
// ===============================
int telolet_melody[] = {
  e4, g4, a4, a4,
  a4, g4, e4, g4,
  a4, c5, b4, a4,
  g4, e4, g4, a4,

  e4, g4, a4, a4,
  a4, g4, e4, g4,
  a4, c5, d5, c5,
  b4, a4, g4, rest
};

int telolet_rhythm[] = {
  2,2,4,2,
  2,2,2,4,
  2,2,2,4,
  2,2,2,4,

  2,2,4,2,
  2,2,2,4,
  2,2,2,4,
  2,4,6,4
};

// ===============================
// Web Page (HTML + JS)
// ===============================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Voltmeter</title>
  <style>
    body { font-family: Arial; text-align: center; background-color: #121212; color: #ffffff; }
    h2 { color: #03dac6; }
    .card { background-color: #1e1e1e; max-width: 400px; margin: 0 auto; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px 0 rgba(0,0,0,0.2); }
    .value { font-size: 3rem; font-weight: bold; color: #bb86fc; }
    .unit { font-size: 1.5rem; color: #aaa; }
    .status-ok { color: #03dac6; font-weight: bold; }
    .status-err { color: #cf6679; font-weight: bold; }
    button { background-color: #03dac6; color: black; border: none; padding: 15px 32px; text-align: center; font-size: 16px; margin: 20px 0; cursor: pointer; border-radius: 5px; }
    button:active { background-color: #018786; }
  </style>
</head>
<body>
  <h2>ESP32 RTOS Monitor</h2>
  <div class="card">
    <p>Input Voltage</p>
    <div class="value"><span id="volt">--</span> <span class="unit">V</span></div>
    <p>Status: <span id="status">Checking...</span></p>
    <p>Relay: <span id="relay">--</span></p>
    <button onclick="resetLatch()">RESET LATCH</button>
  </div>
<script>
setInterval(function() {
  fetch('/data').then(response => response.json()).then(data => {
    document.getElementById('volt').innerText = data.voltage.toFixed(2);
    document.getElementById('relay').innerText = data.relay;
    
    let statusSpan = document.getElementById('status');
    if(data.latch) {
      statusSpan.innerText = "OVERVOLTAGE DETECTED!";
      statusSpan.className = "status-err";
    } else {
      statusSpan.innerText = "Normal";
      statusSpan.className = "status-ok";
    }
  });
}, 1000);

function resetLatch() {
  fetch('/reset').then(response => { console.log("Reset Sent"); });
}
</script>
</body>
</html>
)rawliteral";

// ===============================
// Helper Functions
// ===============================

// Play Note (Adapted for RTOS)
void playNote(int freq, int duration) {
  if (freq > 0) {
    ledcWriteTone(BUZZER_PIN, freq);
  } else {
    ledcWriteTone(BUZZER_PIN, 0);
  }
  
  // Use vTaskDelay instead of delay to allow other tasks to run
  vTaskDelay(pdMS_TO_TICKS(duration));
  
  ledcWriteTone(BUZZER_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(duration * beatseparationconstant));
}

// Play Song (Adapted for RTOS)
void playSong(int *melody, int *rhythm, int length) {
  for (int i = 0; i < length; i++) {
    // Check if we still need to play inside the loop 
    // If user hit Reset on Web, stop playing early!
    bool shouldStop = false;
    xSemaphoreTake(xMutex, portMAX_DELAY);
    if (!overVoltageDetected) shouldStop = true;
    xSemaphoreGive(xMutex);
    
    if (shouldStop) break;

    playNote(melody[i], rhythm[i] * beatlength);
  }
}

// ===============================
// Web Server Handlers
// ===============================
void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleData() {
  float currentVin;
  bool currentLatch;
  int currentRelay;

  xSemaphoreTake(xMutex, portMAX_DELAY);
  currentVin = vin;
  currentLatch = overVoltageDetected;
  xSemaphoreGive(xMutex);
  currentRelay = digitalRead(RELAY_PIN);

  String json = "{";
  json += "\"voltage\":" + String(currentVin);
  json += ", \"latch\":" + String(currentLatch ? "true" : "false");
  json += ", \"relay\":\"" + String(currentRelay ? "ON" : "OFF") + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleReset() {
  xSemaphoreTake(xMutex, portMAX_DELAY);
  overVoltageDetected = false; // Clear the latch
  digitalWrite(RELAY_PIN, HIGH); // Turn Relay back ON
  xSemaphoreGive(xMutex);
  
  server.send(200, "text/plain", "OK");
  Serial.println("System Reset via Web Interface");
}

// ===============================
// RTOS Tasks
// ===============================

// Task 1: Voltage Monitoring & Relay Control
void TaskMonitor(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    int localAdc = analogRead(ANALOG_INPUT);
    float localVout = (localAdc * 3.3) / 4095.0;
    float localVin = localVout * ((R1 + R2) / R2);

    // Protect global variable updates
    xSemaphoreTake(xMutex, portMAX_DELAY);
    adcValue = localAdc;
    vin = localVin;
    
    // Check Threshold
    if (vin > VOLTAGE_THRESHOLD && !overVoltageDetected) {
      overVoltageDetected = true;     // Latch the fault
      digitalWrite(RELAY_PIN, LOW);   // Force relay OFF immediately
    }
    xSemaphoreGive(xMutex);

    // Run this check frequently (e.g., every 50ms)
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// Task 2: Alarm / Music Player
void TaskAlarm(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    bool localLatchedState = false;

    // Read state safely
    xSemaphoreTake(xMutex, portMAX_DELAY);
    localLatchedState = overVoltageDetected;
    xSemaphoreGive(xMutex);

    if (localLatchedState) {
      // Ensure relay is off (redundancy)
      digitalWrite(RELAY_PIN, LOW);
      
      // Play Song
      playSong(
        telolet_melody,
        telolet_rhythm,
        sizeof(telolet_melody) / sizeof(int)
      );
    } else {
      // If no alarm, just wait a bit
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
}

// Task 3: Serial Telemetry
void TaskTelemetry(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    int printAdc;
    float printVin;
    bool printLatch;
    int printRelayState;
    int printBuzzerState;

    xSemaphoreTake(xMutex, portMAX_DELAY);
    printAdc = adcValue;
    printVin = vin;
    printLatch = overVoltageDetected;
    xSemaphoreGive(xMutex);

    printRelayState = digitalRead(RELAY_PIN);
    printBuzzerState = digitalRead(BUZZER_PIN);

    Serial.print("ADC: ");
    Serial.print(printAdc);
    Serial.print(" | Voltage: ");
    Serial.print(printVin, 4);
    Serial.print(" V | Relay: ");
    Serial.print(printRelayState ? "ON" : "OFF");
    Serial.print(" | Buzzer: ");
    Serial.print(printBuzzerState ? "ON" : "OFF");
    Serial.print(" | LATCH: ");
    Serial.println(printLatch ? "TRIGGERED" : "NORMAL");

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// Task 4: Web Server Handler
void TaskWeb(void *pvParameters) {
  (void) pvParameters;
  
  for(;;) {
    server.handleClient();
    // Small delay to prevent Watchdog trigger and allow idle task
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Task 5: Komunikasi ESP-NOW (Pengiriman ke Master)
void TaskCommunication(void *pvParameters) {
    for (;;) {
        xSemaphoreTake(xMutex, portMAX_DELAY);
        myData.senderID = 3;
        myData.energy = vin; // Map Vin ke field energy
        myData.danger = overVoltageDetected ? 1 : 0;
        myData.current = 0.0; // Dummy karena belum ada sensor arus
        xSemaphoreGive(xMutex);

        esp_err_t result = esp_now_send(masterAddress, (uint8_t *) &myData, sizeof(myData));
        
        if (result == ESP_OK) {
            Serial.println("[COMM] Data Sent to Master");
        } else {
            Serial.println("[COMM] Error sending data");
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // Kirim tiap 2 detik
    }
}

// ===============================
// Setup
// ===============================

void setup() {
    Serial.begin(115200);
    
    // Create Mutex
    xMutex = xSemaphoreCreateMutex();

    // Hardware Setup
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    // ESP32 Arduino Core v3.0+ syntax
    ledcAttach(BUZZER_PIN, 2000, 10); 

    // Initial State
    digitalWrite(RELAY_PIN, HIGH);   // Relay ON initially
    digitalWrite(BUZZER_PIN, LOW);   // Buzzer OFF

    WiFi.mode(WIFI_AP_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    memcpy(peerInfo.peer_addr, masterAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    Serial.println("Setting up WiFi AP...");
    
    // WiFi Access Point Setup
    WiFi.softAP(ssid, password);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Web Server Routes
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/reset", handleReset);
    server.begin();
    Serial.println("HTTP server started");

    // Create Tasks
    xTaskCreate(TaskMonitor, "Monitor", 2048, NULL, 3, NULL);   // Priority 3 (Highest)
    xTaskCreate(TaskAlarm,   "Alarm",   4096, NULL, 2, NULL);   // Priority 2
    xTaskCreate(TaskTelemetry,"Serial", 2048, NULL, 1, NULL);   // Priority 1
    xTaskCreate(TaskWeb,     "Web",     4096, NULL, 1, NULL);   // Priority 1
    xTaskCreate(TaskCommunication, "Comm", 2048, NULL, 1, NULL);
}

void loop() {
    vTaskDelete(NULL);    
}