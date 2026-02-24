// #include <SoftwareSerial.h>

// // Branchement : TX module -> D7 (RX), RX module -> D8 (TX)
// SoftwareSerial ThermalSerial(D7, D8); 

// float frame[768];
// uint8_t rawData[1540]; // Buffer pour stocker une trame complète

// void setup() {
//   Serial.begin(115200); // Pour le PC
//   ThermalSerial.begin(115200); // Pour le capteur
  
//   delay(2000);
//   Serial.println("\n--- GY-MCU90640 Binaire (D7/D8) ---");
  
//   // Envoi de la commande de flux continu au cas où
//   byte startCmd[] = {0xA5, 0x35, 0x01, 0xDB};
//   ThermalSerial.write(startCmd, 4);
// }

// void loop() {
//   // 1. On cherche l'en-tête de trame 0x5A 0x5A
//   if (ThermalSerial.available() >= 2) {
//     if (ThermalSerial.read() == 0x5A && ThermalSerial.read() == 0x5A) {
      
//       // 2. On vérifie que c'est bien une trame de pixels (Type 0x02)
//       if (ThermalSerial.read() == 0x02) {
//         uint8_t dataLen = ThermalSerial.read(); // Octet de longueur (souvent 0x06)
        
//         // 3. On lit les 1536 octets de données (768 pixels * 2 octets)
//         // On utilise readBytes pour ne pas rater le flux rapide
//         int received = ThermalSerial.readBytes(rawData, 1536);
        
//         if (received == 1536) {
//           parsePixels();
//           showStats();
//         }
//       }
//     }
//   }
// }

// void parsePixels() {
//   for (int i = 0; i < 768; i++) {
//     // Assemblage de deux octets (High | Low) pour former un entier 16 bits
//     int16_t tempInt = (rawData[i * 2] << 8) | rawData[i * 2 + 1];
//     frame[i] = tempInt / 100.0; // Le module envoie T*100
//   }
// }

// void showStats() {
//   float minT = frame[0], maxT = frame[0];
  
//   // 1. Calcul des extrêmes pour l'auto-scaling
//   for (int i = 1; i < 768; i++) {
//     if (frame[i] < minT) minT = frame[i];
//     if (frame[i] > maxT) maxT = frame[i];
//   }
  
//   Serial.printf("MIN: %.1f°C | MAX: %.1f°C | DIFF: %.1f°C\n", minT, maxT, maxT - minT);
  
//   // 2. Échelle de caractères (du plus froid au plus chaud)
//   const char ramp[] = " .:-=+*#%@";
//   int rampLen = strlen(ramp);

//   // 3. Affichage de la matrice 32x24
//   for (int y = 0; y < 24; y++) {
//     for (int x = 0; x < 32; x++) {
//       float t = frame[y * 32 + x];
      
//       // Calcul de l'index dans la rampe de caractères
//       // Formule : Index = (T - Tmin) / (Tmax - Tmin) * (Nombre de caractères - 1)
//       int val;
//       if (maxT == minT) {
//         val = 0;
//       } else {
//         val = (int)((t - minT) * (rampLen - 1) / (maxT - minT));
//       }
      
//       // Sécurité pour rester dans les bornes du tableau
//       val = max(0, min(val, rampLen - 1));
      
//       Serial.print(ramp[val]);
//       Serial.print(" "); // Espace pour garder un ratio d'aspect carré
//     }
//     Serial.println();
//   }
//   Serial.println("----------------------------------------------------------------");
// }




// #include <SoftwareSerial.h>

// SoftwareSerial ThermalSerial(D7, D8); 

// float frame[768];
// uint8_t rawData[1540]; // 1536 (pixels) + 2 (TA) + 2 (Checksum)

// void setup() {
//   Serial.begin(115200);
//   ThermalSerial.begin(115200);
  
//   delay(3000);
//   Serial.println("\n--- Configuration GY-MCU90640 (Mode 1Hz) ---");

//   // 1. On règle la fréquence à 1Hz pour la stabilité (Page 5 du manuel)
//   byte freqCmd[] = {0xA5, 0x25, 0x01, 0xCB};
//   ThermalSerial.write(freqCmd, 4);
//   delay(500);

//   // 2. On force le mode "Auto-Send" (Page 5)
//   byte autoCmd[] = {0xA5, 0x35, 0x02, 0xDC};
//   ThermalSerial.write(autoCmd, 4);
// }

// void loop() {
//   // On cherche l'en-tête complet : 5A 5A 02 06 (Page 4 du manuel)
//   if (ThermalSerial.available() >= 4) {
//     if (ThermalSerial.read() == 0x5A && ThermalSerial.read() == 0x5A && 
//         ThermalSerial.read() == 0x02 && ThermalSerial.read() == 0x06) {
      
//       // On lit les 1540 octets restants (768 pixels + TA + Checksum)
//       int received = ThermalSerial.readBytes((char*)rawData, 1540);
      
