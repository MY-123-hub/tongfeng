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
            'AA 55 01 10 01 00 02 01 64 00 02 E8 03 F2 05',
        )

    def test_frequency_range_matches_master(self):
        with self.assertRaises(ValueError):
            ROOT_UPPER.LoRaProtocol.cmd_set_freq(1, 100, 50.01)


if __name__ == '__main__':
    unittest.main()
