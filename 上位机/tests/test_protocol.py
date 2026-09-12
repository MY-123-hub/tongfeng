"""验证当前上位机源码与主机频率协议一致。"""

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
            'AA 55 02 10 01 00 02 01 64 00 02 E8 03 FD 41',
        )

    def test_v2_temperature_and_environment_layout(self):
        temp_payload = bytearray([0x00, 0x80] * 38)
        temp_payload[0:2] = (250).to_bytes(2, 'little', signed=True)
        temp_payload[72:74] = (205).to_bytes(2, 'little', signed=True)
        frame = ROOT_UPPER.LoRaProtocol.build_packet(
            ROOT_UPPER.LoRaProtocol.MSG_TEMP_36,
            ROOT_UPPER.LoRaProtocol.ROLE_HOST, 1,
            ROOT_UPPER.LoRaProtocol.ROLE_CONTROL, 0, 0x8000,
            bytes(temp_payload))
        self.assertEqual(len(frame), 89)
        parsed = ROOT_UPPER.LoRaProtocol.parse_packet(frame)
        decoded = ROOT_UPPER.LoRaProtocol.decode_temperature_data(parsed['data'])
        self.assertEqual(decoded['temperatures'][0], 25.0)
        self.assertEqual(decoded['slave_bme_temperature'], 20.5)
        self.assertIsNone(decoded['master_bme_temperature'])

        env_payload = bytearray([0xFF] * 86)
        env_payload[0:2] = (456).to_bytes(2, 'little')
        env_payload[72:74] = (550).to_bytes(2, 'little')
        env_payload[76:80] = (100000).to_bytes(4, 'little')
        env_frame = ROOT_UPPER.LoRaProtocol.build_packet(
            ROOT_UPPER.LoRaProtocol.MSG_ENV_DATA,
            ROOT_UPPER.LoRaProtocol.ROLE_HOST, 1,
            ROOT_UPPER.LoRaProtocol.ROLE_CONTROL, 0, 0x8001,
            bytes(env_payload))
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
