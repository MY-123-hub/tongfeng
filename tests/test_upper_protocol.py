"""不启动 Tk、不打开真实串口的上位机协议测试。"""

from pathlib import Path
import sys
import unittest


UPPER_DIR = Path(__file__).resolve().parents[1] / '上位机代码'
sys.path.insert(0, str(UPPER_DIR))

from upper_protocol import LoRaProtocol
from main import SerialManager


class UpperProtocolTests(unittest.TestCase):
    def test_set_frequency_50hz_matches_master_protocol(self):
        frame = LoRaProtocol.cmd_set_freq(1, 100, 50.00)
        self.assertEqual(
            frame.hex(' ').upper(),
            'AA 55 01 10 01 00 02 01 64 00 02 88 13 DB C9',
        )

    def test_target_temperature_is_signed_x10(self):
        self.assertEqual(
            LoRaProtocol.cmd_set_target_temp(1, 100, 26.0)[11:13], bytes((0x04, 0x01)))
        self.assertEqual(
            LoRaProtocol.cmd_set_target_temp(1, 100, -5.5)[11:13], bytes((0xC9, 0xFF)))

    def test_temp_36_is_85_bytes_and_zero_is_invalid(self):
        raw_values = [0, -55, 250] + [0] * 33
        payload = b''.join(value.to_bytes(2, 'little', signed=True) for value in raw_values)
        frame = LoRaProtocol.build_packet(LoRaProtocol.MSG_TEMP_36,
                                          LoRaProtocol.ROLE_HOST, 1,
                                          LoRaProtocol.ROLE_CONTROL, 0, 0x8000, payload)
        self.assertEqual(len(frame), 85)
        parsed = LoRaProtocol.parse_packet(frame)
        self.assertTrue(LoRaProtocol.validate_control_room_response(parsed))
        self.assertEqual(LoRaProtocol.decode_temperatures(parsed['data'])[:3], [None, -5.5, 25.0])

    def test_ack_result_and_error_are_decoded(self):
        ack = LoRaProtocol.decode_ack(bytes((1, 0)))
        self.assertEqual(ack['status_name'], '重复命令，未重复执行')

        result = LoRaProtocol.decode_result(
            bytes((0, 2, 0)) + (5000).to_bytes(2, 'little') + (-55).to_bytes(2, 'little', signed=True))
        self.assertEqual(result['result_name'], '成功')
        self.assertEqual(result['mode_name'], '手动停机')
        self.assertEqual(result['frequency_hz'], 50.0)
        self.assertEqual(result['target_temperature_c'], -5.5)

        error = LoRaProtocol.decode_error(bytes((4,)))
        self.assertEqual(error['name'], '从机响应超时')

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
        receiver = SerialManager(callback=received.append)
        frame = LoRaProtocol.cmd_read_temp(1, 100)

        receiver._buffer.extend(b'noise\xAA')
        receiver._process_buffer()
        self.assertEqual(received, [])
        self.assertEqual(bytes(receiver._buffer), b'\xAA')

        receiver._buffer.extend(frame[1:7])
        receiver._process_buffer()
        self.assertEqual(received, [])
        receiver._buffer.extend(frame[7:])
        receiver._process_buffer()
        self.assertEqual(received, [frame])


if __name__ == '__main__':
    unittest.main()
