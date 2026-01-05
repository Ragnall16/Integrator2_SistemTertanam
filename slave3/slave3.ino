#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

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
// Definisi Nada (Telolet)
// ===============================
#define e4 329
#define g4 392
#define a4 440
#define c5 523
#define b4 493
#define d5 587
#define rest 0

// ===============================
// Global Variables & Mutex
// ===============================
SemaphoreHandle_t xMutex;

float vin = 0.0;
bool overVoltageDetected = false;

const int ANALOG_INPUT = 34;
const int RELAY_PIN = 18;
const int BUZZER_PIN = 19;
const float R1 = 30000.0;
const float R2 = 7500.0;
const float VOLTAGE_THRESHOLD = 5.0;

int beatlength = 120;
float beatseparationconstant = 0.3;

int telolet_melody[] = {e4, g4, a4, a4, a4, g4, e4, g4, a4, c5, b4, a4, g4, e4, g4, a4, e4, g4, a4, a4, a4, g4, e4, g4, a4, c5, d5, c5, b4, a4, g4, rest};
int telolet_rhythm[] = {2,2,4,2, 2,2,2,4, 2,2,2,4, 2,2,2,4, 2,2,4,2, 2,2,2,4, 2,2,2,4, 2,4,6,4};

// ===============================
// Helper Functions
// ===============================
void playNote(int freq, int duration) {
    if (freq > 0) ledcWriteTone(BUZZER_PIN, freq);
    else ledcWriteTone(BUZZER_PIN, 0);
    
    vTaskDelay(pdMS_TO_TICKS(duration));
    ledcWriteTone(BUZZER_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(duration * beatseparationconstant));
}

void playSong(int *melody, int *rhythm, int length) {
    for (int i = 0; i < length; i++) {
        bool shouldStop = false;
        xSemaphoreTake(xMutex, portMAX_DELAY);
        if (!overVoltageDetected) shouldStop = true;
        xSemaphoreGive(xMutex);
        
        if (shouldStop) {
            ledcWriteTone(BUZZER_PIN, 0); // Matikan buzzer seketika
            break; // Keluar dari loop lagu
        }

        playNote(melody[i], rhythm[i] * beatlength);
    }
}

// ===============================
// RTOS Tasks
// ===============================

// Task 1: Monitoring Tegangan
void TaskMonitor(void *pvParameters) {
    for (;;) {
        int localAdc = analogRead(ANALOG_INPUT);
        float localVout = (localAdc * 3.3) / 4095.0;
        float localVin = localVout * ((R1 + R2) / R2);

        xSemaphoreTake(xMutex, portMAX_DELAY);
        vin = localVin;
        if (vin > VOLTAGE_THRESHOLD && !overVoltageDetected) {
            overVoltageDetected = true;
            digitalWrite(RELAY_PIN, LOW); // Matikan Relay (Safety)
        }
        xSemaphoreGive(xMutex);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Task 2: Alarm (Telolet)
void TaskAlarm(void *pvParameters) {
    for (;;) {
        bool isAlert = false;
        xSemaphoreTake(xMutex, portMAX_DELAY);
        isAlert = overVoltageDetected;
        xSemaphoreGive(xMutex);

        if (isAlert) {
            playSong(telolet_melody, telolet_rhythm, sizeof(telolet_melody) / sizeof(int));
        } else {
            ledcWriteTone(BUZZER_PIN, 0); // Matikan buzzer jika tidak alert
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

// Task 3: Komunikasi ESP-NOW (Pengiriman ke Master)
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

// Task 4: Serial Debug
void TaskTelemetry(void *pvParameters) {
    for (;;) {
        xSemaphoreTake(xMutex, portMAX_DELAY);
        Serial.printf("Voltage: %.2f V | Status: %s\n", vin, overVoltageDetected ? "OVERVOLTAGE" : "NORMAL");
        xSemaphoreGive(xMutex);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    struct_message recvMsg;
    memcpy(&recvMsg, incomingData, sizeof(recvMsg));

    // Jika pengirim adalah Master (ID 10) dan instruksinya adalah Reset (-1)
    if (recvMsg.senderID == 10 && recvMsg.danger == -1) {
        xSemaphoreTake(xMutex, portMAX_DELAY);
        overVoltageDetected = false;    // Clear the latch
        digitalWrite(RELAY_PIN, HIGH); // Nyalakan kembali relay
        xSemaphoreGive(xMutex);
        Serial.println("SYSTEM RESET BY MASTER DASHBOARD");
    }
}

// ===============================
// Setup & Main Loop
// ===============================
void setup() {
    Serial.begin(115200);
    
    // Hardware Setup
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH); // Relay ON di awal
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    ledcAttach(BUZZER_PIN, 2000, 10); 

    // Wi-Fi & ESP-NOW Setup
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

    memcpy(peerInfo.peer_addr, masterAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    xMutex = xSemaphoreCreateMutex();

    // Create RTOS Tasks
    xTaskCreate(TaskMonitor, "Monitor", 2048, NULL, 3, NULL);
    xTaskCreate(TaskAlarm, "Alarm", 4096, NULL, 1, NULL);
    xTaskCreate(TaskCommunication, "Comm", 2048, NULL, 2, NULL);
    xTaskCreate(TaskTelemetry, "Serial", 2048, NULL, 1, NULL);

    Serial.println("Slave 3 (Energy) Ready");
}

void loop() {
    // Kosong (Semua diatur FreeRTOS)
}