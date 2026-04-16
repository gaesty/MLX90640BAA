#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#include "soc/soc.h"           // Permet de désactiver le détecteur de baisse de tension (Brownout)
#include "soc/rtc_cntl_reg.h"  // Idem

// --- CONFIGURATION DES BROCHES (Modèle AI-Thinker) ---
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// --- INSTANCIATION DES OBJETS ---
WebServer server(80);
WebSocketsServer webSocket(81);
unsigned long lastFrameTime = 0;

// --- PAGE HTML ET JAVASCRIPT ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>ESP32-CAM Stream WebSocket</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background-color: #222; color: #fff; margin: 0; padding: 20px; }
    h1 { color: #f8f9fa; }
    .status { padding: 5px 10px; border-radius: 5px; font-size: 0.9em; display: inline-block; margin-bottom: 20px; }
    .connected { background-color: #28a745; }
    .disconnected { background-color: #dc3545; }
    #stream-container { display: inline-block; border: 3px solid #555; border-radius: 5px; overflow: hidden; box-shadow: 0 4px 8px rgba(0,0,0,0.5); }
    img { display: block; max-width: 100%; height: auto; }
  </style>
</head>
<body>
  <h1>Flux Vidéo ESP32-CAM</h1>
  <div id="connectionStatus" class="status disconnected">Déconnecté</div><br>
  
  <div id="stream-container">
    <img id="stream" src="" alt="En attente du flux vidéo..." />
  </div>

  <script>
    const statusDiv = document.getElementById('connectionStatus');
    const img = document.getElementById('stream');
    
    const gateway = `ws://${window.location.hostname}:81/`;
    let websocket;

    function initWebSocket() {
      websocket = new WebSocket(gateway);
      
      // Indispensable pour recevoir des images JPEG en binaire
      websocket.binaryType = 'blob'; 
      
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
        if (event.data instanceof Blob) {
          // Création d'une URL locale pour l'image binaire reçue
          const urlObject = URL.createObjectURL(event.data);
          img.src = urlObject;
          
          // Libération de la mémoire une fois l'image chargée pour éviter les fuites (Memory Leak)
          img.onload = () => {
            URL.revokeObjectURL(urlObject);
          };
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

// --- INITIALISATION DE LA CAMERA ---
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Configuration de la résolution
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA; // Résolution 640x480 (Bon compromis)
    config.jpeg_quality = 12;          // Qualité (0-63, plus bas = meilleure qualité)
    config.fb_count = 2;               // Utilise 2 buffers pour plus de fluidité
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Démarrage de la caméra
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Erreur d'initialisation de la caméra (0x%x)", err);
    return false;
  }
  return true;
}

// --- SETUP ---
void setup() {
  // Désactivation du Brownout detector (évite les reboots inopinés lors du pic de courant du WiFi + Caméra)
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  Serial.begin(115200);

  if (!initCamera()) {
    Serial.println("Échec de la caméra. Redémarrage...");
    delay(3000);
    ESP.restart();
  }

  // --- CONFIGURATION WIFI DYNAMIQUE ---
  WiFiManager wm;
  // wm.resetSettings(); // Décommenter si besoin de réinitialiser le WiFi

  bool res = wm.autoConnect("ESP32-CAM-Config"); 

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

  // Envoi du flux vidéo avec une limite de framerate (ici ~10 FPS)
  if (millis() - lastFrameTime > 100) { 
    lastFrameTime = millis();
    broadcastCameraFrame();
  }
}

// --- CAPTURE ET ENVOI DE L'IMAGE ---
void broadcastCameraFrame() {
  // S'il n'y a pas de client connecté, on ne capture pas d'image (économise les ressources)
  if (webSocket.connectedClients() == 0) {
    return;
  }

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Échec de la capture d'image");
    return;
  }

  // Envoi du buffer JPEG en binaire à tous les clients connectés
  webSocket.broadcastBIN(fb->buf, fb->len);

  // Libération impérative du buffer
  esp_camera_fb_return(fb);
}