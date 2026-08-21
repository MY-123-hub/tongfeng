"""控制室 USART1 与上位机共用的正式二进制协议。"""

from __future__ import annotations


class LoRaProtocol:
    FRAME_HEAD1 = 0xAA
    FRAME_HEAD2 = 0x55
    VERSION = 0x01
    MAX_DATA_LEN = 96
    TEMP_36_DATA_LEN = 72

    ROLE_CONTROL = 0x01
    ROLE_HOST = 0x02
    GROUP_CONTROL = 0x00

    MSG_READ_TEMP = 0x01
    MSG_TEMP_36 = 0x02
    MSG_SET_FREQ = 0x10
    MSG_SET_TARGET_TEMP = 0x11
    MSG_MANUAL_RUN = 0x12
    MSG_MANUAL_STOP = 0x13
    MSG_SET_AUTO = 0x14
    MSG_QUERY_STATUS = 0x15
    MSG_ACK = 0x20
    MSG_RESULT = 0x21
    MSG_ERROR = 0x7E

    MSG_NAMES = {
        MSG_READ_TEMP: 'READ_TEMP', MSG_TEMP_36: 'TEMP_36',
        MSG_SET_FREQ: 'SET_FREQ', MSG_SET_TARGET_TEMP: 'SET_TARGET_TEMP',
        MSG_MANUAL_RUN: 'MANUAL_RUN', MSG_MANUAL_STOP: 'MANUAL_STOP',
        MSG_SET_AUTO: 'SET_AUTO', MSG_QUERY_STATUS: 'QUERY_STATUS',
        MSG_ACK: 'ACK', MSG_RESULT: 'RESULT', MSG_ERROR: 'ERROR',
    }
    ROLE_NAMES = {ROLE_CONTROL: '控制室', ROLE_HOST: '主机'}

    ACK_STATUS = {0: '已接受并开始处理', 1: '重复命令，未重复执行', 2: '已拒绝'}
    ERROR_NAMES = {
        0x01: '参数或数据长度错误', 0x02: '当前状态不允许', 0x03: '主机忙或队列已满',
        0x04: '从机响应超时', 0x05: 'TD710 响应超时', 0x06: 'TD710 异常或校验失败',
        0x07: 'Flash 保存失败', 0x08: '业务流水号冲突', 0x09: '不支持的报文类型或版本',
        0x0A: '控制室等待主机响应超时',
    }
    MODE_NAMES = {0: '自动', 1: '手动运行', 2: '手动停机'}
    FAN_STATE_NAMES = {0: '已确认停止命令', 1: '已确认运行命令', 2: '未知'}

    @staticmethod
    def crc16_modbus(data: bytes) -> int:
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                crc = ((crc >> 1) ^ 0xA001) if (crc & 1) else (crc >> 1)
        return crc & 0xFFFF

    @classmethod
    def build_packet(cls, msg_type: int, sender_role: int, sender_group: int,
                     recv_role: int, recv_group: int, flow_id: int,
                     data: bytes = b'') -> bytes:
        if len(data) > cls.MAX_DATA_LEN:
            raise ValueError('payload too long')
        header = bytes((cls.VERSION, msg_type, sender_role, sender_group,
                        recv_role, recv_group, flow_id & 0xFF,
                        (flow_id >> 8) & 0xFF, len(data))) + data
        crc = cls.crc16_modbus(header)
        return bytes((cls.FRAME_HEAD1, cls.FRAME_HEAD2)) + header + bytes((crc & 0xFF, crc >> 8))

    @classmethod
    def parse_packet(cls, frame: bytes) -> dict | None:
        if len(frame) < 13 or frame[0:2] != bytes((cls.FRAME_HEAD1, cls.FRAME_HEAD2)):
            return None
        if frame[2] != cls.VERSION:
            return None
        payload_length = frame[10]
        total_length = 13 + payload_length
        if payload_length > cls.MAX_DATA_LEN or len(frame) != total_length:
            return None
        expected_crc = cls.crc16_modbus(frame[2:11 + payload_length])
        received_crc = frame[-2] | (frame[-1] << 8)
        if received_crc != expected_crc:
            return None
        return {
            'msg_type': frame[3], 'sender_role': frame[4], 'sender_group': frame[5],
            'recv_role': frame[6], 'recv_group': frame[7],
            'flow_id': frame[8] | (frame[9] << 8), 'data': bytes(frame[11:-2]),
        }

    @classmethod
    def msg_name(cls, msg_type: int) -> str:
        return cls.MSG_NAMES.get(msg_type, f'0x{msg_type:02X}')

    @staticmethod
    def _check_host_group(host_group: int) -> None:
        if not 1 <= host_group <= 4:
            raise ValueError('host group must be 1..4')

    @classmethod
    def _host_command(cls, msg_type: int, host_group: int, flow_id: int,
                      data: bytes = b'') -> bytes:
        cls._check_host_group(host_group)
        return cls.build_packet(msg_type, cls.ROLE_CONTROL, cls.GROUP_CONTROL,
                                cls.ROLE_HOST, host_group, flow_id, data)

    @classmethod
    def cmd_read_temp(cls, host_group: int, flow_id: int, force_resample: bool = False) -> bytes:
        return cls._host_command(cls.MSG_READ_TEMP, host_group, flow_id,
                                 bytes((1 if force_resample else 0,)))

    @classmethod
    def cmd_set_freq(cls, host_group: int, flow_id: int, frequency_hz: float) -> bytes:
        raw = int(round(frequency_hz * 100))
        if not 0 <= raw <= 5000:
            raise ValueError('frequency must be 0.00..50.00 Hz')
        return cls._host_command(cls.MSG_SET_FREQ, host_group, flow_id,
                                 raw.to_bytes(2, 'little'))

    @classmethod
    def cmd_set_target_temp(cls, host_group: int, flow_id: int, temperature_c: float) -> bytes:
        raw = int(round(temperature_c * 10))
        if not -550 <= raw <= 1250:
            raise ValueError('target temperature must be -55.0..125.0 C')
        return cls._host_command(cls.MSG_SET_TARGET_TEMP, host_group, flow_id,
                                 raw.to_bytes(2, 'little', signed=True))

    @classmethod
    def cmd_manual_run(cls, host_group: int, flow_id: int) -> bytes:
        return cls._host_command(cls.MSG_MANUAL_RUN, host_group, flow_id)

    @classmethod
    def cmd_manual_stop(cls, host_group: int, flow_id: int) -> bytes:
        return cls._host_command(cls.MSG_MANUAL_STOP, host_group, flow_id)

    @classmethod
    def cmd_set_auto(cls, host_group: int, flow_id: int) -> bytes:
        return cls._host_command(cls.MSG_SET_AUTO, host_group, flow_id)

    @classmethod
    def cmd_query_status(cls, host_group: int, flow_id: int) -> bytes:
        return cls._host_command(cls.MSG_QUERY_STATUS, host_group, flow_id)

    @classmethod
    def validate_control_room_response(cls, parsed: dict | None) -> bool:
        if not parsed:
            return False
        msg_type = parsed['msg_type']
        source_role, source_group = parsed['sender_role'], parsed['sender_group']
        destination_role, destination_group = parsed['recv_role'], parsed['recv_group']
        payload_length = len(parsed['data'])

        if msg_type in (cls.MSG_TEMP_36, cls.MSG_ACK, cls.MSG_RESULT):
            if not (source_role == cls.ROLE_HOST and 1 <= source_group <= 4 and
                    destination_role == cls.ROLE_CONTROL and destination_group == 0):
                return False
            expected = {cls.MSG_TEMP_36: 72, cls.MSG_ACK: 2, cls.MSG_RESULT: 7}[msg_type]
            return payload_length == expected
        if msg_type == cls.MSG_ERROR:
            from_master = (source_role == cls.ROLE_HOST and 1 <= source_group <= 4 and
                           destination_role == cls.ROLE_CONTROL and destination_group == 0)
            from_gateway = (source_role == cls.ROLE_CONTROL and source_group == 0 and
                            destination_role == cls.ROLE_HOST and 1 <= destination_group <= 4)
            return (from_master or from_gateway) and 1 <= payload_length <= 16
        return False

    @staticmethod
    def _read_i16(data: bytes, offset: int) -> int:
        return int.from_bytes(data[offset:offset + 2], 'little', signed=True)

    @classmethod
    def decode_temperatures(cls, data: bytes) -> list[float | None]:
        if len(data) != cls.TEMP_36_DATA_LEN:
            raise ValueError('TEMP_36 payload length must be 72')
        return [None if (raw := cls._read_i16(data, offset)) == 0 else round(raw / 10.0, 1)
                for offset in range(0, cls.TEMP_36_DATA_LEN, 2)]

    @classmethod
    def decode_ack(cls, data: bytes) -> dict:
        if len(data) != 2:
            raise ValueError('ACK payload length must be 2')
        return {'status': data[0], 'status_name': cls.ACK_STATUS.get(data[0], '未知'),
                'reason': data[1]}

    @classmethod
    def decode_result(cls, data: bytes) -> dict:
        if len(data) != 7:
            raise ValueError('RESULT payload length must be 7')
        return {
            'result_code': data[0],
            'result_name': '成功' if data[0] == 0 else cls.ERROR_NAMES.get(data[0], '未知错误'),
            'mode': data[1], 'mode_name': cls.MODE_NAMES.get(data[1], '未知'),
            'fan_state': data[2], 'fan_state_name': cls.FAN_STATE_NAMES.get(data[2], '未知'),
            'frequency_hz': int.from_bytes(data[3:5], 'little') / 100.0,
            'target_temperature_c': cls._read_i16(data, 5) / 10.0,
        }

    @classmethod
    def decode_error(cls, data: bytes) -> dict:
        if not 1 <= len(data) <= 16:
            raise ValueError('ERROR payload length must be 1..16')
        return {'code': data[0], 'name': cls.ERROR_NAMES.get(data[0], '未知错误'), 'detail': data[1:]}
