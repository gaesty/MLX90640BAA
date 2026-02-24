# GY-MLX90640BAA — ESP8266 Thermal Camera

Read temperature frames from a **GY-MCU90640** (MLX90640BAA) thermal sensor module with an **ESP8266** and display them as an ASCII heat-map over the serial monitor.

---

## Hardware

### GY-MCU90640 module
The GY-MCU90640 is a ready-to-use breakout board built around the **Melexis MLX90640BAA** 32×24 far-infrared sensor array.  
It communicates over **UART at 115 200 baud** and delivers a pre-processed 768-pixel temperature frame together with the ambient temperature on each request.

| Pin | Description |
|-----|-------------|
| VCC | 3.3 V or 5 V |
| GND | Ground |
| TX  | Module transmits → connect to ESP8266 RX |
| RX  | Module receives  → connect to ESP8266 TX |

### ESP8266
Any ESP8266 board (NodeMCU, Wemos D1 mini, …) works.  
The sketch uses `Serial.swap()` to move the hardware UART to **D7 (RX) / D8 (TX)** so that the USB-to-serial adapter (GPIO 1/3) remains free for the debug monitor.

---

## Wiring

```
GY-MCU90640          ESP8266
-----------          -------
VCC  ──────────────► 3.3 V (or 5 V depending on your board)
GND  ──────────────► GND
TX   ──────────────► D7 (GPIO 13) ← Hardware UART RX after swap()
RX   ──────────────► D8 (GPIO 15) ← Hardware UART TX after swap()
```

The USB-to-serial converter of the board stays connected on the default pins (GPIO 1 / 3) and is used exclusively for the debug `SoftwareSerial` output you read in the Serial Monitor.

---

## Dependencies

Install the following libraries through the **Arduino Library Manager** (Sketch → Include Library → Manage Libraries…):

| Library | Notes |
|---------|-------|
| `ESP8266WiFi` | Bundled with the ESP8266 Arduino core |
| `SoftwareSerial` | Bundled with the ESP8266 Arduino core |

Make sure the **ESP8266 Arduino core** is installed via the Boards Manager  
(`https://arduino.esp8266.com/stable/package_esp8266com_index.json`).

---

## How it works

1. **WiFi is disabled** (`WiFi.forceSleepBegin()`) to dedicate full CPU power to the UART stream.  
2. The hardware UART is swapped to **D7/D8** and configured at **115 200 baud** to communicate with the sensor module.  
3. A `SoftwareSerial` instance on the original UART pins (GPIO 3/1) is used for debug output to the PC.  
4. Every 5 seconds the sketch:
   - Sends a query command (`0xA5 0x35 0x01 0xDB`) to the module.  
   - Waits for the 4-byte header `5A 5A 02 06`.  
   - Reads **1 540 bytes**: 1 536 bytes of pixel data (768 pixels × 2 bytes, Little-Endian, value = raw / 100 °C) + 2 bytes ambient temperature + 2 bytes checksum.  
   - Prints the ambient temperature, min/max temperatures, and a **32×12 ASCII thermal map** using the character ramp ` .:-=+*#%@`.

---

## Usage

1. Wire the module as shown above.  
2. Open `sketch_feb23a.ino` in the Arduino IDE.  
3. Select your ESP8266 board and the correct COM port.  
4. Upload the sketch.  
5. Open the **Serial Monitor** at **115 200 baud**.  
6. You should see output similar to:

```
--- Mode Hardware UART (D7/D8) Actif ---
Ambiante: 24.5 C | MIN: 22.1 C | MAX: 31.8 C
                                
     ..::--==++**##%%@@@@##**   
    ..::-==++**##%%@@@@@@##**   
...
Attente 5 secondes...
```

---

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| `Erreur : Pas de reponse.` | Wiring issue — check TX/RX are not swapped |
| `Erreur : Trame incomplete (X/1540)` | Baud rate mismatch or loose connection |
| Garbled characters in the monitor | Make sure the Serial Monitor is set to **115 200 baud** |

---

## License

This project is released into the public domain — use it freely.
