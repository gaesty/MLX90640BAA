#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#include <DHT.h>

// --- CONFIGURATION DES BROCHES ---
#define DHTPIN 5
#define DHTTYPE DHT11
#define PIRPIN 22

// --- INSTANCIATION DES OBJETS ---
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
WebSocketsServer webSocket(81);

unsigned long lastFetch = 0;
int lastPirState = -1; // Pour ne pas rater de détection entre deux lectures du DHT

// --- PAGE HTML ET JAVASCRIPT ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>Dashboard DHT11 & PIR</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background-color: #222; color: #fff; margin: 0; padding: 20px; }
    h1 { color: #f8f9fa; }
    .card-container { display: flex; justify-content: center; gap: 20px; flex-wrap: wrap; margin-top: 20px; }
    .card { background-color: #333; padding: 20px; border-radius: 10px; width: 250px; box-shadow: 0 4px 8px rgba(0,0,0,0.5); }
    .card h2 { margin-top: 0; font-size: 1.2em; color: #adb5bd; }
    .card h3 { font-size: 2em; margin: 10px 0 0 0; }
    .status { padding: 5px 10px; border-radius: 5px; font-size: 0.9em; display: inline-block; margin-bottom: 10px; }
    .connected { background-color: #28a745; }
    .disconnected { background-color: #dc3545; }
    .motion { color: #ffc107; text-shadow: 0 0 10px rgba(255,193,7,0.5); }
    .no-motion { color: #6c757d; }
  </style>
</head>
<body>
  <h1>Capteurs NodeMCU 32S</h1>
  <div id="connectionStatus" class="status disconnected">Déconnecté</div><br>
  
  <div class="card-container">
    <div class="card">
      <h2>Température</h2>
      <h3 id="temp">-- °C</h3>
    </div>
    <div class="card">
      <h2>Humidité</h2>
      <h3 id="hum">-- %</h3>
    </div>
    <div class="card">
      <h2>Mouvement (PIR)</h2>
      <h3 id="pir" class="no-motion">Aucun mouvement</h3>
    </div>
  </div>

  <script>
    const statusDiv = document.getElementById('connectionStatus');
    const tempEl = document.getElementById('temp');
    const humEl = document.getElementById('hum');
    const pirEl = document.getElementById('pir');
    
    // Connexion WebSocket dynamique basée sur l'IP de l'ESP32
    const gateway = `ws://${window.location.hostname}:81/`;
    let websocket;

    function initWebSocket() {
      websocket = new WebSocket(gateway);
      
      websocket.onopen = () => { 
        statusDiv.className = "status connected"; 
        statusDiv.innerText = "Connecté"; 
      };
      
      websocket.onclose = () => { 
        statusDiv.className = "status disconnected"; 
        statusDiv.innerText = "Déconnecté";
        setTimeout(initWebSocket, 2000); 
      };
      
      websocket.onmessage = (event) => {
        try {
          // On attend un JSON du type : {"t": 25.4, "h": 50, "p": 1}
          const data = JSON.parse(event.data);
          
          if (data.t !== undefined && !isNaN(data.t)) tempEl.innerText = data.t.toFixed(1) + " °C";
          if (data.h !== undefined && !isNaN(data.h)) humEl.innerText = data.h.toFixed(1) + " %";
          
          if (data.p !== undefined) {
            if (data.p === 1) {
              pirEl.innerText = "Mouvement détecté !";
              pirEl.className = "motion";
            } else {
              pirEl.innerText = "Aucun mouvement";
              pirEl.className = "no-motion";
            }
          }
        } catch (e) {
          console.error("Erreur de parsing JSON", e);
        }
      };
    }

    window.onload = initWebSocket;
  </script>
</body>
</html>
)rawliteral";

// --- EVENEMENTS WEBSOCKET ---
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("[%u] Client connecté\n", num);
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("[%u] Client déconnecté\n", num);
  }
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);

  // Initialisation des capteurs
  dht.begin();
  pinMode(PIRPIN, INPUT);

  // --- CONFIGURATION WIFI DYNAMIQUE ---
  WiFiManager wm;
  
  // wm.resetSettings(); // Décommenter pour effacer les identifiants WiFi enregistrés

  bool res = wm.autoConnect("ESP32-Capteurs-Config"); 

  if(!res) {
    Serial.println("Échec de la connexion, redémarrage...");
    ESP.restart();
  } 

  Serial.println("Connecté au WiFi !");
  Serial.print("IP : ");
  Serial.println(WiFi.localIP());

  // Configuration de la route HTTP
  server.on("/", []() {
    server.send_P(200, "text/html", index_html);
  });
  server.begin();

  // Démarrage du WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

// --- LOOP ---
void loop() {
  server.handleClient();
  webSocket.loop();

  // Le DHT11 est un capteur lent. On le lit toutes les 2 secondes (2000 ms)
  if (millis() - lastFetch > 2000) { 
    lastFetch = millis();
    broadcastSensorData();
  }
}

// --- LECTURE ET ENVOI DES DONNEES ---
void broadcastSensorData() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int pirState = digitalRead(PIRPIN);

  // Vérification de la lecture du DHT11
  if (isnan(h) || isnan(t)) {
    Serial.println("Échec de lecture du DHT11 !");
    return;
  }

  // Création d'une chaîne JSON manuelle pour éviter de charger une librairie supplémentaire
  String jsonPayload = "{";
  jsonPayload += "\"t\":" + String(t) + ",";
  jsonPayload += "\"h\":" + String(h) + ",";
  jsonPayload += "\"p\":" + String(pirState);
  jsonPayload += "}";

  // Envoi à tous les clients connectés via WebSocket
  webSocket.broadcastTXT(jsonPayload);
}