# MLX90640BAA

A Python testing library and example scripts for the **Melexis MLX90640BAA**
32×24 far-infrared thermal camera array.

## Sensor overview

| Property | Value |
|---|---|
| Resolution | 32 × 24 pixels (768 pixels total) |
| Field of view | 55° × 35° (BAA variant) |
| Accuracy | ±1 °C (typical) |
| Temperature range | −40 °C … 300 °C |
| Interface | I²C (default address `0x33`) |
| Refresh rate | 0.5 – 64 Hz (configurable) |
| Supply voltage | 3.3 V |

## Repository layout

```
mlx90640/          Python driver package
  __init__.py
  driver.py        MLX90640 class + calibration/temperature calculation
examples/
  read_temperature.py   ASCII art thermal viewer (requires hardware)
tests/
  test_driver.py   pytest unit tests (no hardware needed)
requirements.txt
```

## Installation

```bash
pip install -r requirements.txt
```

Requires Python 3.8 or later.

## Quick start

```python
import smbus2
from mlx90640 import MLX90640

bus = smbus2.SMBus(1)          # Raspberry Pi I2C bus 1
sensor = MLX90640(bus)

sensor.set_refresh_rate(4)     # 4 Hz
frame = sensor.get_frame()     # list of 768 temperatures (°C)

# Center pixel
print(f"Center: {frame[12 * 32 + 16]:.1f} °C")
bus.close()
```

See `examples/read_temperature.py` for an ASCII art thermal map viewer.

## Running the tests

```bash
pytest
```

The unit tests mock all I2C communication so no hardware is required.

## Hardware wiring (Raspberry Pi)

| MLX90640 pin | Raspberry Pi pin |
|---|---|
| VDD | 3.3 V (pin 1 or 17) |
| GND | GND (pin 6, 9, …) |
| SDA | GPIO 2 / SDA1 (pin 3) |
| SCL | GPIO 3 / SCL1 (pin 5) |

Enable I2C on the Raspberry Pi with `raspi-config → Interface Options → I2C`.

## References

- [Melexis MLX90640 product page](https://www.melexis.com/en/product/MLX90640/Far-Infrared-Sensor-Array)
- [MLX90640 datasheet (Rev 12)](https://www.melexis.com/en/documents/documentation/datasheets/datasheet-mlx90640)
