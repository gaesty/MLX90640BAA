#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h> // Ajouter cette bibliothèque via le gestionnaire de bibliothèques

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

#define SENSOR_RX 16
#define SENSOR_TX 17

float frame[768];
uint8_t rawData[1540]; 
unsigned long lastFetch = 0;

// --- PAGE HTML (Inchangée) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>Heatmap Thermique</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background-color: #222; color: #fff; margin: 0; padding: 20px; }
    canvas { border: 2px solid #555; margin-top: 20px; image-rendering: pixelated; box-shadow: 0 4px 8px rgba(0,0,0,0.5); }
    #info { margin-top: 15px; font-size: 1.2em; }
    .status { padding: 5px 10px; border-radius: 5px; font-size: 0.9em; display: inline-block; margin-bottom: 10px; }
    .connected { background-color: #28a745; }
    .disconnected { background-color: #dc3545; }
  </style>
</head>
<body>
  <h1>Caméra Thermique 32x24</h1>
  <div id="connectionStatus" class="status disconnected">Déconnecté</div><br>
  <canvas id="heatmap" width="640" height="480"></canvas>
  <div id="info">En attente...</div>
  <script>
    const canvas = document.getElementById('heatmap');
    const ctx = canvas.getContext('2d');
    const info = document.getElementById('info');
    const statusDiv = document.getElementById('connectionStatus');
    const gateway = `ws://${window.location.hostname}:81/`;
    let websocket;

    function initWebSocket() {
      websocket = new WebSocket(gateway);
      websocket.binaryType = "arraybuffer"; 
      websocket.onopen = () => { statusDiv.className = "status connected"; statusDiv.innerText = "Connecté"; };
      websocket.onclose = () => { statusDiv.className = "status disconnected"; setTimeout(initWebSocket, 2000); };
      websocket.onmessage = (event) => {
        const data = new Float32Array(event.data);
        if(data.length === 768) drawHeatmap(data);
      };
    }

    function drawHeatmap(data) {
      let minT = 100, maxT = 0;
      for(let i=0; i<768; i++) {
        if(data[i] < minT) minT = data[i];
        if(data[i] > maxT) maxT = data[i];
      }
      info.innerText = `Min: ${minT.toFixed(1)}°C | Max: ${maxT.toFixed(1)}°C`;
      const w = canvas.width / 32;
      const h = canvas.height / 24;
      for (let y = 0; y < 24; y++) {
        for (let x = 0; x < 32; x++) {
          const temp = data[y * 32 + x];
          const norm = (temp - minT) / (maxT - minT || 1);
          ctx.fillStyle = `hsl(${(1-norm)*240}, 100%, 50%)`;
          ctx.fillRect(x * w, y * h, w, h);
        }
      }
    }
    window.onload = initWebSocket;
  </script>
</body>
</html>
)rawliteral";

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if(type == WStype_CONNECTED) {
    Serial.printf("[%u] Client connecté\n", num);
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.setRxBufferSize(2048); 
  Serial2.begin(115200, SERIAL_8N1, SENSOR_RX, SENSOR_TX);

  // --- CONFIGURATION WIFI DYNAMIQUE ---
  WiFiManager wm;
  
  // Supprimer les commentaires ci-dessous pour réinitialiser les réglages au test
  // wm.resetSettings(); 

  bool res = wm.autoConnect("ESP32-Thermal-Config"); 

  if(!res) {
    Serial.println("Échec de la connexion, redémarrage...");
    ESP.restart();
  } 

  Serial.println("Connecté au WiFi !");
  Serial.print("IP : ");
  Serial.println(WiFi.localIP());

  server.on("/", []() {
    server.send_P(200, "text/html", index_html);
  });
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  byte queryMode[] = {0xA5, 0x35, 0x01, 0xDB}; 
  Serial2.write(queryMode, 4);
}

void loop() {
  server.handleClient();
  webSocket.loop();

  if (millis() - lastFetch > 500) { // On peut monter à 2 FPS (500ms)
    lastFetch = millis();
    requestSensorData();
  }
}

void requestSensorData() {
  while(Serial2.available() > 0) Serial2.read();
  byte askData[] = {0xA5, 0x35, 0x01, 0xDB};
  Serial2.write(askData, 4);

  unsigned long startWait = millis();
  bool headerFound = false;
  
  while (millis() - startWait < 500) {
    if (Serial2.available() >= 4) {
      if (Serial2.read() == 0x5A && Serial2.read() == 0x5A && 
          Serial2.read() == 0x02 && Serial2.read() == 0x06) {
        headerFound = true;
        break;
      }
    }
  }

  if (headerFound) {
    int received = Serial2.readBytes((char*)rawData, 1540);
    if (received == 1540) {
      for (int i = 0; i < 768; i++) {
        int16_t raw = (rawData[i * 2 + 1] << 8) | rawData[i * 2];
        frame[i] = raw / 100.0;
      }
      webSocket.broadcastBIN((uint8_t*)frame, sizeof(frame));
    }
  }
}