//       if (received == 1540) {
//         decodeData();
//       }
//     }
//   }
// }

// void decodeData() {
//   // Décodage des pixels (Little-Endian selon Page 4)
//   float minT = 100, maxT = -100;
  
//   for (int i = 0; i < 768; i++) {
//     int16_t raw = (rawData[i * 2 + 1] << 8) | rawData[i * 2];
//     float temp = raw / 100.0;
    
//     // Filtrage des valeurs aberrantes
//     if (temp > -40 && temp < 300) {
//       frame[i] = temp;
//       if (temp < minT) minT = temp;
//       if (temp > maxT) maxT = temp;
//     }
//   }

//   // TA (Température Ambiante) : Octets 1540-1541 (Page 3)
//   int16_t taRaw = (rawData[1537] << 8) | rawData[1536];
//   float ta = taRaw / 100.0;

//   Serial.printf("Ambiante: %.1f C | MIN: %.1f C | MAX: %.1f C\n", ta, minT, maxT);
//   drawASCII(minT, maxT);
// }

// void drawASCII(float min, float max) {
//   const char ramp[] = " .:-=+*#%@";
//   for (int y = 0; y < 24; y += 2) {
//     for (int x = 0; x < 32; x++) {
//       float t = frame[y * 32 + x];
//       int val = map(constrain(t, min, max), min, max, 0, 9);
//       Serial.print(ramp[val]);
//     }
//     Serial.println();
//   }
// }

#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>

// On simule un port de debug vers le PC sur les pins RX/TX d'origine (GPIO 3 et 1)
// Cela permet de continuer à utiliser le Moniteur Série de ton PC.
SoftwareSerial debugPC(3, 1); 

float frame[768];
uint8_t rawData[1540]; 

void setup() {
  // 1. On coupe le WiFi pour donner toute la puissance au processeur
  WiFi.forceSleepBegin();
  
  // 2. On démarre le port matériel (pour le capteur)
  Serial.begin(115200);
  Serial.swap(); // Déplace Hardware UART sur D7 (RX) et D8 (TX)
  
  // 3. On démarre le port logiciel vers le PC
  debugPC.begin(115200);
  
  delay(2000);
  debugPC.println("\n--- Mode Hardware UART (D7/D8) Actif ---");

  // On configure le module en mode Requete (Query)
  byte queryMode[] = {0xA5, 0x35, 0x01, 0xDB}; // Commande Page 5 du manuel
  Serial.write(queryMode, 4);
}

void loop() {
  // On vide les restes
  while(Serial.available() > 0) Serial.read();

  // Demande de trame
  byte askData[] = {0xA5, 0x35, 0x01, 0xDB};
  Serial.write(askData, 4);

  unsigned long startWait = millis();
  bool headerFound = false;
  
  // Attente de l'en-tête 5A 5A 02 06 (Page 4)
  while (millis() - startWait < 1000) {
    if (Serial.available() >= 4) {
      if (Serial.read() == 0x5A && Serial.read() == 0x5A && 
          Serial.read() == 0x02 && Serial.read() == 0x06) {
        headerFound = true;
        break;
      }
    }
  }

  if (headerFound) {
    // Lecture des 1540 octets (Pixels + TA + Checksum) 
    int received = Serial.readBytes((char*)rawData, 1540);
    
    if (received == 1540) {
      decodeAndShow();
    } else {
      debugPC.print("Erreur : Trame incomplete (");
      debugPC.print(received);
      debugPC.println("/1540)");
    }
  } else {
    debugPC.println("Erreur : Pas de reponse.");
  }

  debugPC.println("Attente 5 secondes...");
  delay(5000); 
}

void decodeAndShow() {
  float minT = 100, maxT = -100;
  for (int i = 0; i < 768; i++) {
    // Little-Endian (Page 4) : T = (High * 256 + Low) / 100 [cite: 102, 124]
    int16_t raw = (rawData[i * 2 + 1] << 8) | rawData[i * 2];
    float t = raw / 100.0;
    
    if (t > -40.0 && t < 300.0) {
      frame[i] = t;
      if (t < minT) minT = t;
      if (t > maxT) maxT = t;
    }
  }
  
  // Température Ambiante (TA) : Octets 1540-1541 [cite: 102, 128]
  int16_t taRaw = (rawData[1537] << 8) | rawData[1536];
  float ta = taRaw / 100.0;

  debugPC.printf("Ambiante: %.1f C | MIN: %.1f C | MAX: %.1f C\n", ta, minT, maxT);
  
  const char* ramp = " .:-=+*#%@";
  for (int y = 0; y < 24; y += 2) {
    for (int x = 0; x < 32; x++) {
      int val = map(constrain(frame[y*32+x], minT, maxT), minT, maxT, 0, 9);
      debugPC.print(ramp[val]);
    }
    debugPC.println();
  }
}