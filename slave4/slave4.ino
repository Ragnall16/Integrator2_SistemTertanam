#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// ===== ADDRESS MASTER =====
uint8_t masterAddress[] = {0x88, 0x57, 0x21, 0x94, 0x69, 0x10}; 

typedef struct struct_message {
    int senderID;
    int light;
    int soil;
    int danger;
    int door;
    float current;
    float energy;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// ===== PIN =====
#define LDR_PIN   35
#define SOIL_PIN  34
#define LED_PIN   26
#define PUMP_PIN  27

// ===== THRESHOLD =====
#define LIGHT_THRESHOLD  80   // sesuaikan hasil kalibrasi
#define SOIL_THRESHOLD   2300   // kering < threshold

// ===== VARIABEL GLOBAL (SHARED) =====
volatile int lightIntensity = 0;
volatile int soilMoisture   = 0;

volatile bool isDark  = false;
volatile bool isDry   = false;

// ===== TASK SENSOR CAHAYA =====
void taskLightSensor(void *parameter) {
    while (1) {
        lightIntensity = analogRead(LDR_PIN);
        isDark = (lightIntensity > LIGHT_THRESHOLD);

        Serial.print("[LIGHT] Intensity: ");
        Serial.println(lightIntensity);

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

// ===== TASK SENSOR SOIL =====
void taskSoilSensor(void *parameter) {
    while (1) {
        soilMoisture = analogRead(SOIL_PIN);
        isDry = (soilMoisture > SOIL_THRESHOLD);

        Serial.print("[SOIL] Moisture: ");
        Serial.println(soilMoisture);

        vTaskDelay(700 / portTICK_PERIOD_MS);
    }
}

// ===== TASK KONTROL AKTUATOR =====
void taskControl(void *parameter) {
    while (1) {

        // === LED GROW LIGHT ===
        if (isDark) {
        digitalWrite(LED_PIN, HIGH);
        Serial.println("[CTRL] GELAP -> LED ON");
        } else {
        digitalWrite(LED_PIN, LOW);
        Serial.println("[CTRL] TERANG -> LED OFF");
        }

        // === POMPA AIR ===
        if (isDry) {
        digitalWrite(PUMP_PIN, LOW);
        Serial.println("[CTRL] TANAH KERING -> POMPA ON");
        } else {
        digitalWrite(PUMP_PIN, HIGH);
        Serial.println("[CTRL] TANAH LEMBAB -> POMPA OFF");
        }

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void taskSender(void *pvParameters) {
    for (;;) {
        myData.senderID = 4;
        myData.light = lightIntensity;
        myData.soil = soilMoisture;
        
        esp_now_send(masterAddress, (uint8_t *) &myData, sizeof(myData));
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK) return;

    pinMode(LED_PIN, OUTPUT);
    pinMode(PUMP_PIN, OUTPUT);

    analogSetAttenuation(ADC_11db);

  // ===== BUAT TASK RTOS =====
    xTaskCreate(taskLightSensor, "Light Task", 2048, NULL, 1, NULL);
    xTaskCreate(taskSoilSensor,  "Soil Task",  2048, NULL, 1, NULL);
    xTaskCreate(taskControl,     "Control",    2048, NULL, 2, NULL);

    memcpy(peerInfo.peer_addr, masterAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    xTaskCreate(taskSender, "SendTask", 2048, NULL, 1, NULL);
}

void loop() {
  // kosong (RTOS yang bekerja)
}