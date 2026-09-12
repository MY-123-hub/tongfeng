"""不启动 Tk、不打开真实串口的上位机协议测试。"""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import unittest


UPPER_MAIN = Path(__file__).resolve().parents[1] / '上位机' / 'main.py'
SPEC = spec_from_file_location('upper_main', UPPER_MAIN)
UPPER = module_from_spec(SPEC)
SPEC.loader.exec_module(UPPER)
LoRaProtocol = UPPER.LoRaProtocol
SerialManager = UPPER.SerialManager


class UpperProtocolTests(unittest.TestCase):
    def test_set_frequency_50hz_matches_master_protocol(self):
        frame = LoRaProtocol.cmd_set_freq(1, 100, 50.00)
        self.assertEqual(
            frame.hex(' ').upper(),
            'AA 55 02 10 01 00 02 01 64 00 02 88 13 D4 8D',
        )

    def test_set_frequency_10hz_uses_x100_little_endian(self):
        frame = LoRaProtocol.cmd_set_freq(1, 100, 10.00)
        self.assertEqual(frame[11:13], bytes((0xE8, 0x03)))
        self.assertEqual(
            frame.hex(' ').upper(),
            'AA 55 02 10 01 00 02 01 64 00 02 E8 03 FD 41',
        )

    def test_target_temperature_is_signed_x10(self):
        self.assertEqual(
            LoRaProtocol.cmd_set_target_temp(1, 100, 26.0)[11:13], bytes((0x04, 0x01)))
        self.assertEqual(
            LoRaProtocol.cmd_set_target_temp(1, 100, -5.5)[11:13], bytes((0xC9, 0xFF)))

    def test_temperature_frame_is_89_bytes_and_uses_v2_invalid_value(self):
        raw_values = [0, -55, 250] + [-32768] * 33 + [205, -32768]
        payload = b''.join(value.to_bytes(2, 'little', signed=True)
                           for value in raw_values)
        frame = LoRaProtocol.build_packet(LoRaProtocol.MSG_TEMP_36,
                                          LoRaProtocol.ROLE_HOST, 1,
                                          LoRaProtocol.ROLE_CONTROL, 0, 0x8000, payload)
        self.assertEqual(len(frame), 89)
        parsed = LoRaProtocol.parse_packet(frame)
        decoded = LoRaProtocol.decode_temperature_data(parsed['data'])
        self.assertEqual(decoded['temperatures'][:3], [0.0, -5.5, 25.0])
        self.assertEqual(decoded['slave_bme_temperature'], 20.5)
        self.assertIsNone(decoded['master_bme_temperature'])

    def test_environment_frame_is_99_bytes_and_rain_is_unavailable(self):
        payload = bytearray([0xFF] * LoRaProtocol.ENV_DATA_LEN)
        payload[0:2] = (456).to_bytes(2, 'little')
        payload[72:74] = (550).to_bytes(2, 'little')
        payload[76:80] = (100000).to_bytes(4, 'little')
        frame = LoRaProtocol.build_packet(
            LoRaProtocol.MSG_ENV_DATA, LoRaProtocol.ROLE_HOST, 1,
            LoRaProtocol.ROLE_CONTROL, 0, 0x8001, bytes(payload))
        self.assertEqual(len(frame), 99)
        decoded = LoRaProtocol.decode_environment_data(
            LoRaProtocol.parse_packet(frame)['data'])
        self.assertEqual(decoded['humidities'][0], 45.6)
        self.assertEqual(decoded['slave_bme_humidity'], 55.0)
        self.assertEqual(decoded['slave_bme_pressure_pa'], 100000)
        self.assertIsNone(decoded['rain'])

    def test_bad_crc_length_and_destination_are_rejected(self):
        frame = bytearray(LoRaProtocol.cmd_set_freq(1, 100, 50.0))
        frame[-1] ^= 1
        self.assertIsNone(LoRaProtocol.parse_packet(bytes(frame)))

        valid = LoRaProtocol.cmd_set_freq(1, 100, 50.0)
        self.assertIsNone(LoRaProtocol.parse_packet(valid + b'\x00'))

        wrong_destination = LoRaProtocol.build_packet(LoRaProtocol.MSG_ACK,
                                                       LoRaProtocol.ROLE_HOST, 1,
                                                       LoRaProtocol.ROLE_HOST, 1,
                                                       100, bytes((0, 0)))
        self.assertFalse(LoRaProtocol.validate_control_room_response(
            LoRaProtocol.parse_packet(wrong_destination)))

    def test_serial_receiver_only_emits_complete_binary_frames(self):
        received = []
        receiver = SerialManager(callback=lambda frame, binary: received.append((frame, binary)))
        frame = LoRaProtocol.cmd_read_temp(1, 100)

        receiver._buffer.extend(b'noise\xAA')
        receiver._process_buffer()
        self.assertEqual(received, [('noise', False)])
        self.assertEqual(bytes(receiver._buffer), b'\xAA')

        receiver._buffer.extend(frame[1:7])
        receiver._process_buffer()
        self.assertEqual(received, [('noise', False)])
        receiver._buffer.extend(frame[7:])
        receiver._process_buffer()
        self.assertEqual(received, [('noise', False), (frame, True)])

    def test_command_flow_ids_do_not_enter_gateway_poll_range(self):
        manager = SerialManager()
        manager._flow_id = 0x7FFF
        self.assertEqual(manager.next_flow_id(), 1)
        self.assertLess(manager.next_flow_id(), 0x8000)

    def test_gateway_local_error_is_parsed(self):
        frame = LoRaProtocol.build_packet(LoRaProtocol.MSG_ERROR,
                                          LoRaProtocol.ROLE_CONTROL, 0,
                                          LoRaProtocol.ROLE_HOST, 2,
                                          101, bytes((0x02,)))
        parsed = LoRaProtocol.parse_packet(frame)
        self.assertTrue(LoRaProtocol.validate_control_room_response(parsed))
        self.assertEqual(parsed['data'], bytes((0x02,)))


if __name__ == '__main__':
    unittest.main()
