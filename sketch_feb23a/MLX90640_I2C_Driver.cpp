#include <Arduino.h>
#include <Wire.h>
#include "MLX90640_I2C_Driver.h"

void MLX90640_I2CInit() {
    // Initialisation gérée dans le setup() du fichier principal
}

int MLX90640_I2CGeneralReset(void) {
    return 0; 
}

// LECTURE SÉCURISÉE (Le fix pour ESP8266)
int MLX90640_I2CRead(uint8_t slaveAddr, unsigned int startAddress, unsigned int nMemAddressRead, uint16_t *data) {
    uint16_t cnt = 0;
    
    // Découpage ultra-sécurisé : 8 mots (16 octets) max par requête
    while (nMemAddressRead > 0) {
        uint16_t chunkWords = nMemAddressRead;
        if (chunkWords > 8) {
            chunkWords = 8;
        }

        Wire.beginTransmission(slaveAddr);
        Wire.write(startAddress >> 8);
        Wire.write(startAddress & 0x00FF);
        
        // LE FIX ABSOLU POUR ESP8266 : "true"
        // On force un STOP. Le Repeated Start de l'ESP8266 est instable.
        if (Wire.endTransmission(true) != 0) {
            return -1;
        }

        // Mini-pause pour laisser la puce I2C respirer et réarmer son pointeur
        delayMicroseconds(50);

        uint8_t bytesRequested = chunkWords * 2;
        uint8_t bytesReceived = Wire.requestFrom((int)slaveAddr, (int)bytesRequested);

        if (bytesReceived != bytesRequested) {
            return -1;
        }

        for (int i = 0; i < chunkWords; i++) {
            uint16_t high = Wire.read();
            uint16_t low = Wire.read();
            data[cnt++] = (high << 8) | low;
        }

        startAddress += chunkWords;
        nMemAddressRead -= chunkWords;
    }
    return 0;
}

// ÉCRITURE STANDARD
int MLX90640_I2CWrite(uint8_t slaveAddr, unsigned int writeAddress, uint16_t data) {
    Wire.beginTransmission(slaveAddr);
    Wire.write(writeAddress >> 8);
    Wire.write(writeAddress & 0x00FF);
    Wire.write(data >> 8);
    Wire.write(data & 0x00FF);
    if (Wire.endTransmission() != 0) {
        return -1;
    }
    return 0;
}