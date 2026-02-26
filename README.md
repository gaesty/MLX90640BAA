# MLX90640BAA — ESP8266 Thermal Camera

A project to read, display and record thermal data from a **GY-MLX90640BAA** (32×24 infrared sensor) using an **ESP8266** microcontroller, with a companion Python script to save frames for further analysis.

---

## Hardware Requirements

| Component | Details |
|-----------|---------|
| ESP8266 board | NodeMCU, Wemos D1 Mini, or equivalent |
| GY-MLX90640BAA | 32×24 IR thermal camera module (UART version) |
| USB cable | For programming and power |
| Wi-Fi network | Required for the WebSocket sketch (`sketch_feb25a.ino`) |

---

## Wiring

The GY-MLX90640BAA communicates over **UART at 115200 baud**.  
`Serial.swap()` is called in the sketches to move the hardware UART to pins **D7 (RX)** and **D8 (TX)**.

| GY-MLX90640BAA pin | ESP8266 pin |
|--------------------|-------------|
| VCC | 3.3 V or 5 V (check module label) |
| GND | GND |
| TX | D7 (GPIO 13) |
| RX | D8 (GPIO 15) |

> **Note:** The original hardware UART (GPIO 1 / GPIO 3) is re-used as a software debug serial port (`debugPC`) so you can still read log messages in the Arduino Serial Monitor.

---

## Repository Files

### `sketch_feb23a.ino` — Standalone Serial Debug Sketch

Reads one thermal frame every 5 seconds and prints:
- Ambient temperature, minimum and maximum pixel temperatures.
- A 32×12 ASCII art heatmap to the debug serial port (visible in the Arduino Serial Monitor).

**Use case:** Quick hardware verification without needing Wi-Fi or a PC client.

### `sketch_feb25a.ino` — Wi-Fi WebSocket Heatmap Sketch *(main sketch)*

Connects the ESP8266 to your Wi-Fi network and:
1. Serves an embedded HTML/JavaScript page on **port 80**.
2. Pushes raw 32×24 float frames over a **WebSocket on port 81** at ~1 Hz.
3. The browser page renders a live colour heatmap (blue → red).

**Use case:** Real-time visualisation from any browser on the same network, and data acquisition with `export_py.py`.

### `export_py.py` — Python Data Recording Script

Connects to the ESP8266 WebSocket server, receives binary float frames and saves each one as a NumPy `.npy` file.  
File names encode the frame index, temperature range, estimated person count (25–33 °C blobs) and hot-spot count (>33 °C blobs).

---

## Arduino Setup

### Required Libraries

Install the following libraries through the **Arduino Library Manager** (`Sketch → Include Library → Manage Libraries…`) or via the provided links:

| Library | Purpose |
|---------|---------|
| [ESP8266 Arduino Core](https://github.com/esp8266/Arduino) | Board support for ESP8266 |
| [WebSockets by Markus Sattler](https://github.com/Links2004/arduinoWebSockets) | WebSocket server (`sketch_feb25a.ino`) |

### Steps

1. Open the Arduino IDE and install the **ESP8266 board package** (add `https://arduino.esp8266.com/stable/package_esp8266com_index.json` in *Preferences → Additional boards manager URLs*).
2. Select your board under **Tools → Board → ESP8266 Boards**.
3. Open **`sketch_feb25a.ino`** (or `sketch_feb23a.ino` for a standalone test).
4. Edit the Wi-Fi credentials at the top of `sketch_feb25a.ino`:
   ```cpp
   const char* ssid     = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```
5. Upload the sketch. Open the Serial Monitor at **115200 baud** to see the assigned IP address.

---

## Python Script Setup

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
python export_py.py --ip <ESP8266_IP> --output <save_directory>
```

#### Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--ip` | `10.28.26.7` | IP address of the ESP8266 (shown in Serial Monitor after boot) |
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

## License

This project is released under the MIT License. See [LICENSE](LICENSE) for details.
