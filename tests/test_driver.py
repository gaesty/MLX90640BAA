"""Unit tests for the MLX90640 driver.

All I2C interactions are mocked so these tests run without hardware.
"""

import math
import struct
from unittest.mock import MagicMock, call, patch

import pytest

from mlx90640 import MLX90640
from mlx90640.driver import (
    COLS,
    MLX90640Error,
    MLX90640_I2C_ADDR,
    PIXEL_COUNT,
    ROWS,
    _REG_CTRL1,
    _REG_STATUS,
    _REFRESH_RATE_MAP,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_bus(read_word_returns=None, read_words_returns=None):
    """Return a mock SMBus object."""
    bus = MagicMock()
    return bus


def build_eeprom():
    """Return a minimal 832-word EEPROM that yields sane calibration values."""
    eeprom = [0] * 832

    # Scale factors
    eeprom[0x20] = (
        (12 << 12)   # alphaScale = 12 + 30 = 42
        | (4 << 8)   # accRowScale
        | (4 << 4)   # accColumnScale
        | 4          # accRemScale
    )
    eeprom[0x10] = (
        (3 << 8)     # occRowScale
        | (3 << 4)   # occColumnScale
        | 3          # occRemScale
    )

    # PTAT / Vdd calibration – safe default values
    eeprom[0x31] = 0x6000   # vPTAT25
    eeprom[0x32] = 0x0000   # KvPTAT=0, KtPTAT=0
    eeprom[0x33] = 0x0000   # kVdd=0 raw → *32=0  ← will be patched
    # Make kVdd non-zero to avoid div/0
    eeprom[0x33] = (0x20 << 8) | 0x00  # kVdd raw=32 → *32=1024

    # Gain
    eeprom[0x30] = 0x1000  # 4096

    # Tgc
    eeprom[0x3C] = 0x0000  # tgc=0, KsTa=0

    # Resolution
    eeprom[0x38] = (2 << 12) | (2 << 8) | (2 << 4) | 2

    # KsTo ranges
    eeprom[0x3F] = (2 << 12) | (0x05 << 4) | (0x03 << 8) | 5
    eeprom[0x3D] = 0x0000
    eeprom[0x3E] = 0x0000

    # CP
    eeprom[0x39] = 0x0001   # cpAlpha[0] tiny, cpP1P0ratio=0
    eeprom[0x3A] = 0x0001   # offsetSP0=1
    eeprom[0x3B] = 0x0000   # cpKta=0, cpKv=0

    # alpha per pixel (all zero → each alpha computed from accRow/accCol/alphaRef)
    eeprom[0x21] = 100  # alphaRef

    # offset per pixel – reference
    eeprom[0x11] = 0   # offsetRef = 0

    return eeprom


# ---------------------------------------------------------------------------
# Basic attribute tests
# ---------------------------------------------------------------------------

class TestInit:
    def test_default_address(self):
        bus = MagicMock()
        sensor = MLX90640(bus)
        assert sensor._addr == MLX90640_I2C_ADDR

    def test_custom_address(self):
        bus = MagicMock()
        sensor = MLX90640(bus, addr=0x40)
        assert sensor._addr == 0x40

    def test_eeprom_initially_none(self):
        sensor = MLX90640(MagicMock())
        assert sensor._eeprom is None


# ---------------------------------------------------------------------------
# Refresh rate tests
# ---------------------------------------------------------------------------

class TestRefreshRate:
    def _make_sensor_with_ctrl(self, ctrl_value):
        bus = MagicMock()
        # _read_word returns the current ctrl register value
        bus.read_i2c_block_data.return_value = [
            (ctrl_value >> 8) & 0xFF,
            ctrl_value & 0xFF,
            0,
        ]
        return MLX90640(bus)

    @pytest.mark.parametrize("hz", sorted(_REFRESH_RATE_MAP))
    def test_set_valid_refresh_rate(self, hz):
        sensor = self._make_sensor_with_ctrl(0x0901)
        sensor.set_refresh_rate(hz)
        # write_i2c_block_data must have been called
        assert sensor._bus.write_i2c_block_data.called

    def test_set_invalid_refresh_rate_raises(self):
        sensor = self._make_sensor_with_ctrl(0x0901)
        with pytest.raises(ValueError, match="Invalid refresh rate"):
            sensor.set_refresh_rate(3)

    @pytest.mark.parametrize("hz,code", _REFRESH_RATE_MAP.items())
    def test_get_refresh_rate(self, hz, code):
        ctrl = code << 7
        sensor = self._make_sensor_with_ctrl(ctrl)
        assert sensor.get_refresh_rate() == hz


# ---------------------------------------------------------------------------
# EEPROM caching tests
# ---------------------------------------------------------------------------

class TestEEPROM:
    def test_eeprom_cached_after_first_read(self):
        bus = MagicMock()
        # Return 16 words (32 bytes) per read_i2c_block_data call, all zeros.
        bus.read_i2c_block_data.return_value = [0] * 32
        sensor = MLX90640(bus)

        eeprom1 = sensor.read_eeprom()
        call_count_after_first = bus.read_i2c_block_data.call_count

        eeprom2 = sensor.read_eeprom()
        assert bus.read_i2c_block_data.call_count == call_count_after_first
        assert eeprom1 is eeprom2

    def test_invalidate_eeprom_clears_cache(self):
        bus = MagicMock()
        bus.read_i2c_block_data.return_value = [0] * 32
        sensor = MLX90640(bus)

        sensor.read_eeprom()
        call_count = bus.read_i2c_block_data.call_count

        sensor.invalidate_eeprom()
        assert sensor._eeprom is None
        assert sensor._params is None

        sensor.read_eeprom()
        assert bus.read_i2c_block_data.call_count > call_count

    def test_eeprom_length(self):
        bus = MagicMock()
        bus.read_i2c_block_data.return_value = [0] * 32
        sensor = MLX90640(bus)
        eeprom = sensor.read_eeprom()
        assert len(eeprom) == 832


# ---------------------------------------------------------------------------
# Constants tests
# ---------------------------------------------------------------------------

class TestConstants:
    def test_dimensions(self):
        assert ROWS == 24
        assert COLS == 32
        assert PIXEL_COUNT == ROWS * COLS

    def test_refresh_rate_map_has_eight_entries(self):
        assert len(_REFRESH_RATE_MAP) == 8

    def test_default_i2c_addr(self):
        assert MLX90640_I2C_ADDR == 0x33


# ---------------------------------------------------------------------------
# Error handling
# ---------------------------------------------------------------------------

class TestErrors:
    def test_mlx90640_error_is_exception(self):
        err = MLX90640Error("test")
        assert isinstance(err, Exception)

    def test_wait_for_data_timeout(self):
        """_wait_for_data raises MLX90640Error after timeout."""
        bus = MagicMock()
        # Status register always returns data_not_ready for subpage 0
        bus.read_i2c_block_data.return_value = [0x00, 0x00, 0x00]
        sensor = MLX90640(bus)
        with pytest.raises(MLX90640Error, match="Timeout"):
            sensor._wait_for_data(subpage=0, timeout=0.05)


# ---------------------------------------------------------------------------
# Calibration parameter extraction smoke test
# ---------------------------------------------------------------------------

class TestParamExtraction:
    def test_extract_params_returns_required_keys(self):
        bus = MagicMock()
        sensor = MLX90640(bus)
        eeprom = build_eeprom()
        params = sensor._extract_params(eeprom)

        required_keys = {
            "kVdd", "vdd25", "KvPTAT", "KtPTAT", "vPTAT25", "alphaPTAT",
            "gain", "tgc", "res_corr", "KsTa", "CT", "KsTo",
            "alpha", "offset", "kta", "kv",
            "cpOffset", "cpAlpha", "cpKta", "cpKv",
        }
        assert required_keys.issubset(params.keys())

    def test_alpha_length(self):
        bus = MagicMock()
        sensor = MLX90640(bus)
        params = sensor._extract_params(build_eeprom())
        assert len(params["alpha"]) == PIXEL_COUNT

    def test_offset_length(self):
        bus = MagicMock()
        sensor = MLX90640(bus)
        params = sensor._extract_params(build_eeprom())
        assert len(params["offset"]) == PIXEL_COUNT

    def test_kta_length(self):
        bus = MagicMock()
        sensor = MLX90640(bus)
        params = sensor._extract_params(build_eeprom())
        assert len(params["kta"]) == PIXEL_COUNT

    def test_kv_length(self):
        bus = MagicMock()
        sensor = MLX90640(bus)
        params = sensor._extract_params(build_eeprom())
        assert len(params["kv"]) == PIXEL_COUNT
