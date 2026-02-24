"""
Driver for the Melexis MLX90640BAA 32x24 IR thermal camera array.

The MLX90640BAA communicates over I2C (default address 0x33).
It provides a 32x24 pixel thermal image with a typical accuracy
of ±1 °C and a resolution of 0.1 °C.

References:
    Melexis MLX90640 Datasheet (3901090640 Rev 12)
"""

import struct
import time
from typing import List, Optional

# Default I2C address of the MLX90640
MLX90640_I2C_ADDR = 0x33

# Register addresses
_REG_STATUS = 0x8000
_REG_CTRL1 = 0x800D
_REG_I2C_CONF = 0x800F
_REG_EEPROM_START = 0x2400
_REG_RAM_START = 0x0400

# Dimensions
ROWS = 24
COLS = 32
PIXEL_COUNT = ROWS * COLS

# Subpage count (the sensor alternates between two subpages)
SUBPAGES = 2

# Refresh rate control field (bits [10:7] of 0x800D)
_REFRESH_RATE_MAP = {
    0.5: 0b000,
    1: 0b001,
    2: 0b010,
    4: 0b011,
    8: 0b100,
    16: 0b101,
    32: 0b110,
    64: 0b111,
}

# Reference ambient temperature (°C) used in the calibration equations
REFERENCE_TEMP_C = 25.0


class MLX90640Error(Exception):
    """Base exception for MLX90640 driver errors."""


