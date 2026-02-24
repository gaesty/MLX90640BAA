"""
Example: Read a single thermal frame from the MLX90640BAA and print it.

Hardware setup:
    - MLX90640BAA connected to the Raspberry Pi I2C bus 1
      (SDA → GPIO 2, SCL → GPIO 3, 3.3 V, GND)
    - smbus2 installed:  pip install smbus2

Usage:
    python examples/read_temperature.py
"""

import smbus2

from mlx90640 import MLX90640
from mlx90640.driver import COLS, ROWS


def celsius_to_colour(temp: float, low: float = 20.0, high: float = 40.0) -> str:
    """Return a single ASCII character representing a temperature level."""
    t = max(0.0, min(1.0, (temp - low) / (high - low)))
    chars = " .:-=+*#%@"
    return chars[int(t * (len(chars) - 1))]


def main() -> None:
    bus = smbus2.SMBus(1)
    sensor = MLX90640(bus)

    sensor.set_refresh_rate(4)  # 4 Hz
    print("MLX90640BAA connected.  Reading one frame …\n")

    frame = sensor.get_frame()

    # Print ASCII art thermal map
    for row in range(ROWS):
        line = ""
        for col in range(COLS):
            temp = frame[row * COLS + col]
            line += celsius_to_colour(temp) * 2  # double-wide for aspect ratio
        print(line)

    print(f"\nMin: {min(frame):.1f} °C   Max: {max(frame):.1f} °C")
    bus.close()


if __name__ == "__main__":
    main()
