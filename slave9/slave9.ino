#include <esp_now.h>
#include <WiFi.h>

// PIN DEFINITIONS
#define SENSOR_A 34
#define SENSOR_B 35
#define GREEN_A 25
#define GREEN_B 33
#define RED_LED 27
#define BUZZER 26
#define FAN  32

// ALAMAT MAC PENERIMA
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

// GLOBAL VARIABLES
int smokeA = 0, smokeB = 0;
int baselineA = 0, baselineB = 0;
int adjA = 0, adjB = 0;
int dangerLevel = 0;
int evacDoor = 1;    

void TaskSensor(void *pvParameters) {
    for (;;) {
        smokeA = analogRead(SENSOR_A);
        smokeB = analogRead(SENSOR_B);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void TaskDecision(void *pvParameters) {
    for (;;) {
        adjA = max(0, smokeA - baselineA);
        adjB = max(0, smokeB - baselineB);
        int maxSmoke = max(adjA, adjB);

        // Tentukan Danger Level
        if (maxSmoke < 300) {
        dangerLevel = 0;
        evacDoor = -1; // Set ke -1 jika di bawah threshold (Aman)
        }
        else {
        if (maxSmoke < 700) dangerLevel = 1;
        else dangerLevel = 2;

        // Penentuan Pintu jika ada asap
        evacDoor = (adjA <= adjB) ? 1 : 2;
        }

        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
}


void TaskDebug(void *pvParameters) {
    for (;;) {
        Serial.println("\n========== DEBUG SYSTEM ==========");
        Serial.printf("Sensor A (Adj): %d | Sensor B (Adj): %d\n", adjA, adjB);
    
        Serial.print("Status: ");
        if (dangerLevel == 0) Serial.println("NORMAL");
        else if (dangerLevel == 1) Serial.println("WARNING");
        else Serial.println("EMERGENCY");

        Serial.print("Jalur Evakuasi: ");
        if (evacDoor == -1) Serial.println("TIDAK ADA (AMAN)");
        else Serial.println(evacDoor == 1 ? "PINTU A" : "PINTU B");

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}


void TaskCommunication(void *pvParameters) {
    for (;;) {
        myData.senderID = 9;
        myData.danger = dangerLevel;
        myData.door = evacDoor;
        
        esp_now_send(masterAddress, (uint8_t *) &myData, sizeof(myData));
        vTaskDelay(2500 / portTICK_PERIOD_MS);
    }
}

void TaskAlarm(void *pvParameters) {
    for (;;) {
        if (dangerLevel == 2) {
        digitalWrite(BUZZER, HIGH);
        digitalWrite(FAN, HIGH);
        digitalWrite(RED_LED, HIGH);
        vTaskDelay(300 / portTICK_PERIOD_MS);
        digitalWrite(RED_LED, LOW);
        vTaskDelay(300 / portTICK_PERIOD_MS);
        } else {
        digitalWrite(FAN, LOW);
        digitalWrite(BUZZER, LOW);
        digitalWrite(RED_LED, LOW);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
}

void TaskEvacuation(void *pvParameters) {
    for (;;) {
        digitalWrite(GREEN_A, (evacDoor == 1 && dangerLevel > 0));
        digitalWrite(GREEN_B, (evacDoor == 2 && dangerLevel > 0));
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    
    pinMode(GREEN_A, OUTPUT);
    pinMode(GREEN_B, OUTPUT);
    pinMode(RED_LED, OUTPUT);
    pinMode(BUZZER, OUTPUT);
    pinMode(FAN, OUTPUT);

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) return;

    memcpy(peerInfo.peer_addr, masterAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.println("Kalibrasi Sensor... Tunggu 60 detik.");
    delay(60000);
    baselineA = analogRead(SENSOR_A);
    baselineB = analogRead(SENSOR_B);

    xTaskCreate(TaskSensor, "Sensor", 2048, NULL, 3, NULL);
    xTaskCreate(TaskDecision, "Decision", 2048, NULL, 2, NULL);
    xTaskCreate(TaskCommunication, "Comm", 2048, NULL, 1, NULL);
    xTaskCreate(TaskAlarm, "Alarm", 1024, NULL, 1, NULL);
    xTaskCreate(TaskEvacuation, "Evac", 1024, NULL, 1, NULL);
    xTaskCreate(TaskDebug, "Debug", 2048, NULL, 1, NULL); // Jalankan Task Debug
}

void loop() {}