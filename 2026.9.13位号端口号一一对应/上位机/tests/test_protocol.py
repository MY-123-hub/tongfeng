"""验证上位机源码与主机 V3 LoRa 协议一致。"""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import unittest


ROOT_MAIN = Path(__file__).resolve().parents[1] / 'main.py'
SPEC = spec_from_file_location('root_upper_main', ROOT_MAIN)
ROOT_UPPER = module_from_spec(SPEC)
SPEC.loader.exec_module(ROOT_UPPER)


class RootUpperProtocolTests(unittest.TestCase):
    def test_10hz_is_encoded_as_x100_little_endian(self):
        frame = ROOT_UPPER.LoRaProtocol.cmd_set_freq(1, 100, 10.00)
        self.assertEqual(frame[11:13], bytes((0xE8, 0x03)))
        self.assertEqual(
            frame.hex(' ').upper(),
            'AA 55 03 10 01 00 02 01 64 00 02 E8 03 F9 BD',
        )

    def test_v3_temperature_layout_preserves_position_and_temperature(self):
        temp_payload = bytearray(112)
        temp_payload[0:3] = bytes((1, 0xFA, 0x00))
        temp_payload[3:6] = bytes((2, 0x00, 0x80))
        temp_payload[105:108] = bytes((6, 0x05, 0x01))
        temp_payload[108:110] = (205).to_bytes(2, 'little', signed=True)
        frame = ROOT_UPPER.LoRaProtocol.build_packet(
            ROOT_UPPER.LoRaProtocol.MSG_TEMP_36,
            ROOT_UPPER.LoRaProtocol.ROLE_HOST, 1,
            ROOT_UPPER.LoRaProtocol.ROLE_CONTROL, 0, 0x8000,
            bytes(temp_payload))
        self.assertEqual(frame[2], 0x03)
        self.assertEqual(len(frame), 125)
        decoded = ROOT_UPPER.LoRaProtocol.decode_temperature_data(
            ROOT_UPPER.LoRaProtocol.parse_packet(frame)['data'])
        self.assertEqual(decoded['positions'][0], 1)
        self.assertEqual(decoded['temperatures'][0], 25.0)
        self.assertEqual(decoded['positions'][1], 2)
        self.assertIsNone(decoded['temperatures'][1])
        self.assertEqual(decoded['positions'][35], 6)
        self.assertEqual(decoded['temperatures'][35], 26.1)
        self.assertEqual(decoded['slave_bme_temperature'], 20.5)

    def test_v3_environment_layout_is_unchanged(self):
        env_payload = bytearray([0xFF] * 86)
        env_payload[0:2] = (456).to_bytes(2, 'little')
        env_payload[72:74] = (550).to_bytes(2, 'little')
        env_payload[76:80] = (100000).to_bytes(4, 'little')
        env_frame = ROOT_UPPER.LoRaProtocol.build_packet(
            ROOT_UPPER.LoRaProtocol.MSG_ENV_DATA,
            ROOT_UPPER.LoRaProtocol.ROLE_HOST, 1,
            ROOT_UPPER.LoRaProtocol.ROLE_CONTROL, 0, 0x8001,
            bytes(env_payload))
        self.assertEqual(env_frame[2], 0x03)
        self.assertEqual(len(env_frame), 99)
        env = ROOT_UPPER.LoRaProtocol.decode_environment_data(
            ROOT_UPPER.LoRaProtocol.parse_packet(env_frame)['data'])
        self.assertEqual(env['humidities'][0], 45.6)
        self.assertEqual(env['slave_bme_pressure_pa'], 100000)
        self.assertIsNone(env['master_bme_pressure_pa'])
        self.assertIsNone(env['rain'])

    def test_frequency_range_matches_master(self):
        with self.assertRaises(ValueError):
            ROOT_UPPER.LoRaProtocol.cmd_set_freq(1, 100, 50.01)


if __name__ == '__main__':
    unittest.main()

