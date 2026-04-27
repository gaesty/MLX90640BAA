# ESP32 Sensor & Camera Projects

A collection of projects to read, display, and stream data from various sensors (Thermal, Video, Temperature, Humidity, Motion) using **ESP32**, **ESP32-CAM**, and **ESP32-S3** microcontrollers. Includes WebSockets for real-time browser dashboards and a companion Python script for thermal data recording.

---

## Hardware Requirements

| Component | Details |
|-----------|---------|
| ESP32 board | NodeMCU-32S, ESP32 DevKit, or equivalent |
| ESP32-CAM | AI-Thinker ESP32-CAM module |
| ESP32-S3 CAM | Freenove/Elegoo ESP32-S3 WROOM CAM module |
| GY-MLX90640BAA | 32×24 IR thermal camera module (UART version) |
| DHT11 | Temperature and Humidity sensor |
| HC-SR501 (PIR) | Passive Infrared Motion sensor |
| USB cable | For programming and power |
| Wi-Fi network | Required for the WebSocket and Streaming sketches |

---

## Wiring

### GY-MLX90640BAA (Thermal Camera)

The GY-MLX90640BAA communicates over **UART at 115200 baud**.

**ESP32 (`sketch_feb27a.ino`, `sketch_apr15a.ino`)**
Uses the hardware **Serial2** peripheral.
*   **VCC:** 3.3 V
*   **GND:** GND
*   **TX:** GPIO 16 (Serial2 RX)
*   **RX:** GPIO 17 (Serial2 TX)

### DHT11 & PIR Motion Sensor (`sketch_apr15b.ino`)

**ESP32 (NodeMCU-32S)**
*   **DHT11 Data:** GPIO 5
*   **PIR Output:** GPIO 22

---

## Repository Files

### Thermal Camera Sketches
*   **`sketch_apr15a.ino` / `sketch_feb27a.ino`** — ESP32 Wi-Fi WebSocket Heatmap. Uses `WiFiManager` to dynamically configure Wi-Fi. Serves an HTML page on port 80 and streams thermal frames via WebSockets on port 81.

### Video Streaming Sketches
*   **`sketch_apr15c.ino`** — ESP32-CAM (AI-Thinker). Uses `WiFiManager` to connect to Wi-Fi. Streams real-time JPEG frames to an embedded web dashboard via WebSockets.
*   **`sketch_apr15d.ino`** — ESP32-S3 WROOM CAM. Similar to the above but configured for the ESP32-S3 camera pinout.

### Environmental Dashboard
*   **`sketch_apr15b.ino`** — ESP32 Dashboard for DHT11 & PIR. Uses `WiFiManager` for easy setup. Sends JSON data (`{t, h, p}`) over WebSockets to a sleek web interface displaying temperature, humidity, and motion status.

### Utilities
*   **`export_py.py`** — Python script to connect to the Thermal Camera WebSockets and save raw frames as NumPy `.npy` files for offline analysis.

---

## Arduino Setup

### Required Libraries

Install the following libraries through the **Arduino Library Manager** (`Sketch → Include Library → Manage Libraries…`):

*   **WiFiManager** by tzapu (Required for newer sketches to avoid hardcoding credentials)
*   **WebSockets** by Markus Sattler (WebSocket server)
*   **DHT sensor library** by Adafruit (For `sketch_apr15b.ino`)
*   ESP32/ESP8266 core libraries and `esp_camera` (Built into the ESP32 board package)

### Connecting to Wi-Fi (WiFiManager)

Most sketches now use **WiFiManager**. Instead of hardcoding your SSID and Password:
1.  Upload the sketch to your board.
2.  The board will host an Access Point (e.g., `ESP32-CAM-Config`, `ESP32-Thermal-Config`, etc.).
3.  Connect to this network using your phone or PC.
4.  A captive portal will appear (or navigate to `192.168.4.1`).
5.  Select your home Wi-Fi network and enter the password.
6.  The board will reboot and connect to your network. Check the Serial Monitor (115200 baud) for the assigned IP address.

---

## Python Script Setup (Thermal Recording)

### Requirements

```
python >= 3.8
websocket-client
numpy
scipy
```

Install dependencies:

```bash
pip install websocket-client numpy scipy
```

### Usage

```bash
python export_py.py --ip <BOARD_IP> --output <save_directory>
```

#### Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--ip` | `10.28.26.7` | IP address of the ESP32 (shown in Serial Monitor after boot) |
| `--output` | `./dataset_thermique` | Directory where `.npy` frame files are saved |

#### Example

```bash
python export_py.py --ip 192.168.1.42 --output ./my_thermal_dataset
```

### Saved File Format

Each frame is saved as a **NumPy `.npy` file** containing a `float32` array of shape `(24, 32)` (24 rows × 32 columns, temperatures in °C).

File naming convention:
```
frame_<min_temp>_<max_temp>_<num_persons>_<num_hotspots>_<frame_index>.npy
```

Example: `frame_22.3_36.8_1_0_42.npy`

Load a saved frame in Python:
```python
import numpy as np
matrix = np.load("frame_22.3_36.8_1_0_42.npy")
print(matrix.shape)   # (24, 32)
print(matrix.min(), matrix.max())
```

---

## GY-MLX90640BAA UART Protocol (Summary)

| Byte | Value | Meaning |
|------|-------|---------|
| 0–1 | `0x5A 0x5A` | Frame header |
| 2 | `0x02` | Frame type: pixel data |
| 3 | `0x06` | Data length field |
| 4–1539 | — | 768 × 2 bytes, Little-Endian int16, divide by 100 for °C |
| 1540–1541 | — | Ambient temperature (TA), same encoding |
| 1542–1543 | — | Checksum |

Query command (request one frame):
```
0xA5  0x35  0x01  0xDB
```

---
