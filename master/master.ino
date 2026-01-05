#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ===============================
// STRUKTUR DATA UNIFIKASI
// ===============================
typedef struct struct_message {
    int senderID;   // 3: Energy, 4: Agri, 9: Safet y
    int light;
    int soil;
    int danger;     // Untuk Kel 9 (Level) & Kel 3 (Overvoltage flag)
    int door;
    float current;
    float energy;   // Digunakan Kel 3 untuk membawa data Voltage (Vin)
} struct_message;

// Variabel penampung data per kelompok
struct_message slave3, slave4, slave9;

// ===== MAC ADDRESS SLAVE 3 =====
uint8_t slave3Address[] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX};

// ===============================
// KONFIGURASI WEB SERVER
// ===============================
AsyncWebServer server(80);
const char* ssid = "Integrator2_Dashboard";
const char* password = "password123";

// Thresholds Kelompok 4 (Agri)
const int LIGHT_THRESHOLD = 80;
const int SOIL_THRESHOLD = 2300;

// ===============================
// LOGIKA INTERPRETASI STATUS
// ===============================

String getAgriStatus() {
    if (slave4.senderID == 0) return "Menunggu Data...";
    String s = "";
    s += (slave4.light > LIGHT_THRESHOLD) ? "GELAP (LED ON)" : "TERANG (LED OFF)";
    s += "<br>";
    s += (slave4.soil > SOIL_THRESHOLD) ? "KERING (POMPA ON)" : "LEMBAB (POMPA OFF)";
    return s;
}

String getSafetyStatus() {
    if (slave9.senderID == 0) return "Menunggu Data...";
    String status = (slave9.danger == 0) ? "NORMAL" : (slave9.danger == 1) ? "WARNING" : "EMERGENCY";
    String pintu = (slave9.door == -1) ? "AMAN" : (slave9.door == 1) ? "PINTU A" : "PINTU B";
    return "Status: " + status + "<br>Evakuasi: " + pintu;
}

String getEnergyStatus() {
    if (slave3.senderID == 0) return "Menunggu Data...";
    if (slave3.energy > 5.0) return "OVERVOLTAGE! (ALARM ON)";
    return "Tegangan Normal (Relay ON)";
}

// ===============================
// CALLBACK ESP-NOW
// ===============================
void OnDataRecv(const uint8_t * mac, const uint8_t *data, int len) {
    struct_message incoming;
    memcpy(&incoming, data, sizeof(incoming));
    
    if (incoming.senderID == 4) slave4 = incoming;
    else if (incoming.senderID == 9) slave9 = incoming;
    else if (incoming.senderID == 3) slave3 = incoming;
    
    Serial.printf("Data diterima dari ID: %d\n", incoming.senderID);
}

