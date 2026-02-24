#include <SoftwareSerial.h>

// Branchement : TX module -> D7 (RX), RX module -> D8 (TX)
SoftwareSerial ThermalSerial(D7, D8); 

float frame[768];
uint8_t rawData[1540]; // Buffer pour stocker une trame complète

void setup() {
  Serial.begin(115200); // Pour le PC
  ThermalSerial.begin(115200); // Pour le capteur
  
  delay(2000);
  Serial.println("\n--- GY-MCU90640 Binaire (D7/D8) ---");
  
  // Envoi de la commande de flux continu au cas où
  byte startCmd[] = {0xA5, 0x35, 0x01, 0xDB};
  ThermalSerial.write(startCmd, 4);
}

void loop() {
  // 1. On cherche l'en-tête de trame 0x5A 0x5A
  if (ThermalSerial.available() >= 2) {
    if (ThermalSerial.read() == 0x5A && ThermalSerial.read() == 0x5A) {
      
      // 2. On vérifie que c'est bien une trame de pixels (Type 0x02)
      if (ThermalSerial.read() == 0x02) {
        uint8_t dataLen = ThermalSerial.read(); // Octet de longueur (souvent 0x06)
        
        // 3. On lit les 1536 octets de données (768 pixels * 2 octets)
        // On utilise readBytes pour ne pas rater le flux rapide
        int received = ThermalSerial.readBytes(rawData, 1536);
        
        if (received == 1536) {
          parsePixels();
          showStats();
        }
      }
    }
  }
}

void parsePixels() {
  for (int i = 0; i < 768; i++) {
    // Assemblage de deux octets (High | Low) pour former un entier 16 bits
    int16_t tempInt = (rawData[i * 2] << 8) | rawData[i * 2 + 1];
    frame[i] = tempInt / 100.0; // Le module envoie T*100
  }
}

void showStats() {
  float minT = frame[0], maxT = frame[0];
  for (int i = 1; i < 768; i++) {
    if (frame[i] < minT) minT = frame[i];
    if (frame[i] > maxT) maxT = frame[i];
  }
  
  Serial.printf("MIN: %.1f°C | MAX: %.1f°C\n", minT, maxT);
  
  // Affichage ASCII simplifié
  for (int y = 0; y < 24; y += 2) {
    for (int x = 0; x < 32; x++) {
      Serial.print(frame[y * 32 + x] > 30 ? '#' : '.');
    }
    Serial.println();
  }
  Serial.println("--------------------------------");
}