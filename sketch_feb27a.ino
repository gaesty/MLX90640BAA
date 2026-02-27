#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// Remplace par tes identifiants WiFi
const char* ssid = "NP3";
const char* password = "NP3Wifi1";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81); // Serveur WS sur le port 81

// Broches pour le capteur GY-MLX90640 (Hardware Serial 2)
#define SENSOR_RX 16
#define SENSOR_TX 17

float frame[768];
uint8_t rawData[1540]; 
unsigned long lastFetch = 0;

// --- PAGE HTML ET JAVASCRIPT EMBARQUÉE ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>Heatmap Thermique (WebSocket)</title>
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
  <div id="info">En attente des données...</div>

  <script>
    const canvas = document.getElementById('heatmap');
    const ctx = canvas.getContext('2d');
    const info = document.getElementById('info');
    const statusDiv = document.getElementById('connectionStatus');

    // Initialisation du WebSocket sur le port 81 de l'IP actuelle
    const gateway = `ws://${window.location.hostname}:81/`;
    let websocket;

    function initWebSocket() {
      console.log('Tentative de connexion au WebSocket...');
      websocket = new WebSocket(gateway);
      
      // Essentiel : on indique qu'on s'attend à recevoir des données binaires
      websocket.binaryType = "arraybuffer"; 

      websocket.onopen = onOpen;
      websocket.onclose = onClose;
      websocket.onmessage = onMessage;
    }

    function onOpen(event) {
      console.log('Connexion WebSocket établie');
      statusDiv.className = "status connected";
      statusDiv.innerText = "Connecté en Temps Réel";
    }

    function onClose(event) {
      console.log('Connexion WebSocket fermée');
      statusDiv.className = "status disconnected";
      statusDiv.innerText = "Déconnecté - Reconnexion...";
      setTimeout(initWebSocket, 2000); // Tentative de reconnexion auto
    }

    function onMessage(event) {
      // Les données binaires (ArrayBuffer) sont directement castées en Float32 (Little-Endian par défaut)
      const data = new Float32Array(event.data);
      
      // Vérification de sécurité pour la taille de la trame
      if(data.length === 768) {
        drawHeatmap(data);
      }
    }

    function drawHeatmap(data) {
      // Math.min/max fonctionne mal sur les gros TypedArrays avec l'opérateur spread, on utilise une boucle
      let minT = Infinity, maxT = -Infinity;
      for(let i=0; i<data.length; i++) {
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
          const hue = (1 - norm) * 240; // 240 = Bleu, 0 = Rouge
          
          ctx.fillStyle = `hsl(${hue}, 100%, 50%)`;
          ctx.fillRect(x * w, y * h, w, h);
        }
      }
    }
    
    // Lancement au chargement de la page
    window.addEventListener('load', initWebSocket);
  </script>
</body>
</html>
)rawliteral";

// Callback optionnel pour debugger l'état des WebSockets
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Deconnecte!\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connecte depuis %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    case WStype_TEXT:
    case WStype_BIN:
      // On ne s'attend pas à recevoir des données du navigateur pour le moment
      break;
  }
}

void setup() {
  // Initialisation du Moniteur Série (PC) via USB
  Serial.begin(115200);
  
  // Initialisation du port série pour le capteur MLX90640
  // L'ESP32 a besoin d'un buffer plus grand pour recevoir les 1540 octets d'un coup
  Serial2.setRxBufferSize(2048); 
  Serial2.begin(115200, SERIAL_8N1, SENSOR_RX, SENSOR_TX);
  
  delay(1000);
  Serial.println("\n--- Démarrage de la Caméra Thermique WebSockets sur ESP32 ---");

  WiFi.begin(ssid, password);
  Serial.print("Connexion au WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnecté ! Adresse IP : ");
  Serial.println(WiFi.localIP());

  // Configuration HTTP
  server.on("/", []() {
    server.send_P(200, "text/html", index_html);
  });
  server.begin();

  // Configuration WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Demande initiale au capteur
  byte queryMode[] = {0xA5, 0x35, 0x01, 0xDB}; 
  Serial2.write(queryMode, 4);
  
  for(int i=0; i<768; i++) frame[i] = 0.0;
}

void loop() {
  server.handleClient();
  webSocket.loop(); // Indispensable pour maintenir le flux WS en vie

  // On peut descendre le délai si le capteur le supporte
  if (millis() - lastFetch > 1000) {
    lastFetch = millis();
    requestSensorData();
  }
}

void requestSensorData() {
  // Vider le buffer avant de demander de nouvelles données
  while(Serial2.available() > 0) Serial2.read();

  byte askData[] = {0xA5, 0x35, 0x01, 0xDB};
  Serial2.write(askData, 4);

  unsigned long startWait = millis();
  bool headerFound = false;
  
  // Recherche de l'en-tête (5A 5A 02 06)
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
    // Lecture de la trame de données complète
    int received = Serial2.readBytes((char*)rawData, 1540);
    if (received == 1540) {
      decodeFrame();
      
      // ENVOI WEBSOCKET : On diffuse le tableau de float en binaire brut
      webSocket.broadcastBIN((uint8_t*)frame, sizeof(frame));
      
    } else {
      Serial.printf("Erreur : Trame incomplete (%d/1540)\n", received);
    }
  }
}

void decodeFrame() {
  for (int i = 0; i < 768; i++) {
    int16_t raw = (rawData[i * 2 + 1] << 8) | rawData[i * 2];
    float t = raw / 100.0;
    
    // Filtrage basique pour éliminer les valeurs aberrantes
    if (t > -40.0 && t < 300.0) {
      frame[i] = t;
    }
  }
}