// ===============================
// HTML & DASHBOARD (UI)
// ===============================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Integrator 2 - Smart Building</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: 'Segoe UI', Arial; text-align: center; background-color: #f0f2f5; margin: 0; }
    .header { background: #1a73e8; color: white; padding: 20px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
    .container { display: flex; flex-wrap: wrap; justify-content: center; padding: 20px; }
    .card { background: white; padding: 20px; margin: 15px; border-radius: 12px; width: 320px; 
            box-shadow: 0 4px 15px rgba(0,0,0,0.1); transition: 0.3s; border-top: 5px solid #ddd; }
    .card h3 { color: #5f6368; margin-top: 0; border-bottom: 1px solid #eee; padding-bottom: 10px; }
    .status-box { font-weight: bold; padding: 15px; border-radius: 8px; background: #f8f9fa; 
                 margin-top: 15px; font-size: 1.1rem; color: #3c4043; line-height: 1.5; }
    .val { font-size: 0.9rem; color: #70757a; }
    .danger { background: #fce8e6 !important; color: #d93025 !important; border: 1px solid #f5c6cb; }
    .warning { background: #fff4e5 !important; color: #664d03 !important; }
    .blink { animation: blinker 1s linear infinite; }
    @keyframes blinker { 50% { opacity: 0.6; } }
    #agri-card { border-top-color: #34a853; }
    #safety-card { border-top-color: #ea4335; }
    #energy-card { border-top-color: #fbbc05; }
  </style>
</head>
<body>
  <div class="header"><h1>INTEGRATOR 2 DASHBOARD</h1><p>Smart Building Central Control</p></div>
  <div class="container">
    
    <div class="card" id="agri-card">
      <h3>Smart Agricultural</h3>
      <div class="val">LDR: <span id="s4_light">0</span> | Soil: <span id="s4_soil">0</span></div>
      <div id="agri_status" class="status-box">...</div>
    </div>

    <div class="card" id="safety-card">
      <h3>Safety & Emergency</h3>
      <div class="val">Danger Level: <span id="s9_danger_val">0</span></div>
      <div id="safety_status" class="status-box">...</div>
    </div>

    <div class="card" id="energy-card">
      <h3>Energy Optimization</h3>
      <div class="val">Input Voltage: <span id="s3_vin">0.00</span> V</div>
      <div id="energy_status" class="status-box">...</div>
      <button onclick="resetS3()" style="margin-top:10px; padding:10px; cursor:pointer;">RESET LATCH</button>
    </div>

  </div>

  <script>
    setInterval(function ( ) {
      fetch('/data').then(response => response.json()).then(data => {
        // Update Agri
        document.getElementById("s4_light").innerHTML = data.s4_light;
        document.getElementById("s4_soil").innerHTML = data.s4_soil;
        document.getElementById("agri_status").innerHTML = data.s4_msg;

        // Update Safety
        document.getElementById("s9_danger_val").innerHTML = data.s9_danger;
        document.getElementById("safety_status").innerHTML = data.s9_msg;
        if(data.s9_danger > 1) document.getElementById("safety_status").className = "status-box danger blink";
        else if(data.s9_danger == 1) document.getElementById("safety_status").className = "status-box warning";
        else document.getElementById("safety_status").className = "status-box";

        // Update Energy
        document.getElementById("s3_vin").innerHTML = data.s3_vin.toFixed(2);
        document.getElementById("energy_status").innerHTML = data.s3_msg;
        if(data.s3_vin > 5.0) document.getElementById("energy_status").className = "status-box danger blink";
        else document.getElementById("energy_status").className = "status-box";
      });
    }, 1000);
    function resetS3() {
    fetch('/resetS3').then(response => alert("Reset Command Sent!"));
    }
  </script>
</body>
</html>)rawliteral";

// ===============================
// SETUP & LOOP
// ===============================
void setup() {
    Serial.begin(115200);

    // 1. WiFi Mode AP & STA
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid, password);
    Serial.print("Dashboard IP: ");
    Serial.println(WiFi.softAPIP());

    // 2. Init ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, slave3Address, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    // 3. Web Server Endpoints
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
        StaticJsonDocument<768> json;
        // Kelompok 4
        json["s4_light"] = slave4.light;
        json["s4_soil"] = slave4.soil;
        json["s4_msg"] = getAgriStatus();
        
        // Kelompok 9
        json["s9_danger"] = slave9.danger;
        json["s9_msg"] = getSafetyStatus();

        // Kelompok 3
        json["s3_vin"] = slave3.energy; // Mengirim tegangan
        json["s3_msg"] = getEnergyStatus();

        String response;
        serializeJson(json, response);
        request->send(200, "application/json", response);
    });

    server.on("/resetS3", HTTP_GET, [](AsyncWebServerRequest *request){
        struct_message msg;
        msg.senderID = 10; // ID Master
        msg.danger = -1;   // KODE KHUSUS: -1 berarti RESET LATCH
        
        esp_now_send(slave3Address, (uint8_t *) &msg, sizeof(msg));
        request->send(200, "text/plain", "Reset Sent");
    });

    server.begin();
    Serial.println("Integrator 2 Server Started");
}

void loop() {
    // Kosong (FreeRTOS & AsyncWebServer bekerja di background)
}