class MLX90640:
    """Driver for the MLX90640BAA 32×24 thermal camera array.

    Usage example::

        import smbus2
        from mlx90640 import MLX90640

        bus = smbus2.SMBus(1)
        sensor = MLX90640(bus)
        sensor.set_refresh_rate(4)          # 4 Hz
        temps = sensor.get_frame()          # list of 768 floats (°C)
        print(f"Center pixel: {temps[12*32+16]:.1f} °C")
    """

    def __init__(self, bus, addr: int = MLX90640_I2C_ADDR) -> None:
        """Initialise the driver.

        Args:
            bus: An *smbus2.SMBus* instance (or any compatible object that
                 implements ``read_i2c_block_data`` and
                 ``write_i2c_block_data``).
            addr: I2C address of the sensor (default 0x33).
        """
        self._bus = bus
        self._addr = addr
        self._eeprom: Optional[List[int]] = None
        self._params: Optional[dict] = None

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def set_refresh_rate(self, hz: float) -> None:
        """Set the sensor refresh rate.

        Args:
            hz: Desired refresh rate in Hz.  Valid values: 0.5, 1, 2, 4,
                8, 16, 32, 64.

        Raises:
            ValueError: If *hz* is not a valid refresh rate.
            MLX90640Error: If the I2C transaction fails.
        """
        if hz not in _REFRESH_RATE_MAP:
            raise ValueError(
                f"Invalid refresh rate {hz} Hz.  "
                f"Valid values: {sorted(_REFRESH_RATE_MAP)}"
            )
        ctrl = self._read_word(_REG_CTRL1)
        ctrl = (ctrl & 0xFC7F) | (_REFRESH_RATE_MAP[hz] << 7)
        self._write_word(_REG_CTRL1, ctrl)

    def get_refresh_rate(self) -> float:
        """Return the currently configured refresh rate in Hz."""
        ctrl = self._read_word(_REG_CTRL1)
        bits = (ctrl >> 7) & 0x07
        for hz, code in _REFRESH_RATE_MAP.items():
            if code == bits:
                return hz
        raise MLX90640Error(f"Unexpected refresh-rate field value: {bits:#05b}")

    def read_eeprom(self) -> List[int]:
        """Read and cache the 832-word EEPROM.

        The EEPROM is only read once; subsequent calls return the cached
        copy.  Call :meth:`invalidate_eeprom` to force a re-read.

        Returns:
            List of 832 unsigned 16-bit integers.

        Raises:
            MLX90640Error: If the I2C transaction fails.
        """
        if self._eeprom is None:
            self._eeprom = self._read_words(_REG_EEPROM_START, 832)
        return self._eeprom

    def invalidate_eeprom(self) -> None:
        """Clear the cached EEPROM data so the next call to
        :meth:`read_eeprom` performs a fresh read."""
        self._eeprom = None
        self._params = None

    def get_frame(self) -> List[float]:
        """Capture a full 32×24 thermal frame (both subpages).

        Waits for each subpage to be ready and merges them into a single
        768-element list of temperatures in degrees Celsius, ordered
        row-major (pixel 0 is top-left).

        Returns:
            List[float] of length 768 (ROWS × COLS).

        Raises:
            MLX90640Error: On I2C or calibration errors.
        """
        if self._params is None:
            self._params = self._extract_params(self.read_eeprom())

        frame = [0.0] * PIXEL_COUNT

        for subpage in range(SUBPAGES):
            self._wait_for_data(subpage)
            raw = self._read_words(_REG_RAM_START, 832)
            self._clear_status_flag()
            self._process_subpage(raw, subpage, frame)

        return frame

    # ------------------------------------------------------------------
    # I2C helpers
    # ------------------------------------------------------------------

    def _read_word(self, reg: int) -> int:
        """Read a single 16-bit big-endian word from *reg*."""
        data = self._bus.read_i2c_block_data(
            self._addr, reg >> 8, 3, reg_size=2
        )
        return (data[0] << 8) | data[1]

    def _write_word(self, reg: int, value: int) -> None:
        """Write a single 16-bit big-endian word to *reg*."""
        self._bus.write_i2c_block_data(
            self._addr,
            reg >> 8,
            [(reg & 0xFF), (value >> 8) & 0xFF, value & 0xFF],
            reg_size=1,
        )

    def _read_words(self, reg: int, count: int) -> List[int]:
        """Read *count* consecutive 16-bit words starting at *reg*.

        Returns them as a list of unsigned integers.
        """
        words: List[int] = []
        # smbus2 can read at most 32 bytes (16 words) per transaction on
        # many hosts; chunk accordingly.
        chunk_size = 16
        for offset in range(0, count, chunk_size):
            n = min(chunk_size, count - offset)
            raw = self._bus.read_i2c_block_data(
                self._addr,
                (reg + offset) >> 8,
                n * 2,
                reg_size=2,
            )
            for i in range(n):
                words.append((raw[i * 2] << 8) | raw[i * 2 + 1])
        return words

    # ------------------------------------------------------------------
    # Frame acquisition helpers
    # ------------------------------------------------------------------

    def _wait_for_data(self, subpage: int, timeout: float = 2.0) -> None:
        """Poll the status register until the requested subpage is ready.

        Args:
            subpage: 0 or 1.
            timeout: Maximum time to wait in seconds.

        Raises:
            MLX90640Error: If no data is ready within *timeout* seconds.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = self._read_word(_REG_STATUS)
            data_ready = (status >> 3) & 1
            current_subpage = status & 1
            if data_ready and current_subpage == subpage:
                return
            time.sleep(0.01)
        raise MLX90640Error(
            f"Timeout waiting for subpage {subpage} data ready"
        )

    def _clear_status_flag(self) -> None:
        """Clear the data-ready bit in the status register."""
        status = self._read_word(_REG_STATUS)
        self._write_word(_REG_STATUS, status & ~(1 << 3))

    # ------------------------------------------------------------------
    # Calibration / temperature calculation
    # ------------------------------------------------------------------

    def _extract_params(self, eeprom: List[int]) -> dict:
        """Extract calibration parameters from the EEPROM data.

        This implements the parameter extraction described in the
        MLX90640 datasheet (section 11).

        Returns:
            dict with calibration coefficients.
        """
        params: dict = {}

        # --- Vdd sensor ---
        kVdd = (eeprom[0x33] & 0xFF00) >> 8
        if kVdd > 127:
            kVdd -= 256
        kVdd *= 32
        vdd25 = eeprom[0x33] & 0x00FF
        vdd25 = ((vdd25 - 256) << 5) - 8192
        params["kVdd"] = kVdd
        params["vdd25"] = vdd25

        # --- PTAT ---
        KvPTAT = (eeprom[0x32] & 0xFC00) >> 10
        if KvPTAT > 31:
            KvPTAT -= 64
        KvPTAT /= 4096.0
        KtPTAT = eeprom[0x32] & 0x03FF
        if KtPTAT > 511:
            KtPTAT -= 1024
        KtPTAT /= 8.0
        vPTAT25 = eeprom[0x31]
        alphaPTAT = (eeprom[0x10] & 0xF000) / 4.0 + 8.0
        params["KvPTAT"] = KvPTAT
        params["KtPTAT"] = KtPTAT
        params["vPTAT25"] = vPTAT25
        params["alphaPTAT"] = alphaPTAT

        # --- Gain ---
        params["gain"] = eeprom[0x30]
        if params["gain"] > 32767:
            params["gain"] -= 65536

        # --- Tgc ---
        tgc = eeprom[0x3C] & 0x00FF
        if tgc > 127:
            tgc -= 256
        params["tgc"] = tgc / 32.0

        # --- Resolution ---
        params["res_corr"] = (eeprom[0x38] & 0x3000) >> 12

        # --- KsTa ---
        KsTa = (eeprom[0x3C] & 0xFF00) >> 8
        if KsTa > 127:
            KsTa -= 256
        params["KsTa"] = KsTa / 8192.0

        # --- KsTo ---
        step = ((eeprom[0x3F] & 0x3000) >> 12) * 10
        params["CT"] = [
            -40,
            0,
            (eeprom[0x3F] & 0x00F0) >> 4,
            (eeprom[0x3F] & 0x0F00) >> 8,
        ]
        params["CT"][2] = params["CT"][2] * step
        params["CT"][3] = params["CT"][2] + params["CT"][3] * step

        KsToScale = (eeprom[0x3F] & 0x000F) + 8
        KsTo = [0.0] * 4
        for i in range(4):
            val = eeprom[0x3D + (i // 2)]
            shift = 0 if i % 2 == 0 else 8
            k = (val >> shift) & 0xFF
            if k > 127:
                k -= 256
            KsTo[i] = k / (1 << KsToScale)
        params["KsTo"] = KsTo

        # --- Alpha (sensitivity) per pixel ---
        accRemScale = eeprom[0x20] & 0x000F
        accColumnScale = (eeprom[0x20] & 0x00F0) >> 4
        accRowScale = (eeprom[0x20] & 0x0F00) >> 8
        alphaScale = ((eeprom[0x20] & 0xF000) >> 12) + 30
        alphaRef = eeprom[0x21]
        accRow = [0] * ROWS
        accCol = [0] * COLS
        for i in range(6):
            p = i * 4
            accRow[p] = (eeprom[0x22 + i] & 0x000F)
            accRow[p + 1] = (eeprom[0x22 + i] & 0x00F0) >> 4
            accRow[p + 2] = (eeprom[0x22 + i] & 0x0F00) >> 8
            accRow[p + 3] = (eeprom[0x22 + i] & 0xF000) >> 12
        for i in range(ROWS):
            if accRow[i] > 7:
                accRow[i] -= 16
        for i in range(8):
            p = i * 4
            accCol[p] = (eeprom[0x28 + i] & 0x000F)
            accCol[p + 1] = (eeprom[0x28 + i] & 0x00F0) >> 4
            accCol[p + 2] = (eeprom[0x28 + i] & 0x0F00) >> 8
            accCol[p + 3] = (eeprom[0x28 + i] & 0xF000) >> 12
        for i in range(COLS):
            if accCol[i] > 7:
                accCol[i] -= 16

        alpha = [0.0] * PIXEL_COUNT
        for i in range(PIXEL_COUNT):
            row = i // COLS
            col = i % COLS
            p = (eeprom[0x40 + i] & 0x03F0) >> 4
            if p > 31:
                p -= 64
            alpha[i] = (
                alphaRef
                + (accRow[row] << accRowScale)
                + (accCol[col] << accColumnScale)
                + (p << accRemScale)
            ) / (1 << alphaScale)
        params["alpha"] = alpha

        # --- Offset per pixel ---
        occRemScale = eeprom[0x10] & 0x000F
        occColumnScale = (eeprom[0x10] & 0x00F0) >> 4
        occRowScale = (eeprom[0x10] & 0x0F00) >> 8
        offsetRef = eeprom[0x11]
        if offsetRef > 32767:
            offsetRef -= 65536
        occRow = [0] * ROWS
        occCol = [0] * COLS
        for i in range(6):
            p = i * 4
            occRow[p] = (eeprom[0x12 + i] & 0x000F)
            occRow[p + 1] = (eeprom[0x12 + i] & 0x00F0) >> 4
            occRow[p + 2] = (eeprom[0x12 + i] & 0x0F00) >> 8
            occRow[p + 3] = (eeprom[0x12 + i] & 0xF000) >> 12
        for i in range(ROWS):
            if occRow[i] > 7:
                occRow[i] -= 16
        for i in range(8):
            p = i * 4
            occCol[p] = (eeprom[0x18 + i] & 0x000F)
            occCol[p + 1] = (eeprom[0x18 + i] & 0x00F0) >> 4
            occCol[p + 2] = (eeprom[0x18 + i] & 0x0F00) >> 8
            occCol[p + 3] = (eeprom[0x18 + i] & 0xF000) >> 12
        for i in range(COLS):
            if occCol[i] > 7:
                occCol[i] -= 16

        offset = [0] * PIXEL_COUNT
        for i in range(PIXEL_COUNT):
            row = i // COLS
            col = i % COLS
            p = (eeprom[0x40 + i] & 0xFC00) >> 10
            if p > 31:
                p -= 64
            offset[i] = (
                offsetRef
                + (occRow[row] << occRowScale)
                + (occCol[col] << occColumnScale)
                + (p << occRemScale)
            )
        params["offset"] = offset

        # --- Kta per pixel ---
        KtaRoCo = (eeprom[0x36] & 0xFF00) >> 8
        if KtaRoCo > 127:
            KtaRoCo -= 256
        KtaReCo = (eeprom[0x36] & 0x00FF)
        if KtaReCo > 127:
            KtaReCo -= 256
        KtaRoCe = (eeprom[0x37] & 0xFF00) >> 8
        if KtaRoCe > 127:
            KtaRoCe -= 256
        KtaReCe = (eeprom[0x37] & 0x00FF)
        if KtaReCe > 127:
            KtaReCe -= 256
        ktaScale1 = ((eeprom[0x38] & 0x00F0) >> 4) + 8
        ktaScale2 = (eeprom[0x38] & 0x000F)

        kta = [0.0] * PIXEL_COUNT
        for i in range(PIXEL_COUNT):
            row = i // COLS
            col = i % COLS
            raw = (eeprom[0x40 + i] & 0x000E) >> 1
            if raw > 3:
                raw -= 8
            if row % 2 == 0 and col % 2 == 0:
                base = KtaRoCo
            elif row % 2 == 1 and col % 2 == 0:
                base = KtaReCo
            elif row % 2 == 0 and col % 2 == 1:
                base = KtaRoCe
            else:
                base = KtaReCe
            kta[i] = (base + (raw << ktaScale2)) / (1 << ktaScale1)
        params["kta"] = kta

        # --- Kv per pixel (column-based) ---
        KvRoCo = (eeprom[0x34] & 0xF000) >> 12
        if KvRoCo > 7:
            KvRoCo -= 16
        KvReCo = (eeprom[0x34] & 0x0F00) >> 8
        if KvReCo > 7:
            KvReCo -= 16
        KvRoCe = (eeprom[0x34] & 0x00F0) >> 4
        if KvRoCe > 7:
            KvRoCe -= 16
        KvReCe = (eeprom[0x34] & 0x000F)
        if KvReCe > 7:
            KvReCe -= 16
        kvScale = (eeprom[0x38] & 0x0F00) >> 8

        kv = [0.0] * PIXEL_COUNT
        for i in range(PIXEL_COUNT):
            row = i // COLS
            col = i % COLS
            if row % 2 == 0 and col % 2 == 0:
                kv[i] = KvRoCo / (1 << kvScale)
            elif row % 2 == 1 and col % 2 == 0:
                kv[i] = KvReCo / (1 << kvScale)
            elif row % 2 == 0 and col % 2 == 1:
                kv[i] = KvRoCe / (1 << kvScale)
            else:
                kv[i] = KvReCe / (1 << kvScale)
        params["kv"] = kv

        # --- CP (compensation pixel) ---
        alphaScale_CP = (eeprom[0x20] & 0xF000) >> 12
        cpP1P0ratio = (eeprom[0x39] & 0xFC00) >> 10
        if cpP1P0ratio > 31:
            cpP1P0ratio -= 64
        offsetSP0 = eeprom[0x3A] & 0x03FF
        if offsetSP0 > 511:
            offsetSP0 -= 1024
        offsetSP1 = offsetSP0 + ((eeprom[0x3A] & 0xFC00) >> 10)
        if offsetSP1 > 511:
            offsetSP1 -= 1024
        cpAlpha = [0.0, 0.0]
        cpAlpha[0] = (eeprom[0x39] & 0x03FF) / (1 << (alphaScale_CP + 27))
        cpAlpha[1] = cpAlpha[0] * (1 + cpP1P0ratio / 128.0)
        cpKta = (eeprom[0x3B] & 0x00FF)
        if cpKta > 127:
            cpKta -= 256
        cpKtaScale = (eeprom[0x38] & 0x00F0) >> 4
        cpKta /= 1 << (cpKtaScale + 8)
        cpKv = (eeprom[0x3B] & 0xFF00) >> 8
        if cpKv > 127:
            cpKv -= 256
        cpKv /= 1 << (kvScale + 8)
        params["cpOffset"] = [offsetSP0, offsetSP1]
        params["cpAlpha"] = cpAlpha
        params["cpKta"] = cpKta
        params["cpKv"] = cpKv

        return params

    def _process_subpage(
        self,
        raw: List[int],
        subpage: int,
        frame: List[float],
    ) -> None:
        """Calculate temperatures for one subpage and update *frame*.

        Implements the temperature calculation from MLX90640 datasheet
        section 11.

        Args:
            raw:     832-word RAM dump.
            subpage: 0 or 1.
            frame:   Output list (modified in place).
        """
        params = self._params

        # --- Restore resolution ---
        # RAM starts at 0x0400; index into the raw list is (reg - 0x0400).
        res_ram = (raw[0x039F] >> 10) & 0x03

        def r(reg: int) -> int:
            return raw[reg - _REG_RAM_START]

        # Restore Vdd
        vdd_raw = r(0x072A)
        if vdd_raw > 32767:
            vdd_raw -= 65536
        res_corr = (1 << params["res_corr"]) / (1 << res_ram)
        Vdd = (res_corr * vdd_raw - params["vdd25"]) / params["kVdd"] + 3.3

        # Restore Ta (ambient temperature)
        vPTAT_art = r(0x0720)
        if vPTAT_art > 32767:
            vPTAT_art -= 65536
        vPTAT = r(0x0700)
        if vPTAT > 32767:
            vPTAT -= 65536
        vPTAT_art = (vPTAT / (vPTAT * params["alphaPTAT"] + vPTAT_art)) * 131072.0
        Ta = (
            vPTAT_art / (1 + params["KvPTAT"] * (Vdd - 3.3))
            - params["vPTAT25"]
        ) / params["KtPTAT"] + REFERENCE_TEMP_C

        # Gain
        gain_raw = r(0x070A)
        if gain_raw > 32767:
            gain_raw -= 65536
        gain = params["gain"] / gain_raw

        # CP pixel
        cpP0 = r(0x0708)
        if cpP0 > 32767:
            cpP0 -= 65536
        cpP1 = r(0x0728)
        if cpP1 > 32767:
            cpP1 -= 65536
        cpSP = [
            cpP0 * gain - params["cpOffset"][0] * (
                1 + params["cpKta"] * (Ta - REFERENCE_TEMP_C)
            ) * (1 + params["cpKv"] * (Vdd - 3.3)),
            cpP1 * gain - params["cpOffset"][1] * (
                1 + params["cpKta"] * (Ta - REFERENCE_TEMP_C)
            ) * (1 + params["cpKv"] * (Vdd - 3.3)),
        ]

        # Pixel temperatures
        for i in range(PIXEL_COUNT):
            row = i // COLS
            col = i % COLS
            # Pixels belong to subpages based on (row+col) % 2
            if (row + col) % 2 != subpage:
                continue

            pix_reg = _REG_RAM_START + i
            pix_raw = r(pix_reg)
            if pix_raw > 32767:
                pix_raw -= 65536

            # IR pixel offset removal
            pix_gain = pix_raw * gain
            pix_os = (
                pix_gain
                - params["offset"][i] * (
                    1 + params["kta"][i] * (Ta - REFERENCE_TEMP_C)
                ) * (1 + params["kv"][i] * (Vdd - 3.3))
            )

            # CP subtraction
            pix_os -= params["tgc"] * cpSP[subpage]

            # Emissivity correction (assume ε = 1)
            pix_comp = pix_os

            # Object temperature
            Sx = (
                params["KsTo"][1]
                * (pix_comp / params["alpha"][i]) ** 3
                * pix_comp
            )
            To = (
                (pix_comp / (params["alpha"][i] * (1 - params["KsTo"][1] * REFERENCE_TEMP_C) + Sx))
                + (Ta + 273.15) ** 4
            ) ** 0.25 - 273.15

            frame[i] = To
