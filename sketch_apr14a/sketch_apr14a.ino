// NodeMCU-32S : pont USB <-> UART ROCK 5B

// UART2 du NodeMCU-32S
#define RXD2 16  // à connecter sur pin 8 de la ROCK 5B (UART2_TX_M0)
#define TXD2 17  // à connecter sur pin 10 de la ROCK 5B (UART2_RX_M0)

// Choisir la vitesse ici : 1500000 ou 115200 pour test
#define BAUD 115200

void setup() {
  Serial.begin(BAUD);                               // USB vers PC
  Serial2.begin(BAUD, SERIAL_8N1, RXD2, TXD2);      // UART2 vers ROCK 5B
  Serial.println("Bridge ready");
}

void loop() {
  // PC -> ROCK 5B
  while (Serial.available()) {
    Serial2.write(Serial.read());
  }

  // ROCK 5B -> PC
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}