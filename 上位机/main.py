#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""智能通风系统上位机 - LoRa 二进制协议 + 4从机×36路温度 + 4主机变频器控制

通信链路 (参考最新固件 E:\\智能通风系统\\代码\\ControlRoomLoRa):
  从机(采集) → 主机(转发) → 控制室板(LoRa网关) → USART1/USB → 本上位机
控制室固件行为:
  - 纯二进制透传: USART2(LoRa) 收帧 → up_queue → USART1(PC) 原样转发
  - USART1 RX 已启用: 上位机发帧经 route_frame 路由:
      dst_role=ROLE_HOST(02) → down_queue → USART2 转发到 LoRa 给主机
      dst_role=ROLE_CONTROL(01) → 本地处理 (0x30 主机列表配置)
  - 控制室按 M1温度、M1环境 ... M4环境 的顺序自动轮询
  - 无效温度 = 0x8000 (int16 -32768), 温度为有符号 0.1℃ (可为负), 低字节在前
  - CRC-16/MODBUS: poly 0xA001, init 0xFFFF, 从版本字节(buf[2])起算, 跳过 AA55
  - 角色: 01=控制室, 02=主机, 03=从机, 04=上位机(仅上位机→控制室私有配置)
命令下发:
  给主机的命令以控制室身份(01/00)发送, 控制室原样透传到 LoRa
  0x30 主机列表配置以上位机身份(04/00)发送, 控制室本地处理
4个从机切换按钮选择当前显示哪个从机的温度数据。白色主题, SQLite 实时存储。
"""


import os, csv, json, sqlite3
from datetime import datetime, timedelta

from collections import deque

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None

import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import threading, time

try:
    import matplotlib
    matplotlib.use('TkAgg')
    # Tk 会自动使用 Windows 中文字体，但 Matplotlib 默认的 DejaVu Sans 不含中文，
    # 图表标题、坐标轴文字会显示为方框。按当前 Windows 常见字体顺序显式回退。
    matplotlib.rcParams['font.sans-serif'] = [
        'Microsoft YaHei', 'SimHei', 'Noto Sans SC', 'DejaVu Sans'
    ]
    matplotlib.rcParams['axes.unicode_minus'] = False
    import matplotlib.dates as mdates
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure
    from matplotlib.patches import Patch
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False

NUM_PORTS = 6
NUM_SENSORS = 6
NUM_NODES = NUM_PORTS * NUM_SENSORS   # 36
NUM_SLAVES = 4
NUM_HOSTS = 4


# ============================================================
#  LoRa 报文协议 (二进制)
#  帧: AA 55 02 [类型] [发送角色 发送组号] [接收角色 接收组号]
#       [流水号低 流水号高] [数据长度] [数据区] [CRC16低 CRC16高]
#  CRC: CRC-16/MODBUS (多项式 0xA001, 初值 0xFFFF), 低字节在前
#  上位机经 USB 串口连接控制室板, 以"控制室"身份 (01/00) 收发
# ============================================================
class LoRaProtocol:
    FRAME_HEAD1 = 0xAA
    FRAME_HEAD2 = 0x55
    VERSION = 0x02

    # 角色 (与固件 frame.h 对齐)
    ROLE_CONTROL = 0x01     # 控制室
    ROLE_HOST = 0x02        # 主机
    ROLE_SLAVE = 0x03       # 从机
    ROLE_PC = 0x04          # 上位机 (PC, 仅用于上位机→控制室私有配置)

    # 组号
    GROUP_CONTROL = 0x00

    # 报文类型 (与固件 frame.h 对齐)
    MSG_READ_TEMP = 0x01        # 控制室→主机→从机 请求 36 点温度
    MSG_TEMP_36 = 0x02          # 返回 36 点温度 + 主从机 BME 温度
    MSG_READ_ENV = 0x03         # 请求湿度、绝对气压和雨滴预留值
    MSG_ENV_DATA = 0x04         # 返回 36 点湿度 + 主从机 BME 环境数据
    MSG_SET_FREQ = 0x10         # 控制室→主机 设置变频器频率
    MSG_SET_TARGET_TEMP = 0x11  # 控制室→主机 设置目标温度
    MSG_MANUAL_RUN = 0x12       # 控制室→主机 手动启动风机
    MSG_MANUAL_STOP = 0x13      # 控制室→主机 手动停止并锁定
    MSG_SET_AUTO = 0x14         # 控制室→主机 解除手动, 恢复自动
    MSG_QUERY_STATUS = 0x15     # 控制室→主机 查询状态
    MSG_ACK = 0x20              # 主机→控制室 命令已收到
    MSG_RESULT = 0x21           # 主机→控制室 命令执行结果
    MSG_ERROR = 0x7E            # 任意方向 处理失败/超时/组号错误
    MSG_HOST_LIST = 0x30        # 上位机→控制室 配置主机列表 (私有)

    MSG_NAMES = {
        0x01: 'READ_TEMP', 0x02: 'TEMP_36', 0x03: 'READ_ENV',
        0x04: 'ENV_DATA', 0x10: 'SET_FREQ',
        0x11: 'SET_TARGET_TEMP', 0x12: 'MANUAL_RUN', 0x13: 'MANUAL_STOP',
        0x14: 'SET_AUTO', 0x15: 'QUERY_STATUS', 0x20: 'ACK',
        0x21: 'RESULT', 0x7E: 'ERROR', 0x30: 'HOST_LIST',
    }
    ROLE_NAMES = {0x01: '控制室', 0x02: '主机', 0x03: '从机', 0x04: '上位机'}

    MAX_DATA_LEN = 96
    TEMP_DATA_LEN = 76
    ENV_DATA_LEN = 86
    INVALID_TEMPERATURE = 0x8000
    INVALID_HUMIDITY = 0xFFFF
    INVALID_PRESSURE = 0xFFFFFFFF
    RAIN_UNAVAILABLE = 0xFFFF
    RESULT_DATA_LEN = 7

    RESULT_NAMES = {
        0x00: '成功', 0x01: '参数无效', 0x02: '状态不允许',
        0x03: '设备忙', 0x04: '从机超时', 0x05: '变频器超时',
        0x06: '变频器响应异常', 0x07: '参数保存失败',
        0x08: '流水号冲突', 0x09: '不支持的命令',
    }
    CONTROL_MODE_NAMES = {0: '自动', 1: '手动运行', 2: '手动停止'}
    FAN_STATE_NAMES = {0: '已停止', 1: '运行中', 2: '状态未知'}

    @classmethod
    def _valid_payload(cls, msg_type, payload):
        lengths = {
            cls.MSG_READ_TEMP: 1,
            cls.MSG_TEMP_36: cls.TEMP_DATA_LEN,
            cls.MSG_READ_ENV: 1,
            cls.MSG_ENV_DATA: cls.ENV_DATA_LEN,
            cls.MSG_SET_FREQ: 2,
            cls.MSG_SET_TARGET_TEMP: 2,
            cls.MSG_MANUAL_RUN: 0,
            cls.MSG_MANUAL_STOP: 0,
            cls.MSG_SET_AUTO: 0,
            cls.MSG_QUERY_STATUS: 0,
            cls.MSG_ACK: 2,
            cls.MSG_RESULT: 7,
        }
        if msg_type == cls.MSG_ERROR:
            return 1 <= len(payload) <= 16
        if msg_type == cls.MSG_HOST_LIST:
            return 1 <= len(payload) <= NUM_HOSTS
        if msg_type not in lengths or len(payload) != lengths[msg_type]:
            return False
        if msg_type in (cls.MSG_READ_TEMP, cls.MSG_READ_ENV):
            return payload[0] <= 1
        return True

    @staticmethod
    def crc16_modbus(data: bytes) -> int:
        """CRC-16/MODBUS: 多项式 0xA001, 初值 0xFFFF"""
        crc = 0xFFFF
        for b in data:
            crc ^= b
            for _ in range(8):
                if crc & 0x0001:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc & 0xFFFF

    @classmethod
    def build_packet(cls, msg_type, sender_role, sender_group,
                     recv_role, recv_group, flow_id, data=b''):
        """组装完整报文 (含 CRC).
        CRC 范围: 从版本字节(buf[2])到数据区末尾, 跳过 AA55 帧头, 与固件 frame.c 一致.
        """
        if len(data) > cls.MAX_DATA_LEN:
            raise ValueError(f'数据区超长: {len(data)} > {cls.MAX_DATA_LEN}')
        header_no_crc = bytes([
            cls.VERSION, msg_type,
            sender_role, sender_group, recv_role, recv_group,
            flow_id & 0xFF, (flow_id >> 8) & 0xFF,
            len(data),
        ]) + data
        crc = cls.crc16_modbus(header_no_crc)
        frame = bytes([cls.FRAME_HEAD1, cls.FRAME_HEAD2]) + header_no_crc
        return frame + bytes([crc & 0xFF, (crc >> 8) & 0xFF])

    @classmethod
    def parse_packet(cls, data: bytes, strict=True):
        """解析一帧.
        CRC 范围: 从版本字节(buf[2])到数据区末尾, 跳过 AA55 帧头, 与固件 frame.c 一致.
        strict=True (默认): CRC 错误返回 None
        strict=False: 返回 dict 含 'crc_ok' 字段, CRC 不一致时仍可处理
        """
        if len(data) < 13:
            return None
        if data[0] != cls.FRAME_HEAD1 or data[1] != cls.FRAME_HEAD2:
            return None
        if data[2] != cls.VERSION:
            return None
        data_len = data[10]
        total = 11 + data_len + 2
        if len(data) != total:
            return None
        recv_crc = data[11 + data_len] | (data[12 + data_len] << 8)
        # CRC 从版本字节(buf[2])起算, 不含 AA55 帧头和末尾 CRC
        calc_crc = cls.crc16_modbus(data[2:11 + data_len])
        crc_ok = (recv_crc == calc_crc)
        if strict and not crc_ok:
            return None
        payload = bytes(data[11:11 + data_len])
        if not cls._valid_payload(data[3], payload):
            return None
        return {
            'msg_type': data[3],
            'sender_role': data[4],
            'sender_group': data[5],
            'recv_role': data[6],
            'recv_group': data[7],
            'flow_id': data[8] | (data[9] << 8),
            'data': payload,
            'crc_ok': crc_ok,
            'recv_crc': recv_crc,
            'calc_crc': calc_crc,
        }

    @classmethod
    def msg_name(cls, t):
        return cls.MSG_NAMES.get(t, f'0x{t:02X}')

    @classmethod
    def validate_control_room_response(cls, parsed):
        if not parsed:
            return False
        host_types = {cls.MSG_TEMP_36, cls.MSG_ENV_DATA, cls.MSG_ACK,
                      cls.MSG_RESULT, cls.MSG_ERROR}
        if (parsed['msg_type'] in host_types and
                parsed['sender_role'] == cls.ROLE_HOST and
                1 <= parsed['sender_group'] <= NUM_HOSTS and
                parsed['recv_role'] == cls.ROLE_CONTROL and
                parsed['recv_group'] == 0):
            return True
        # 控制室生成的本地超时/队列错误仍以原目标主机地址返回给PC。
        return (parsed['msg_type'] == cls.MSG_ERROR and
                parsed['sender_role'] == cls.ROLE_CONTROL and
                parsed['sender_group'] == 0 and
                parsed['recv_role'] == cls.ROLE_HOST and
                1 <= parsed['recv_group'] <= NUM_HOSTS)

    # ---------- 上位机(控制室 01/00) → 主机 命令构造 ----------
    @classmethod
    def cmd_read_temp(cls, host_group, flow_id, force_resample=False):
        # 数据区 1 字节: 0=允许主机使用缓存, 1=强制重新采样
        return cls.build_packet(cls.MSG_READ_TEMP, cls.ROLE_CONTROL, 0,
                                cls.ROLE_HOST, host_group, flow_id,
                                bytes([1 if force_resample else 0]))

    @classmethod
    def cmd_read_env(cls, host_group, flow_id, force_resample=False):
        return cls.build_packet(cls.MSG_READ_ENV, cls.ROLE_CONTROL, 0,
                                cls.ROLE_HOST, host_group, flow_id,
                                bytes([1 if force_resample else 0]))

    @staticmethod
    def _read_u16(data, offset):
        return data[offset] | (data[offset + 1] << 8)

    @staticmethod
    def _read_u32(data, offset):
        return int.from_bytes(data[offset:offset + 4], 'little')

    @classmethod
    def decode_temperature_data(cls, data):
        if len(data) != cls.TEMP_DATA_LEN:
            raise ValueError(f'温度帧数据区应为 {cls.TEMP_DATA_LEN} 字节')

        def decode(raw):
            if raw == cls.INVALID_TEMPERATURE:
                return None
            signed = raw - 0x10000 if raw >= 0x8000 else raw
            return round(signed / 10.0, 1)

        return {
            'temperatures': [decode(cls._read_u16(data, i * 2))
                             for i in range(NUM_NODES)],
            'slave_bme_temperature': decode(cls._read_u16(data, 72)),
            'master_bme_temperature': decode(cls._read_u16(data, 74)),
        }

    @classmethod
    def decode_environment_data(cls, data):
        if len(data) != cls.ENV_DATA_LEN:
            raise ValueError(f'环境帧数据区应为 {cls.ENV_DATA_LEN} 字节')

        def humidity(raw):
            return None if raw == cls.INVALID_HUMIDITY else round(raw / 10.0, 1)

        def pressure(raw):
            return None if raw == cls.INVALID_PRESSURE else raw

        rain_raw = cls._read_u16(data, 84)
        return {
            'humidities': [humidity(cls._read_u16(data, i * 2))
                           for i in range(NUM_NODES)],
            'slave_bme_humidity': humidity(cls._read_u16(data, 72)),
            'master_bme_humidity': humidity(cls._read_u16(data, 74)),
            'slave_bme_pressure_pa': pressure(cls._read_u32(data, 76)),
            'master_bme_pressure_pa': pressure(cls._read_u32(data, 80)),
            'rain': None if rain_raw == cls.RAIN_UNAVAILABLE else rain_raw,
        }

    @classmethod
    def decode_result_data(cls, data):
        """解析主机 RESULT 的7字节状态回执。"""
        if len(data) != cls.RESULT_DATA_LEN:
            raise ValueError(f'RESULT 数据区应为 {cls.RESULT_DATA_LEN} 字节')

        target_raw = cls._read_u16(data, 5)
        target_x10 = target_raw - 0x10000 if target_raw >= 0x8000 else target_raw
        result_code = data[0]
        control_mode = data[1]
        fan_state = data[2]
        return {
            'result_code': result_code,
            'result_name': cls.RESULT_NAMES.get(result_code, f'未知错误(0x{result_code:02X})'),
            'control_mode': control_mode,
            'control_mode_name': cls.CONTROL_MODE_NAMES.get(
                control_mode, f'未知模式(0x{control_mode:02X})'),
            'fan_state': fan_state,
            'fan_state_name': cls.FAN_STATE_NAMES.get(
                fan_state, f'未知风机状态(0x{fan_state:02X})'),
            'frequency_hz': cls._read_u16(data, 3) / 100.0,
            'target_temperature_c': target_x10 / 10.0,
        }

    @classmethod
    def cmd_set_freq(cls, host_group, flow_id, freq_hz):
        # 频率: 2 字节 LE, 单位 0.01 Hz (10.00 Hz -> 1000 -> E8 03)
        # 必须与主机 master_runtime.c 的 SET_FREQ 解析保持一致。
        v = int(round(freq_hz * 100))
        if not 0 <= v <= 5000:
            raise ValueError('frequency must be 0.00..50.00 Hz')
        return cls.build_packet(cls.MSG_SET_FREQ, cls.ROLE_CONTROL, 0,
                                cls.ROLE_HOST, host_group, flow_id,
                                v.to_bytes(2, 'little'))

    @classmethod
    def cmd_set_target_temp(cls, host_group, flow_id, temp_c):
        # 温度: 2 字节 LE, 单位 0.1 ℃ (26.0 ℃ -> 260)
        v = int(round(temp_c * 10))
        if not -550 <= v <= 1250:
            raise ValueError('target temperature must be -55.0..125.0 ℃')
        return cls.build_packet(cls.MSG_SET_TARGET_TEMP, cls.ROLE_CONTROL, 0,
                                cls.ROLE_HOST, host_group, flow_id,
                                v.to_bytes(2, 'little', signed=True))

    @classmethod
    def cmd_manual_run(cls, host_group, flow_id):
        return cls.build_packet(cls.MSG_MANUAL_RUN, cls.ROLE_CONTROL, 0,
                                cls.ROLE_HOST, host_group, flow_id)

    @classmethod
    def cmd_manual_stop(cls, host_group, flow_id):
        return cls.build_packet(cls.MSG_MANUAL_STOP, cls.ROLE_CONTROL, 0,
                                cls.ROLE_HOST, host_group, flow_id)

    @classmethod
    def cmd_set_auto(cls, host_group, flow_id):
        return cls.build_packet(cls.MSG_SET_AUTO, cls.ROLE_CONTROL, 0,
                                cls.ROLE_HOST, host_group, flow_id)

    @classmethod
    def cmd_query_status(cls, host_group, flow_id):
        return cls.build_packet(cls.MSG_QUERY_STATUS, cls.ROLE_CONTROL, 0,
                                cls.ROLE_HOST, host_group, flow_id)

    # ---------- 上位机(PC 04/00) → 控制室(01/00) 私有配置 ----------
    @classmethod
    def cmd_host_list(cls, host_groups, flow_id):
        """配置控制室轮询的主机列表.
        host_groups: 主机组号序列, 如 [1,2,3,4]
        固件 handle_host_list: 数据区 = 主机组号序列, cur_host 重置为 0
        """
        data = bytes(host_groups)
        return cls.build_packet(cls.MSG_HOST_LIST, cls.ROLE_PC, 0,
                                cls.ROLE_CONTROL, 0, flow_id, data)


# ============================================================
#  数据库管理
# ============================================================
class DatabaseManager:
    CONFIG_FILE = os.path.join(os.environ.get('APPDATA', os.path.expanduser('~')),
                               '.ventilation_config.json')

    def __init__(self):
        self.db_path = None
        self.conn = None
        self.data_dir = None

    def init_storage(self, parent_window):
        saved = self._load_config()
        if saved and os.path.isdir(saved):
            self.data_dir = saved
        else:
            default_dir = 'D:\\'
            if not os.path.exists(default_dir):
                default_dir = os.path.expanduser('~')
            messagebox.showinfo(
                '数据存储路径',
                '首次运行，请选择数据存储路径。\n'
                '系统将在此路径下创建数据库文件，\n'
                '所有采集数据将实时保存到此位置。',
                parent=parent_window)
            chosen = filedialog.askdirectory(
                title='选择数据存储路径', initialdir=default_dir)
            if not chosen:
                chosen = os.path.join(default_dir, '智能通风系统数据')
            self.data_dir = os.path.join(chosen, '智能通风系统数据')
            os.makedirs(self.data_dir, exist_ok=True)
            self._save_config(self.data_dir)

        self.db_path = os.path.join(self.data_dir, 'ventilation_data.db')
        self.conn = sqlite3.connect(self.db_path, check_same_thread=False)
        self._create_table()

    def _create_table(self):
        self.conn.execute('''
            CREATE TABLE IF NOT EXISTS sensor_data (
                id           INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp    TEXT,
                full_date    TEXT,
                slave        INTEGER,
                temperatures TEXT,
                humidities   TEXT,
                pressure     REAL,
                rain         INTEGER,
                port         TEXT,
                num          INTEGER
            )
        ''')
        self.conn.execute('''
            CREATE TABLE IF NOT EXISTS command_log (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp   TEXT,
                direction   TEXT,
                content     TEXT
            )
        ''')
        self.conn.commit()

    def insert_sensor_data(self, data):
        now = datetime.now()
        self.conn.execute('''
            INSERT INTO sensor_data
                (timestamp, full_date, slave, temperatures, humidities, pressure, rain, port, num)
            VALUES (?,?,?,?,?,?,?,?,?)
        ''', (
            now.strftime('%H:%M:%S'),
            now.strftime('%Y-%m-%d %H:%M:%S'),
            data.get('slave', 1),
            json.dumps(data.get('temperatures', [])),
            json.dumps(data.get('humidities', [])),
            data.get('pressure'),
            data.get('rain'),
            data.get('port'),
            data.get('num'),
        ))
        self.conn.commit()

    def insert_command_log(self, direction, content):
        self.conn.execute(
            'INSERT INTO command_log (timestamp, direction, content) VALUES (?,?,?)',
            (datetime.now().strftime('%H:%M:%S'), direction, content))
        self.conn.commit()

    def load_recent(self, slave, limit=500):
        rows = self.conn.execute(
            'SELECT timestamp, full_date, temperatures, humidities, pressure, rain, port, num '
            'FROM sensor_data WHERE slave=? ORDER BY id DESC LIMIT ?', (slave, limit)
        ).fetchall()
        rows.reverse()
        result = []
        for r in rows:
            try:
                temps = json.loads(r[2]) if r[2] else []
            except Exception:
                temps = []
            try:
                humis = json.loads(r[3]) if r[3] else []
            except Exception:
                humis = []
            result.append({
                'timestamp': r[0], 'full_date': r[1],
                'temperatures': temps, 'humidities': humis,
                'pressure': r[4], 'rain': r[5], 'port': r[6], 'num': r[7]})
        return result

    def close(self):
        if self.conn:
            self.conn.close()

    def _load_config(self):
        try:
            with open(self.CONFIG_FILE, 'r', encoding='utf-8') as f:
                return json.load(f).get('data_dir')
        except Exception:
            return None

    def _save_config(self, path):
        try:
            with open(self.CONFIG_FILE, 'w', encoding='utf-8') as f:
                json.dump({'data_dir': path}, f)
        except Exception:
            pass


# ============================================================
#  白色主题
# ============================================================
class Theme:
    BG       = '#f0f0f0'
    BG_CARD  = '#ffffff'
    BG_INPUT = '#ececec'
    BG_TAB   = '#e0e0e0'
    BG_TAB_SEL = '#ffffff'
    BORDER   = '#d0d0d0'
    TEXT     = '#333333'
    TEXT_DIM = '#9e9e9e'
    TEXT_HI  = '#000000'
    ACCENT   = '#1976d2'
    GREEN    = '#2e7d32'
    GREEN_DK = '#43a047'
    RED      = '#c62828'
    RED_DK   = '#e53935'
    ORANGE   = '#fb8c00'
    YELLOW   = '#fdd835'
    PURPLE   = '#8e24aa'
    CYAN     = '#00838f'
    CHART_BG = '#ffffff'
    CHART_GD = '#e0e0e0'
    PORT_COLORS = ['#1976d2', '#fb8c00', '#e53935', '#43a047', '#00838f', '#8e24aa']
    SLAVE_COLORS = ['#1976d2', '#43a047', '#fb8c00', '#8e24aa']


# ============================================================
#  数据解析
# ============================================================
class DataParser:
    @staticmethod
    def parse(data):
        result = {'timestamp': datetime.now().strftime('%H:%M:%S'), 'slave': 1}
        try:
            data = data.strip()
            parts = data.split(',')
            for part in parts:
                part = part.strip()
                if part.startswith('SLAVE:'):
                    result['slave'] = int(part.split(':')[1])
                elif part.startswith('TM:'):
                    vals = part.split(':', 1)[1]
                    result['temperatures'] = [float(x) for x in vals.split(',') if x.strip()]
                elif part.startswith('HM:'):
                    vals = part.split(':', 1)[1]
                    result['humidities'] = [float(x) for x in vals.split(',') if x.strip()]
                elif part.startswith('Pressure:'):
                    result['pressure'] = float(part.split(':')[1])
                elif part.startswith('RAIN:'):
                    result['rain'] = int(part.split(':')[1])
                elif part.startswith('NUM:'):
                    result['num'] = int(part.split(':')[1])
                elif part.startswith('PORT:'):
                    result['port'] = part.split(':')[1]
        except Exception:
            pass
        return result


# ============================================================
#  串口管理
#  支持二进制 LoRa 帧 (AA 55 ...) 与 ASCII 文本回退
# ============================================================
class SerialManager:
    def __init__(self, callback=None):
        self.serial_port = None
        self.is_connected = False
        self.callback = callback
        self._running = False
        self._thread = None
        self._buffer = bytearray()
        self._flow_id = 100   # 业务流水号从 100 开始递增

    def list_ports(self):
        ports = []
        try:
            for p in serial.tools.list_ports.comports():
                ports.append(f"{p.device} - {p.description}")
            return sorted(ports, key=lambda x: x.split(' - ')[0])
        except Exception:
            return []

    def connect(self, port_name, callback):
        port = port_name.split(' - ')[0].strip()
        self.serial_port = serial.Serial(port, 115200, timeout=1)
        self.is_connected = True
        self.callback = callback
        self._running = True
        self._buffer.clear()
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    def disconnect(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=2)
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
        self.is_connected = False

    def send_bytes(self, data: bytes):
        """发送二进制 LoRa 报文"""
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.write(data)
            return True
        return False

    def send_command(self, cmd):
        """遗留: 发送 ASCII 文本命令"""
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.write((cmd + '\r\n').encode('utf-8'))

    def next_flow_id(self):
        # 0x8000..0xFFFF 保留给控制室自动轮询，人工命令只用低半区。
        self._flow_id = (self._flow_id + 1) & 0x7FFF
        if self._flow_id == 0:
            self._flow_id = 1
        return self._flow_id

    def _read_loop(self):
        while self._running:
            try:
                if self.serial_port and self.serial_port.in_waiting:
                    chunk = self.serial_port.read(self.serial_port.in_waiting)
                    if chunk:
                        self._buffer.extend(chunk)
                        self._process_buffer()
                else:
                    time.sleep(0.05)
            except Exception:
                pass

    def _process_buffer(self):
        """从缓冲区提取完整 LoRa 帧; 帧外字节当 ASCII 文本回退"""
        while self._buffer:
            idx = -1
            for i in range(len(self._buffer) - 1):
                if self._buffer[i] == 0xAA and self._buffer[i + 1] == 0x55:
                    idx = i
                    break
            if idx < 0:
                # 末尾单独的 AA 可能是下一批二进制帧头，必须保留。
                if self._buffer[-1] == 0xAA:
                    self._emit_ascii(bytes(self._buffer[:-1]))
                    self._buffer[:] = b'\xAA'
                else:
                    self._emit_ascii(bytes(self._buffer))
                    self._buffer.clear()
                return
            if idx > 0:
                # AA 55 前的字节当 ASCII
                self._emit_ascii(bytes(self._buffer[:idx]))
                del self._buffer[:idx]
            # 此时缓冲区以 AA 55 开头
            if len(self._buffer) < 11:
                return  # 头部不完整, 等待
            data_len = self._buffer[10]
            total = 11 + data_len + 2
            if len(self._buffer) < total:
                return  # 整帧不完整, 等待
            frame = bytes(self._buffer[:total])
            del self._buffer[:total]
            if self.callback:
                self.callback(frame, True)

    def _emit_ascii(self, buf: bytes):
        if not self.callback or not buf:
            return
        text = buf.decode('utf-8', 'ignore')
        for line in text.split('\n'):
            line = line.strip()
            if line:
                self.callback(line, False)


# ============================================================
#  主窗口
# ============================================================
class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('智能通风系统')
        self.geometry('1400x950')
        self.minsize(1200, 820)
        self.configure(bg=Theme.BG)

        self.db = DatabaseManager()
        self.db.init_storage(self)

        self.serial_mgr = SerialManager()
        self.parser = DataParser()

        # 4个从机各自独立的历史队列
        self.slave_data = {i: deque(maxlen=500) for i in range(1, NUM_SLAVES + 1)}
        self.latest_v2 = {
            i: {
                'temperatures': [None] * NUM_NODES,
                'humidities': [None] * NUM_NODES,
                'slave_bme_temperature': None,
                'master_bme_temperature': None,
                'slave_bme_humidity': None,
                'master_bme_humidity': None,
                'slave_bme_pressure_pa': None,
                'master_bme_pressure_pa': None,
                'rain': None,
            }
            for i in range(1, NUM_SLAVES + 1)
        }
        self.current_slave = 1
        self.log_lines = []
        # 图表视图状态：global 自动跟随完整历史，manual 保持用户缩放/拖移位置。
        self.chart_view_mode = 'global'
        self.chart_xlim = None
        self.chart_ylim = None
        self.chart_full_xlim = None
        self.chart_full_ylim = None
        self.chart_view_context = None
        self.chart_pan_start = None
        # JSON 温度批组装: key=(slave, flow_id) -> {temps[36], channels_received, ...}
        self.pending_temps = {}

        # 从数据库加载历史
        try:
            for s in range(1, NUM_SLAVES + 1):
                for r in self.db.load_recent(s, 500):
                    self.slave_data[s].append(r)
                if self.slave_data[s]:
                    last = self.slave_data[s][-1]
                    self.latest_v2[s]['temperatures'] = list(
                        last.get('temperatures', []))
                    self.latest_v2[s]['humidities'] = list(
                        last.get('humidities', []))
        except Exception:
            pass

        self._setup_styles()
        self._build_ui()

        self.protocol('WM_DELETE_WINDOW', self._on_close)
        self.after(2000, self._auto_refresh)

    def _setup_styles(self):
        style = ttk.Style(self)
        style.theme_use('clam')
        style.configure('TFrame', background=Theme.BG)
        style.configure('TLabel', background=Theme.BG, foreground=Theme.TEXT)
        style.configure('TNotebook', background=Theme.BG, borderwidth=0)
        style.configure('TNotebook.Tab',
                        background=Theme.BG_CARD, foreground=Theme.TEXT_DIM, padding=(20, 8))
        style.map('TNotebook.Tab',
                  background=[('selected', Theme.BG)],
                  foreground=[('selected', Theme.TEXT_HI)])
        style.configure('CardTitle.TLabel',
                        background=Theme.BG_CARD, foreground=Theme.TEXT_DIM, font=('', 11))
        style.configure('Dim.TLabel',
                        background=Theme.BG, foreground=Theme.TEXT_DIM, font=('', 11))
        style.configure('Status.TLabel',
                        background=Theme.BG, foreground=Theme.TEXT_DIM, font=('', 11))
        for name in ['Flat', 'SmallFlat', 'SmallBlue', 'SmallGreen', 'SmallRed',
                     'Conn', 'Disconn', 'SlaveTab', 'SlaveTabSel']:
            sz = 11 if name in ('Flat', 'Conn', 'Disconn') else 10
            pad = (0, 0) if name in ('Flat', 'Conn', 'Disconn') else (8, 4)
            if name == 'SlaveTab':
                style.configure(f'{name}.TButton', font=('', 12, 'bold'), padding=(15, 6))
            elif name == 'SlaveTabSel':
                style.configure(f'{name}.TButton', font=('', 12, 'bold'), padding=(15, 6))
            else:
                style.configure(f'{name}.TButton', font=('', sz), padding=pad)
        for name, bg, fg in [
            ('Flat', Theme.BG_INPUT, Theme.TEXT),
            ('SmallFlat', Theme.BG_INPUT, Theme.TEXT),
            ('SmallBlue', Theme.ACCENT, '#ffffff'),
            ('SmallGreen', Theme.GREEN_DK, '#ffffff'),
            ('SmallRed', Theme.RED_DK, '#ffffff'),
            ('Conn', Theme.GREEN_DK, '#ffffff'),
            ('Disconn', Theme.RED_DK, '#ffffff'),
            ('SlaveTab', Theme.BG_TAB, Theme.TEXT_DIM),
            ('SlaveTabSel', Theme.ACCENT, '#ffffff'),
        ]:
            style.configure(f'{name}.TButton', background=bg, foreground=fg, borderwidth=0)
            style.map(f'{name}.TButton',
                      background=[('active', bg), ('pressed', bg), ('disabled', Theme.BG_INPUT)])
        style.configure('TEntry',
                        fieldbackground=Theme.BG_INPUT, foreground=Theme.TEXT,
                        borderwidth=0, insertcolor=Theme.TEXT)
        style.configure('TCombobox',
                        fieldbackground=Theme.BG_INPUT, foreground=Theme.TEXT,
                        background=Theme.BG_CARD, arrowcolor=Theme.TEXT, borderwidth=0)
        style.configure('Section.TLabelframe',
                        background=Theme.BG_CARD, borderwidth=1, relief='flat')
        style.configure('Section.TLabelframe.Label',
                        background=Theme.BG_CARD, foreground=Theme.TEXT, font=('', 12, 'bold'))
        style.configure('Brand.TLabel',
                        background=Theme.BG_CARD, foreground=Theme.TEXT_HI, font=('', 20, 'bold'))
        style.configure('Sub.TLabel',
                        background=Theme.BG_CARD, foreground=Theme.TEXT_DIM, font=('', 10))

    def _build_ui(self):
        self._build_topbar()
        self._build_slave_selector()
        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=8, pady=(0, 8))
        self._build_monitor_tab()
        self._build_control_tab()

    def _build_topbar(self):
        bar = tk.Frame(self, bg=Theme.BG_CARD, height=60)
        bar.pack(fill=tk.X)
        bar.pack_propagate(False)
        ttk.Label(bar, text='智能通风系统', style='Brand.TLabel').pack(side=tk.LEFT, padx=20)
        ttk.Label(bar, text='Intelligent Ventilation Control', style='Sub.TLabel').pack(side=tk.LEFT, pady=(22, 0))
        ttk.Label(bar, text='  串口  ', style='Dim.TLabel').pack(side=tk.LEFT, padx=(30, 5))
        self.port_combo = ttk.Combobox(bar, width=25, state='readonly')
        self.port_combo.pack(side=tk.LEFT, pady=12)
        ttk.Button(bar, text='刷新', style='SmallFlat.TButton',
                   command=self._refresh_ports).pack(side=tk.LEFT, padx=4)
        self.connect_btn = ttk.Button(bar, text='  连接  ', style='Conn.TButton',
                                      command=self._toggle_connect)
        self.connect_btn.pack(side=tk.LEFT, padx=8)
        self.status_canvas = tk.Canvas(bar, width=12, height=12, bg=Theme.BG_CARD, highlightthickness=0)
        self.status_canvas.pack(side=tk.LEFT, padx=(15, 5))
        self._dot_id = self.status_canvas.create_oval(2, 2, 10, 10, fill=Theme.RED, outline='')
        self.status_label = ttk.Label(bar, text='未连接', style='Status.TLabel')
        self.status_label.pack(side=tk.LEFT)
        ttk.Button(bar, text='导出 CSV', style='SmallFlat.TButton',
                   command=self._export_csv).pack(side=tk.RIGHT, padx=8)
        ttk.Button(bar, text='清除数据', style='SmallFlat.TButton',
                   command=self._clear_data).pack(side=tk.RIGHT, padx=4)

    def _build_slave_selector(self):
        """4个从机切换按钮"""
        bar = tk.Frame(self, bg=Theme.BG, height=45)
        bar.pack(fill=tk.X)
        bar.pack_propagate(False)
        tk.Label(bar, text='选择从机: ', bg=Theme.BG, fg=Theme.TEXT_DIM,
                 font=('', 11)).pack(side=tk.LEFT, padx=(15, 8), pady=8)
        self.slave_btns = []
        for s in range(1, NUM_SLAVES + 1):
            st = 'SlaveTabSel.TButton' if s == self.current_slave else 'SlaveTab.TButton'
            btn = ttk.Button(bar, text=f'  从机{s}  ', style=st,
                             command=lambda s=s: self._switch_slave(s))
            btn.pack(side=tk.LEFT, padx=4, pady=6)
            self.slave_btns.append(btn)
        # 实时状态指示
        self.slave_status_labels = []
        for s in range(1, NUM_SLAVES + 1):
            dot = tk.Canvas(bar, width=8, height=8, bg=Theme.BG, highlightthickness=0)
            dot.pack(side=tk.LEFT, padx=(10, 2), pady=8)
            oid = dot.create_oval(0, 0, 8, 8, fill=Theme.TEXT_DIM, outline='')
            lbl = tk.Label(bar, text=f'从机{s}', bg=Theme.BG, fg=Theme.TEXT_DIM, font=('', 9))
            lbl.pack(side=tk.LEFT)
            self.slave_status_labels.append((oid, dot))

    def _switch_slave(self, s):
        self.current_slave = s
        for i, btn in enumerate(self.slave_btns):
            st = 'SlaveTabSel.TButton' if (i + 1) == s else 'SlaveTab.TButton'
            btn.configure(style=st)
        self._update_display()
        if HAS_MATPLOTLIB:
            self._reset_chart_view()

    # -------------------- 数据监控 Tab --------------------
    def _build_monitor_tab(self):
        tab = tk.Frame(self.notebook, bg=Theme.BG)
        self.notebook.add(tab, text='  数据监控  ')

        # --- 顶部：气压卡片 + 雨滴报警卡片 ---
        top_frame = tk.Frame(tab, bg=Theme.BG)
        top_frame.pack(fill=tk.X, padx=10, pady=(8, 3))
        p_card = tk.Frame(top_frame, bg=Theme.BG_CARD, width=520, height=82)
        p_card.pack(side=tk.LEFT, padx=5)
        p_card.pack_propagate(False)
        ttk.Label(p_card, text='BME280（从机 / 主机）', style='CardTitle.TLabel').pack(anchor='w', padx=10, pady=(6, 0))
        self.pressure_label = tk.Label(p_card, text='--', bg=Theme.BG_CARD,
                                        fg=Theme.ORANGE, font=('', 12, 'bold'),
                                        justify=tk.LEFT)
        self.pressure_label.pack(anchor='w', padx=10)

        r_card = tk.Frame(top_frame, bg=Theme.BG_CARD, width=200, height=82)
        r_card.pack(side=tk.LEFT, padx=5)
        r_card.pack_propagate(False)
        ttk.Label(r_card, text='雨滴报警', style='CardTitle.TLabel').pack(anchor='w', padx=10, pady=(6, 0))
        self.rain_label = tk.Label(r_card, text='未启用', bg=Theme.BG_CARD,
                                    fg=Theme.GREEN_DK, font=('', 18, 'bold'))
        self.rain_label.pack(anchor='w', padx=10)
        tk.Label(r_card, text='雨滴传感器', bg=Theme.BG_CARD,
                 fg=Theme.TEXT_DIM, font=('', 10)).pack(anchor='w', padx=10)

        # 当前从机指示
        self.slave_indicator = tk.Label(top_frame, text='当前显示: 从机1', bg=Theme.BG,
                                         fg=Theme.ACCENT, font=('', 14, 'bold'))
        self.slave_indicator.pack(side=tk.LEFT, padx=20)

        # --- 36路温湿度表格 ---
        temp_frame = ttk.LabelFrame(tab, text=' 36路温湿度节点 (6端口 × 6传感器) ',
                                    style='Section.TLabelframe')
        temp_frame.pack(fill=tk.X, padx=10, pady=3)

        grid_frame = tk.Frame(temp_frame, bg=Theme.BG_CARD)
        grid_frame.pack(fill=tk.X, padx=6, pady=6)

        tk.Label(grid_frame, text='', bg=Theme.BG_CARD).grid(row=0, column=0, padx=2, pady=2)
        for s in range(NUM_SENSORS):
            tk.Label(grid_frame, text=f'传感器{s+1}', bg=Theme.BG_CARD,
                     fg=Theme.TEXT_DIM, font=('', 9, 'bold')).grid(
                row=0, column=s+1, padx=2, pady=2, sticky='ew')
        for c in range(NUM_SENSORS + 1):
            grid_frame.columnconfigure(c, weight=1)

        self.temp_labels = []
        self.humi_labels = []
        for p in range(NUM_PORTS):
            tk.Label(grid_frame, text=f'端口{p+1}', bg=Theme.BG_CARD,
                     fg=Theme.PORT_COLORS[p], font=('', 9, 'bold')).grid(
                row=p*2+1, column=0, rowspan=2, padx=2, pady=1, sticky='ew')
            for s in range(NUM_SENSORS):
                cell = tk.Frame(grid_frame, bg=Theme.BG_CARD)
                cell.grid(row=p*2+1, column=s+1, rowspan=2, padx=2, pady=1, sticky='nsew')
                t_lbl = tk.Label(cell, text='--', bg=Theme.BG_CARD,
                                 fg=Theme.TEXT_HI, font=('', 11, 'bold'))
                t_lbl.pack(anchor='center')
                h_lbl = tk.Label(cell, text='--', bg=Theme.BG_CARD,
                                 fg=Theme.CYAN, font=('', 9))
                h_lbl.pack(anchor='center')
                idx = p * NUM_SENSORS + s
                self.temp_labels.append((idx, t_lbl))
                self.humi_labels.append((idx, h_lbl))

        # --- 趋势图 ---
        chart_outer = tk.Frame(tab, bg=Theme.BG)
        chart_outer.pack(fill=tk.BOTH, expand=True, padx=10, pady=3)

        if HAS_MATPLOTLIB:
            sel_frame = tk.Frame(chart_outer, bg=Theme.BG)
            sel_frame.pack(fill=tk.X, pady=(0, 2))
            ttk.Label(sel_frame, text='图表: ', style='Dim.TLabel').pack(side=tk.LEFT)
            self.chart_type_var = tk.StringVar(value='温度')
            type_combo = ttk.Combobox(sel_frame, textvariable=self.chart_type_var,
                                       width=8, state='readonly')
            type_combo['values'] = ['温度', '湿度']
            type_combo.pack(side=tk.LEFT, padx=5)
            type_combo.bind('<<ComboboxSelected>>', self._on_chart_filter_changed)
            ttk.Label(sel_frame, text=' 端口: ', style='Dim.TLabel').pack(side=tk.LEFT)
            self.chart_port_var = tk.StringVar(value='全部端口')
            port_combo = ttk.Combobox(sel_frame, textvariable=self.chart_port_var,
                                       width=10, state='readonly')
            port_combo['values'] = ['全部端口'] + [f'端口{p+1}' for p in range(NUM_PORTS)]
            port_combo.pack(side=tk.LEFT, padx=5)
            port_combo.bind('<<ComboboxSelected>>', self._on_chart_filter_changed)

            ttk.Button(sel_frame, text='全局', style='SmallFlat.TButton',
                       command=self._reset_chart_view).pack(side=tk.LEFT, padx=(16, 2))
            ttk.Button(sel_frame, text='横放大', style='SmallFlat.TButton',
                       command=lambda: self._zoom_chart('x', 0.7)).pack(side=tk.LEFT, padx=2)
            ttk.Button(sel_frame, text='横缩小', style='SmallFlat.TButton',
                       command=lambda: self._zoom_chart('x', 1.4)).pack(side=tk.LEFT, padx=2)
            ttk.Button(sel_frame, text='纵放大', style='SmallFlat.TButton',
                       command=lambda: self._zoom_chart('y', 0.7)).pack(side=tk.LEFT, padx=2)
            ttk.Button(sel_frame, text='纵缩小', style='SmallFlat.TButton',
                       command=lambda: self._zoom_chart('y', 1.4)).pack(side=tk.LEFT, padx=2)

            self.fig = Figure(figsize=(10, 3), dpi=100, facecolor=Theme.CHART_BG)
            self.ax = self.fig.add_subplot(111)
            self._style_chart()
            self.canvas = FigureCanvasTkAgg(self.fig, chart_outer)
            self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
            self.canvas.mpl_connect('button_press_event', self._on_chart_mouse_press)
            self.canvas.mpl_connect('motion_notify_event', self._on_chart_mouse_move)
            self.canvas.mpl_connect('button_release_event', self._on_chart_mouse_release)
        else:
            ttk.Label(chart_outer,
                      text='安装 matplotlib 可显示趋势图\npip install matplotlib',
                      style='Dim.TLabel').pack(pady=30)

        # --- 数据日志 ---
        log_frame = tk.Frame(tab, bg=Theme.BG)
        log_frame.pack(fill=tk.BOTH, expand=False, padx=10, pady=(3, 8))
        self.log_text = tk.Text(log_frame, height=4, bg=Theme.BG_CARD, fg=Theme.TEXT,
                                font=('Consolas', 11), borderwidth=0,
                                state='disabled', wrap='word')
        self.log_text.pack(fill=tk.BOTH, expand=True)
        self.log_text.tag_config('recv', foreground=Theme.ACCENT)
        self.log_text.tag_config('send', foreground=Theme.PURPLE)

        # 初始显示
        self._update_display()
        if HAS_MATPLOTLIB:
            self._update_chart()

    def _build_control_tab(self):
        tab = tk.Frame(self.notebook, bg=Theme.BG)
        self.notebook.add(tab, text='  控制面板  ')

        # --- 主机列表配置 (上位机 → 控制室, 私有 0x30) ---
        list_frame = ttk.LabelFrame(tab, text=' 主机列表配置 (上位机 → 控制室 0x30) ',
                                    style='Section.TLabelframe')
        list_frame.pack(fill=tk.X, padx=10, pady=(10, 5))

        host_list_row = tk.Frame(list_frame, bg=Theme.BG_CARD)
        host_list_row.pack(fill=tk.X, padx=8, pady=5)
        tk.Label(host_list_row, text='控制室轮询主机:', bg=Theme.BG_CARD,
                 fg=Theme.TEXT, font=('', 11, 'bold')).pack(side=tk.LEFT, padx=(5, 10))

        self.host_list_vars = []
        for h in range(NUM_HOSTS):
            g = h + 1
            var = tk.BooleanVar(value=True)
            self.host_list_vars.append(var)
            cb = ttk.Checkbutton(host_list_row, text=f'主机{g}', variable=var)
            cb.pack(side=tk.LEFT, padx=5)

        ttk.Button(host_list_row, text='下发主机列表', style='SmallBlue.TButton',
                   command=self._send_host_list).pack(side=tk.LEFT, padx=15)
        ttk.Label(host_list_row,
                  text='  (控制室按温度帧/环境帧串行轮询列表内主机)',
                  style='Dim.TLabel').pack(side=tk.LEFT, padx=5)

        # --- 主机控制: 4 行, 每行一条主机全部命令 ---
        host_frame = ttk.LabelFrame(tab, text=' 主机控制 (LoRa: 控制室 → 主机1~4) ',
                                    style='Section.TLabelframe')
        host_frame.pack(fill=tk.X, padx=10, pady=5)

        self.freq_vars = []
        self.temp_vars = []
        self.host_status_vars = {}
        tk.Label(host_frame,
                 text='自动温控：先设目标温度，再点击“自动”启用；设温度不会改变当前手动/自动模式。',
                 bg=Theme.BG_CARD, fg=Theme.TEXT_DIM, font=('', 10)
                 ).pack(anchor='w', padx=13, pady=(5, 0))
        for h in range(NUM_HOSTS):
            g = h + 1  # 主机组号 1~4
            sub = tk.Frame(host_frame, bg=Theme.BG_CARD)
            sub.pack(fill=tk.X, padx=8, pady=4)
            tk.Label(sub, text=f'主机{g}', bg=Theme.BG_CARD,
                     fg=Theme.SLAVE_COLORS[h % len(Theme.SLAVE_COLORS)],
                     font=('', 12, 'bold'), width=6).pack(side=tk.LEFT, padx=(5, 8))

            # 频率
            tk.Label(sub, text='频率(Hz)', bg=Theme.BG_CARD,
                     fg=Theme.TEXT_DIM, font=('', 10)).pack(side=tk.LEFT)
            fv = tk.StringVar(value='50.0')
            self.freq_vars.append(fv)
            ttk.Entry(sub, textvariable=fv, width=6).pack(side=tk.LEFT, padx=3)
            ttk.Button(sub, text='设频率', style='SmallBlue.TButton',
                       command=lambda g=g, h=h: self._send_freq(g, self.freq_vars[h].get())
                       ).pack(side=tk.LEFT, padx=2)

            # 目标温度
            tk.Label(sub, text='目标温度(℃)', bg=Theme.BG_CARD,
                     fg=Theme.TEXT_DIM, font=('', 10)).pack(side=tk.LEFT, padx=(10, 0))
            tv = tk.StringVar(value='26.0')
            self.temp_vars.append(tv)
            ttk.Entry(sub, textvariable=tv, width=6).pack(side=tk.LEFT, padx=3)
            ttk.Button(sub, text='设温度', style='SmallBlue.TButton',
                       command=lambda g=g, h=h: self._send_target_temp(g, self.temp_vars[h].get())
                       ).pack(side=tk.LEFT, padx=2)

            # 风机/自动/查询/遥测读取
            ttk.Button(sub, text='手动启动', style='SmallGreen.TButton',
                       command=lambda g=g: self._send_manual_run(g)).pack(side=tk.LEFT, padx=(10, 2))
            ttk.Button(sub, text='手动停止', style='SmallRed.TButton',
                       command=lambda g=g: self._send_manual_stop(g)).pack(side=tk.LEFT, padx=2)
            ttk.Button(sub, text='自动', style='SmallFlat.TButton',
                       command=lambda g=g: self._send_set_auto(g)).pack(side=tk.LEFT, padx=2)
            ttk.Button(sub, text='查询状态', style='SmallFlat.TButton',
                       command=lambda g=g: self._send_query_status(g)).pack(side=tk.LEFT, padx=2)
            ttk.Button(sub, text='请求温度', style='SmallFlat.TButton',
                       command=lambda g=g: self._send_read_temp(g, force=False)).pack(side=tk.LEFT, padx=2)
            ttk.Button(sub, text='请求环境', style='SmallFlat.TButton',
                       command=lambda g=g: self._send_read_env(g, force=False)).pack(side=tk.LEFT, padx=2)
            status_var = tk.StringVar(value='状态：未查询')
            self.host_status_vars[g] = status_var
            tk.Label(sub, textvariable=status_var, bg=Theme.BG_CARD,
                     fg=Theme.TEXT_DIM, font=('', 9), anchor='w'
                     ).pack(side=tk.LEFT, padx=(10, 2))

        # 全局: 请求所有主机温度 (允许缓存)
        bulk = tk.Frame(host_frame, bg=Theme.BG_CARD)
        bulk.pack(fill=tk.X, padx=8, pady=(4, 8))
        ttk.Button(bulk, text='请求所有主机温度 (允许缓存)', style='SmallBlue.TButton',
                   command=self._send_read_temp_all).pack(side=tk.LEFT, padx=5)
        ttk.Button(bulk, text='强制重采样所有主机温度', style='SmallRed.TButton',
                   command=lambda: self._send_read_temp_all(force=True)).pack(side=tk.LEFT, padx=5)
        ttk.Label(bulk, text='  (强制重采样: 主机会重新向从机请求采集)',
                  style='Dim.TLabel').pack(side=tk.LEFT, padx=5)

        # --- 控制响应 ---
        rsp_frame = ttk.LabelFrame(tab, text=' 控制响应 (ACK/RESULT/ERROR) ',
                                   style='Section.TLabelframe')
        rsp_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        self.rsp_text = tk.Text(rsp_frame, height=5, bg=Theme.BG_CARD, fg=Theme.TEXT,
                                font=('Consolas', 11), borderwidth=0,
                                state='disabled', wrap='word')
        self.rsp_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # --- 已发送指令 ---
        cmd_frame = ttk.LabelFrame(tab, text=' 已发送指令 (LoRa 二进制) ',
                                   style='Section.TLabelframe')
        cmd_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(5, 10))
        self.cmd_log_text = tk.Text(cmd_frame, height=4, bg=Theme.BG_CARD, fg=Theme.TEXT,
                                    font=('Consolas', 11), borderwidth=0,
                                    state='disabled', wrap='word')
        self.cmd_log_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

    # -------------------- 图表 --------------------
    def _style_chart(self):
        self.ax.clear()
        self.ax.set_facecolor(Theme.CHART_BG)
        is_temp = self.chart_type_var.get() == '温度'
        title = f'从机{self.current_slave} - ' + ('温度趋势' if is_temp else '湿度趋势')
        self.ax.set_title(title, color=Theme.TEXT, fontsize=13)
        self.ax.set_xlabel('时间', color=Theme.TEXT_DIM, fontsize=11)
        self.ax.set_ylabel('℃' if is_temp else '%RH', color=Theme.TEXT_DIM, fontsize=11)
        self.ax.tick_params(colors=Theme.TEXT_DIM, labelsize=10)
        for spine in self.ax.spines.values():
            spine.set_color(Theme.CHART_GD)
        self.ax.grid(True, color=Theme.CHART_GD, linewidth=0.5)

    def _on_chart_filter_changed(self, _event=None):
        """切换图表类型或端口后，恢复该筛选条件下的完整视图。"""
        self._reset_chart_view()

    def _reset_chart_view(self):
        """恢复全局视图；新数据到来后会自动扩展到最新历史。"""
        self.chart_view_mode = 'global'
        self.chart_xlim = None
        self.chart_ylim = None
        self.chart_full_xlim = None
        self.chart_full_ylim = None
        self.chart_view_context = None
        self.chart_pan_start = None
        if HAS_MATPLOTLIB and hasattr(self, 'canvas'):
            self._update_chart()

    @staticmethod
    def _is_chart_number(value):
        return (isinstance(value, (int, float)) and not isinstance(value, bool)
                and value == value)

    def _chart_times(self, history):
        """返回真实采集时刻；旧记录缺少完整日期时使用相邻秒数兜底。"""
        fallback_start = datetime.now() - timedelta(seconds=max(0, len(history) - 1))
        result = []
        for idx, rec in enumerate(history):
            value = rec.get('full_date') or rec.get('chart_time') or rec.get('timestamp')
            if isinstance(value, datetime):
                result.append(value)
                continue
            if isinstance(value, str):
                for fmt in ('%Y-%m-%d %H:%M:%S', '%Y-%m-%dT%H:%M:%S', '%H:%M:%S'):
                    try:
                        parsed = datetime.strptime(value, fmt)
                        if fmt == '%H:%M:%S':
                            parsed = datetime.combine(datetime.now().date(), parsed.time())
                        result.append(parsed)
                        break
                    except ValueError:
                        continue
                else:
                    result.append(fallback_start + timedelta(seconds=idx))
            else:
                result.append(fallback_start + timedelta(seconds=idx))
        return result

    @staticmethod
    def _clamp_chart_window(limits, full_limits, min_span):
        """把缩放/拖移后的窗口限制在全局边界内。"""
        full_lo, full_hi = full_limits
        full_span = full_hi - full_lo
        if full_span <= 0:
            return full_limits
        span = max(min_span, limits[1] - limits[0])
        if span >= full_span:
            return full_limits
        center = (limits[0] + limits[1]) / 2.0
        lo = center - span / 2.0
        hi = center + span / 2.0
        if lo < full_lo:
            hi += full_lo - lo
            lo = full_lo
        if hi > full_hi:
            lo -= hi - full_hi
            hi = full_hi
        return (lo, hi)

    @staticmethod
    def _dynamic_chart_window(limits, min_span):
        """横轴动态窗口：只限制最小跨度，不受现有数据首尾边界约束。"""
        lo, hi = limits
        if hi < lo:
            lo, hi = hi, lo
        center = (lo + hi) / 2.0
        span = max(min_span, hi - lo)
        return (center - span / 2.0, center + span / 2.0)

    def _calculate_chart_limits(self, x_times, values, is_temp):
        x_values = mdates.date2num(x_times)
        if len(x_values) == 1:
            x_pad = 30.0 / 86400.0
        else:
            x_pad = max((x_values[-1] - x_values[0]) * 0.02, 1.0 / 86400.0)
        full_xlim = (x_values[0] - x_pad, x_values[-1] + x_pad)

        if is_temp:
            # 温度全局视图显示 0～45 ℃，局部观察通过纵向缩放完成。
            full_ylim = (0.0, 45.0)
        else:
            valid = [float(v) for v in values if self._is_chart_number(v)]
            if not valid:
                full_ylim = (0.0, 100.0)
            else:
                lo, hi = min(valid), max(valid)
                pad = max((hi - lo) * 0.08, 1.0)
                full_ylim = (max(0.0, lo - pad), min(100.0, hi + pad))
                if full_ylim[1] - full_ylim[0] < 1.0:
                    full_ylim = (max(0.0, lo - 0.5), min(100.0, hi + 0.5))
        return full_xlim, full_ylim

    def _apply_chart_view(self, x_times, values, is_temp, context):
        full_xlim, full_ylim = self._calculate_chart_limits(x_times, values, is_temp)
        if context != self.chart_view_context:
            self.chart_view_mode = 'global'
            self.chart_xlim = None
            self.chart_ylim = None
            self.chart_view_context = context

        self.chart_full_xlim = full_xlim
        self.chart_full_ylim = full_ylim
        if self.chart_view_mode == 'global':
            self.chart_xlim, self.chart_ylim = full_xlim, full_ylim
        else:
            self.chart_xlim = self._dynamic_chart_window(
                self.chart_xlim or full_xlim, 10.0 / 86400.0)
            min_y_span = 0.1 if is_temp else 1.0
            self.chart_ylim = self._clamp_chart_window(
                self.chart_ylim or full_ylim, full_ylim, min_y_span)

        self.ax.set_xlim(self.chart_xlim)
        self.ax.set_ylim(self.chart_ylim)
        locator = mdates.AutoDateLocator(minticks=3, maxticks=7)
        self.ax.xaxis.set_major_locator(locator)
        self.ax.xaxis.set_major_formatter(mdates.ConciseDateFormatter(locator))

    def _zoom_chart(self, axis, factor):
        """仅缩放指定轴：x=时间，y=温度/湿度。"""
        if not self.chart_full_xlim or not self.chart_full_ylim:
            return
        is_temp = self.chart_type_var.get() == '温度'
        if axis == 'x':
            current = self.chart_xlim or self.chart_full_xlim
            min_span = 10.0 / 86400.0
            center = (current[0] + current[1]) / 2.0
            new_span = (current[1] - current[0]) * factor
            limits = self._dynamic_chart_window(
                (center - new_span / 2.0, center + new_span / 2.0), min_span)
        else:
            current = self.chart_ylim or self.chart_full_ylim
            full = self.chart_full_ylim
            min_span = 0.1 if is_temp else 1.0
            center = (current[0] + current[1]) / 2.0
            new_span = (current[1] - current[0]) * factor
            limits = self._clamp_chart_window(
                (center - new_span / 2.0, center + new_span / 2.0), full, min_span)
        self.chart_view_mode = 'manual'
        if axis == 'x':
            self.chart_xlim = limits
        else:
            self.chart_ylim = limits
        if (self.chart_xlim == self.chart_full_xlim
                and self.chart_ylim == self.chart_full_ylim):
            self.chart_view_mode = 'global'
        self._update_chart()

    def _on_chart_mouse_press(self, event):
        if (self.chart_full_xlim is None or self.chart_full_ylim is None
                or event.button != 1 or event.inaxes is not self.ax
                or event.xdata is None or event.ydata is None):
            return
        self.chart_pan_start = (event.xdata, event.ydata,
                                self.chart_xlim or self.ax.get_xlim(),
                                self.chart_ylim or self.ax.get_ylim())

    def _on_chart_mouse_move(self, event):
        if (self.chart_pan_start is None or event.inaxes is not self.ax
                or event.xdata is None or event.ydata is None):
            return
        start_x, start_y, start_xlim, start_ylim = self.chart_pan_start
        self.chart_xlim = self._dynamic_chart_window(
            (start_xlim[0] + start_x - event.xdata,
             start_xlim[1] + start_x - event.xdata),
            10.0 / 86400.0)
        min_y_span = 0.1 if self.chart_type_var.get() == '温度' else 1.0
        self.chart_ylim = self._clamp_chart_window(
            (start_ylim[0] + start_y - event.ydata,
             start_ylim[1] + start_y - event.ydata),
            self.chart_full_ylim, min_y_span)
        self.chart_view_mode = 'manual'
        self.ax.set_xlim(self.chart_xlim)
        self.ax.set_ylim(self.chart_ylim)
        self.canvas.draw_idle()

    def _on_chart_mouse_release(self, _event):
        self.chart_pan_start = None

    def _update_chart(self):
        if not HAS_MATPLOTLIB:
            return
        self._style_chart()
        history = list(self.slave_data.get(self.current_slave, []))
        if not history:
            self.canvas.draw()
            return

        is_temp = self.chart_type_var.get() == '温度'
        key = 'temperatures' if is_temp else 'humidities'
        port_sel = self.chart_port_var.get()
        x = self._chart_times(history)
        chart_values = []

        if port_sel == '全部端口':
            for p in range(NUM_PORTS):
                base = Theme.PORT_COLORS[p]
                for s in range(NUM_SENSORS):
                    idx = p * NUM_SENSORS + s
                    vals = []
                    for rec in history:
                        arr = rec.get(key, [])
                        vals.append(arr[idx] if idx < len(arr) and arr[idx] is not None else None)
                    if any(v is not None for v in vals):
                        chart_values.extend(v for v in vals if self._is_chart_number(v))
                        self.ax.plot(x, vals, color=base, linewidth=0.7,
                                     alpha=0.4 + 0.6 * (s + 1) / NUM_SENSORS,
                                     label=f'P{p+1}S{s+1}')
            handles, labels = self.ax.get_legend_handles_labels()
            seen = set()
            port_colors, sl = [], []
            for h, l in zip(handles, labels):
                pk = l[:2]
                if pk not in seen:
                    seen.add(pk)
                    # 全部端口的图例用实心色块表示端口；曲线本身仍保持原来的细线。
                    port_colors.append(h.get_color())
                    sl.append(pk)
            if port_colors:
                legend_handles = [Patch(facecolor=color, edgecolor='none')
                                  for color in port_colors]
                self.ax.legend(legend_handles, sl, loc='upper left', fontsize=8,
                               facecolor=Theme.BG_CARD, edgecolor=Theme.BORDER,
                               labelcolor=Theme.TEXT_DIM, ncol=6,
                               handlelength=2.0, handleheight=0.8)
        else:
            p = int(port_sel.replace('端口', '')) - 1
            for s in range(NUM_SENSORS):
                idx = p * NUM_SENSORS + s
                vals = []
                for rec in history:
                    arr = rec.get(key, [])
                    vals.append(arr[idx] if idx < len(arr) and arr[idx] is not None else None)
                if any(v is not None for v in vals):
                    chart_values.extend(v for v in vals if self._is_chart_number(v))
                    self.ax.plot(x, vals, color=Theme.PORT_COLORS[p],
                                 linewidth=1.5,
                                 alpha=0.4 + 0.6 * (s + 1) / NUM_SENSORS,
                                 label=f'传感器{s+1}')
            self.ax.legend(loc='upper left', fontsize=9,
                           facecolor=Theme.BG_CARD, edgecolor=Theme.BORDER,
                           labelcolor=Theme.TEXT_DIM)

        context = (self.current_slave, self.chart_type_var.get(), port_sel)
        self._apply_chart_view(x, chart_values, is_temp, context)
        self.fig.tight_layout()
        self.canvas.draw()

    # -------------------- 数据处理 --------------------
    def _on_data_received(self, data, is_binary=False):
        if is_binary:
            self.after(0, self._process_binary_frame, data)
        else:
            self.after(0, self._process_data, data)

    def _process_data(self, data):
        """ASCII 文本解析: 优先 JSON (控制室温度), 回退旧格式"""
        self._log(data, 'recv')
        if data.startswith('RSP:'):
            self._show_response(data)
            return
        # 控制室实际发的是 ASCII JSON 行 (每批 6 行, channel 1~6)
        if data.startswith('{'):
            if self._handle_json_line(data):
                return
        parsed = self.parser.parse(data)
        slave = parsed.get('slave', 1)
        if not (1 <= slave <= NUM_SLAVES):
            slave = 1
            parsed['slave'] = 1

        has_sensor = ('temperatures' in parsed or 'humidities' in parsed
                      or 'pressure' in parsed or 'rain' in parsed)
        if has_sensor:
            parsed['full_date'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            self.slave_data[slave].append(parsed)
            try:
                self.db.insert_sensor_data(parsed)
            except Exception:
                pass
            self._update_slave_status(slave)
            if slave == self.current_slave:
                self._update_display()
                self._update_chart()

    # -------------------- JSON 温度批处理 (控制室实际格式) --------------------
    def _handle_json_line(self, line):
        """解析一行温度 JSON, 按 (slave, flow_id) 组装 6 通道成完整 36 点"""
        try:
            obj = json.loads(line)
        except Exception:
            return False
        if not isinstance(obj, dict) or obj.get('type') != 'TEMP_36':
            return False
        slave = obj.get('slave', 0)
        if not (1 <= slave <= NUM_SLAVES):
            return False
        flow_id = obj.get('flow', 0)
        channel = obj.get('channel', 0)
        if not (1 <= channel <= NUM_PORTS):
            return False
        values = obj.get('values', [])
        if not isinstance(values, list) or len(values) != NUM_SENSORS:
            return False
        vals = [(float(v) if v is not None else None) for v in values]

        key = (slave, flow_id)
        pending = self.pending_temps.get(key)
        if pending is None or channel == 1:
            # 新批: channel==1 时覆盖旧的不完整批
            captured_at = datetime.now()
            pending = {
                'temps': [None] * NUM_NODES,
                'channels': set(),
                'flow_id': flow_id,
                'slave': slave,
                'timestamp': captured_at.strftime('%H:%M:%S'),
                'full_date': captured_at.strftime('%Y-%m-%d %H:%M:%S'),
            }
            self.pending_temps[key] = pending
        # 填入本通道 6 个温度: channel 1->索引0..5, channel 2->6..11, ...
        base = (channel - 1) * NUM_SENSORS
        for i in range(NUM_SENSORS):
            pending['temps'][base + i] = vals[i]
        pending['channels'].add(channel)

        self._update_slave_status(slave)
        # 增量刷新当前从机显示 (用户可看到数据陆续到达)
        if slave == self.current_slave:
            self._update_temp_labels(pending['temps'])
            if HAS_MATPLOTLIB:
                self._update_chart()

        # 6 通道到齐 -> 提交历史 + 数据库
        if len(pending['channels']) >= NUM_PORTS:
            record = {
                'timestamp': pending['timestamp'],
                'full_date': pending['full_date'],
                'slave': slave,
                'temperatures': list(pending['temps']),
                'humidities': [],
                'pressure': None,
                'rain': None,
                'port': None,
                'num': None,
                'flow_id': flow_id,
            }
            self.slave_data[slave].append(record)
            try:
                self.db.insert_sensor_data(record)
            except Exception:
                pass
            del self.pending_temps[key]
        return True

    def _update_temp_labels(self, temps):
        """仅更新 36 个温度标签 (JSON 增量刷新用), 湿度/气压/雨滴保持 '--'"""
        for idx, lbl in self.temp_labels:
            if idx < len(temps) and temps[idx] is not None:
                t = temps[idx]
                lbl.config(text=f'{t:.1f}', fg=self._temp_color(t))
            else:
                lbl.config(text='--', fg=Theme.TEXT_DIM)
        for _, lbl in self.humi_labels:
            lbl.config(text='--', fg=Theme.TEXT_DIM)
        self.pressure_label.config(text='--')
        self.rain_label.config(text='未启用', fg=Theme.TEXT_DIM)

    # -------------------- LoRa 二进制帧处理 --------------------
    # 新控制室固件 (ControlRoomLoRa) 是纯二进制透传:
    #   USART2(LoRa) 收到的帧 → route_frame → up_queue → relay_process → USART1(PC)
    #   CRC 从版本字节(buf[2])起算, 跳过 AA55 帧头, 与固件 frame.c 一致
    def _process_binary_frame(self, frame: bytes):
        parsed = LoRaProtocol.parse_packet(frame, strict=True)
        hex_str = frame.hex(' ').upper()
        if not parsed:
            self._log(f'[CRC错误/格式错误] {hex_str}', 'recv')
            return
        if not LoRaProtocol.validate_control_room_response(parsed):
            self._log(f'[方向/地址错误] {hex_str}', 'recv')
            return
        msg_type = parsed['msg_type']
        name = LoRaProtocol.msg_name(msg_type)
        sender = (LoRaProtocol.ROLE_NAMES.get(parsed['sender_role'], '?'),
                  parsed['sender_group'])
        self._log(
            f'[{name}] {sender[0]}{sender[1]} 流水号={parsed["flow_id"]} | {hex_str}',
            'recv')

        if msg_type == LoRaProtocol.MSG_TEMP_36:
            self._handle_temp_36(parsed)
        elif msg_type == LoRaProtocol.MSG_ENV_DATA:
            self._handle_env_data(parsed)
        elif msg_type == LoRaProtocol.MSG_ACK:
            self._handle_ack(parsed)
        elif msg_type == LoRaProtocol.MSG_RESULT:
            self._handle_result(parsed)
        elif msg_type == LoRaProtocol.MSG_ERROR:
            self._handle_error(parsed)
        # 其他类型仅记录日志, 不做业务处理

    def _handle_temp_36(self, parsed):
        """处理独立温度帧，只更新节点与两套 BME 温度。"""
        data = parsed['data']
        decoded = LoRaProtocol.decode_temperature_data(data)
        # 主机转发, sender_group 即对应从机序号 (M1↔S1 同组)
        slave = parsed['sender_group']
        if not (1 <= slave <= NUM_SLAVES):
            self._log(f'[组号异常] sender_group={slave}, 无法关联从机')
            return
        self.latest_v2[slave].update(decoded)
        record = self._make_v2_record(slave, parsed['flow_id'], 'temperature')
        self._commit_v2_record(slave, record)

    def _handle_env_data(self, parsed):
        """处理独立环境帧，保留最近一次温度帧中的所有温度。"""
        slave = parsed['sender_group']
        if not (1 <= slave <= NUM_SLAVES):
            self._log(f'[组号异常] sender_group={slave}, 无法关联从机')
            return
        decoded = LoRaProtocol.decode_environment_data(parsed['data'])
        self.latest_v2[slave].update(decoded)
        record = self._make_v2_record(slave, parsed['flow_id'], 'environment')
        self._commit_v2_record(slave, record)

    def _make_v2_record(self, slave, flow_id, frame_kind):
        captured_at = datetime.now()
        state = self.latest_v2[slave]
        master_pressure = state.get('master_bme_pressure_pa')
        slave_pressure = state.get('slave_bme_pressure_pa')
        display_pressure = master_pressure if master_pressure is not None else slave_pressure
        record = dict(state)
        record.update({
            'timestamp': captured_at.strftime('%H:%M:%S'),
            'full_date': captured_at.strftime('%Y-%m-%d %H:%M:%S'),
            'slave': slave,
            'temperatures': list(state['temperatures']),
            'humidities': list(state['humidities']),
            # 兼容现有数据库单气压列；内存中仍保留主、从机两套原始 Pa 值。
            'pressure': (display_pressure / 1000.0
                         if display_pressure is not None else None),
            'port': None,
            'num': None,
            'flow_id': flow_id,
            'frame_kind': frame_kind,
        })
        return record

    def _commit_v2_record(self, slave, record):
        self.slave_data[slave].append(record)
        try:
            self.db.insert_sensor_data(record)
        except Exception:
            pass
        self._update_slave_status(slave)
        if slave == self.current_slave:
            self._update_display()
            self._update_chart()

    def _handle_ack(self, parsed):
        # 数据区: 接收状态、拒绝原因 (具体定义见设备侧)
        data = parsed['data']
        status = data[0] if len(data) > 0 else None
        reason = data[1] if len(data) > 1 else None
        status_map = {0: '接受', 1: '拒绝'}
        st_str = status_map.get(status, hex(status) if status is not None else '?')
        self._show_response(
            f'ACK 流水号={parsed["flow_id"]} 状态={st_str} 原因={reason} | {data.hex(" ").upper()}')

    def _handle_result(self, parsed):
        try:
            result = LoRaProtocol.decode_result_data(parsed['data'])
        except ValueError as exc:
            self._show_response(f'RESULT 流水号={parsed["flow_id"]} 格式错误：{exc}')
            return

        host_group = parsed['sender_group']
        status_text = (
            f'状态：{result["result_name"]} | 模式：{result["control_mode_name"]} | '
            f'风机：{result["fan_state_name"]} | {result["frequency_hz"]:.2f}Hz | '
            f'{result["target_temperature_c"]:.1f}℃')
        status_var = self.host_status_vars.get(host_group)
        if status_var is not None:
            status_var.set(status_text)
        if 1 <= host_group <= len(self.freq_vars):
            self.freq_vars[host_group - 1].set(f'{result["frequency_hz"]:.2f}')
            self.temp_vars[host_group - 1].set(f'{result["target_temperature_c"]:.1f}')
        self._show_response(
            f'RESULT 主机{host_group} 流水号={parsed["flow_id"]} | {status_text}')

    def _handle_error(self, parsed):
        # 数据区: 错误码、补充信息
        data = parsed['data']
        err_code = data[0] if len(data) > 0 else None
        self._show_response(
            f'ERROR 流水号={parsed["flow_id"]} 错误码={err_code} | {data.hex(" ").upper()}')

    def _update_slave_status(self, slave):
        """点亮对应从机状态指示灯"""
        idx = slave - 1
        if 0 <= idx < len(self.slave_status_labels):
            oid, canvas = self.slave_status_labels[idx]
            canvas.itemconfig(oid, fill=Theme.GREEN_DK)
            # 3秒后变暗
            self.after(3000, lambda: self._dim_slave_status(slave))

    def _dim_slave_status(self, slave):
        idx = slave - 1
        if 0 <= idx < len(self.slave_status_labels):
            oid, canvas = self.slave_status_labels[idx]
            canvas.itemconfig(oid, fill=Theme.TEXT_DIM)

    def _update_display(self):
        history = self.slave_data.get(self.current_slave, [])
        self.slave_indicator.config(text=f'当前显示: 从机{self.current_slave}')
        if not history:
            for _, lbl in self.temp_labels:
                lbl.config(text='--', fg=Theme.TEXT_DIM)
            for _, lbl in self.humi_labels:
                lbl.config(text='--', fg=Theme.TEXT_DIM)
            self.pressure_label.config(text='--')
            self.rain_label.config(text='未启用', fg=Theme.TEXT_DIM)
            return

        data = history[-1]
        temps = data.get('temperatures', [])
        humis = data.get('humidities', [])

        for idx, lbl in self.temp_labels:
            if idx < len(temps) and temps[idx] is not None:
                t = temps[idx]
                lbl.config(text=f'{t:.1f}')
                lbl.config(fg=self._temp_color(t))
            else:
                lbl.config(text='--', fg=Theme.TEXT_DIM)

        for idx, lbl in self.humi_labels:
            if idx < len(humis) and humis[idx] is not None:
                lbl.config(text=f'{humis[idx]:.0f}%')
            else:
                lbl.config(text='--', fg=Theme.TEXT_DIM)

        def fmt(value, suffix, digits=1):
            return '--' if value is None else f'{value:.{digits}f}{suffix}'

        slave_pressure = data.get('slave_bme_pressure_pa')
        master_pressure = data.get('master_bme_pressure_pa')
        if (slave_pressure is None and master_pressure is None and
                data.get('pressure') is not None):
            slave_pressure = data['pressure'] * 1000.0
        self.pressure_label.config(
            text=(
                f'从机: {fmt(data.get("slave_bme_temperature"), "℃")}  '
                f'{fmt(data.get("slave_bme_humidity"), "%RH")}  '
                f'{fmt(slave_pressure / 1000.0 if slave_pressure is not None else None, "kPa")}\n'
                f'主机: {fmt(data.get("master_bme_temperature"), "℃")}  '
                f'{fmt(data.get("master_bme_humidity"), "%RH")}  '
                f'{fmt(master_pressure / 1000.0 if master_pressure is not None else None, "kPa")}'
            ))

        rain = data.get('rain')
        if rain is None:
            self.rain_label.config(text='未启用', fg=Theme.TEXT_DIM)
        else:
            # 0x0000..0xFFFE 的单位和干湿方向尚未定义，只显示原始值。
            self.rain_label.config(text=f'原始值 {rain}', fg=Theme.ORANGE)

    def _temp_color(self, temp):
        if temp is None:
            return Theme.TEXT_DIM
        if temp < 20:
            return Theme.CYAN
        elif temp < 30:
            return Theme.GREEN_DK
        elif temp < 35:
            return '#ffa726'
        else:
            return Theme.RED_DK

    # -------------------- 指令发送 (LoRa 二进制) --------------------
    def _send_lora_packet(self, packet: bytes, description: str):
        """发送 LoRa 二进制报文并记录日志
        注意: 当前控制室固件不读 USART1 RX, 需固件升级支持
        (USART1 收到命令帧后转发到 USART2/LoRa), 命令才能真正送达主机
        """
        if not self.serial_mgr.is_connected:
            messagebox.showwarning('提示', '请先连接串口')
            return False
        if not self.serial_mgr.send_bytes(packet):
            messagebox.showerror('错误', '串口写入失败')
            return False
        now = datetime.now().strftime('%H:%M:%S')
        hex_str = packet.hex(' ').upper()
        self.cmd_log_text.config(state='normal')
        self.cmd_log_text.insert(tk.END, f'[{now}] → {description}\n          {hex_str}\n', 'send')
        self.cmd_log_text.see(tk.END)
        self.cmd_log_text.config(state='disabled')
        try:
            self.db.insert_command_log('send', f'{description} | {hex_str}')
        except Exception:
            pass
        self._log(f'→ {description}', 'send')
        return True

    def _send_freq(self, host_group, freq_str):
        try:
            freq = float(freq_str)
            if not (0 <= freq <= 50.0):
                raise ValueError
        except ValueError:
            messagebox.showwarning('提示', f'主机{host_group} 频率范围: 0~50.0 Hz')
            return
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_set_freq(host_group, flow_id, freq)
        self._send_lora_packet(
            packet, f'SET_FREQ 主机{host_group} = {freq} Hz (流水号 {flow_id})')

    def _send_target_temp(self, host_group, temp_str):
        try:
            temp = float(temp_str)
            if not (-55.0 <= temp <= 125.0):
                raise ValueError
        except ValueError:
            messagebox.showwarning('提示', f'主机{host_group} 目标温度范围: -55.0~125.0 ℃')
            return
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_set_target_temp(host_group, flow_id, temp)
        self._send_lora_packet(
            packet, f'SET_TARGET_TEMP 主机{host_group} = {temp} ℃ (流水号 {flow_id})')

    def _send_manual_run(self, host_group):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_manual_run(host_group, flow_id)
        self._send_lora_packet(
            packet, f'MANUAL_RUN 主机{host_group} (流水号 {flow_id})')

    def _send_manual_stop(self, host_group):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_manual_stop(host_group, flow_id)
        self._send_lora_packet(
            packet, f'MANUAL_STOP 主机{host_group} (流水号 {flow_id})')

    def _send_set_auto(self, host_group):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_set_auto(host_group, flow_id)
        self._send_lora_packet(
            packet, f'SET_AUTO 主机{host_group} (流水号 {flow_id})')

    def _send_query_status(self, host_group):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_query_status(host_group, flow_id)
        self._send_lora_packet(
            packet, f'QUERY_STATUS 主机{host_group} (流水号 {flow_id})')

    def _send_read_temp(self, host_group, force=False):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_read_temp(host_group, flow_id, force_resample=force)
        mode = '强制重采样' if force else '允许缓存'
        self._send_lora_packet(
            packet, f'READ_TEMP 主机{host_group} ({mode}, 流水号 {flow_id})')

    def _send_read_env(self, host_group, force=False):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_read_env(host_group, flow_id,
                                           force_resample=force)
        mode = '强制重采样' if force else '允许缓存'
        self._send_lora_packet(
            packet, f'READ_ENV 主机{host_group} ({mode}, 流水号 {flow_id})')

    def _send_read_temp_all(self, force=False):
        """依次向所有主机请求温度 (可选, 控制室自动轮询)"""
        if not self.serial_mgr.is_connected:
            messagebox.showwarning('提示', '请先连接串口')
            return
        for g in range(1, NUM_HOSTS + 1):
            self._send_read_temp(g, force=force)

    def _send_host_list(self):
        """下发主机列表给控制室 (0x30, 上位机→控制室私有)"""
        groups = [i + 1 for i, v in enumerate(self.host_list_vars) if v.get()]
        if not groups:
            messagebox.showwarning('提示', '至少选择一台主机')
            return
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_host_list(groups, flow_id)
        self._send_lora_packet(
            packet, f'HOST_LIST 主机组号={groups} (流水号 {flow_id})')

    def _show_response(self, text):
        now = datetime.now().strftime('%H:%M:%S')
        self.rsp_text.config(state='normal')
        self.rsp_text.insert(tk.END, f'[{now}] ← {text}\n')
        self.rsp_text.see(tk.END)
        self.rsp_text.config(state='disabled')
        try:
            self.db.insert_command_log('recv', text)
        except Exception:
            pass

    # -------------------- 串口连接 --------------------
    def _toggle_connect(self):
        if self.serial_mgr.is_connected:
            self.serial_mgr.disconnect()
            self.connect_btn.config(text='  连接  ', style='Conn.TButton')
            self.status_canvas.itemconfig(self._dot_id, fill=Theme.RED)
            self.status_label.config(text='未连接')
            self._log('已断开连接')
        else:
            port = self.port_combo.get()
            if not port:
                messagebox.showwarning('提示', '请先选择串口')
                return
            port_name = self._get_port_name(port)
            try:
                self.serial_mgr.connect(port_name, self._on_data_received)
                self.connect_btn.config(text='  断开  ', style='Disconn.TButton')
                self.status_canvas.itemconfig(self._dot_id, fill=Theme.GREEN)
                self.status_label.config(text=f'已连接 {port_name} @ 115200')
                self._log(f'已连接 {port_name} @ 115200')
            except Exception as e:
                messagebox.showerror('连接失败', str(e))

    def _get_port_name(self, combo_text):
        return combo_text.split(' - ')[0].strip()

    def _refresh_ports(self):
        ports = self.serial_mgr.list_ports()
        self.port_combo['values'] = ports
        for i, p in enumerate(ports):
            if 'CH340' in p.upper() or 'CH341' in p.upper():
                self.port_combo.current(i)
                return
        if ports:
            self.port_combo.current(0)

    def _auto_refresh(self):
        if not self.serial_mgr.is_connected:
            self._refresh_ports()
        self.after(3000, self._auto_refresh)

    # -------------------- 日志 --------------------
    def _log(self, text, tag=None):
        now = datetime.now().strftime('%H:%M:%S')
        prefix = '← ' if text.startswith('RSP:') or (tag == 'recv') else ''
        if text.startswith('→ '):
            prefix = ''
        line = f'[{now}] {prefix}{text}\n'
        self.log_lines.append(line)
        if len(self.log_lines) > 200:
            self.log_lines = self.log_lines[-200:]
        self.log_text.config(state='normal')
        if tag:
            self.log_text.insert(tk.END, line, tag)
        else:
            self.log_text.insert(tk.END, line)
        self.log_text.see(tk.END)
        self.log_text.config(state='disabled')

    # -------------------- 导出 / 清除 --------------------
    def _export_csv(self):
        history = list(self.slave_data.get(self.current_slave, []))
        if not history:
            messagebox.showwarning('提示', f'从机{self.current_slave} 暂无数据可导出')
            return
        default_name = f'slave{self.current_slave}_' + datetime.now().strftime('%Y%m%d_%H%M%S') + '.csv'
        path = filedialog.asksaveasfilename(
            title=f'导出从机{self.current_slave} CSV', defaultextension='.csv',
            initialfile=default_name, filetypes=[('CSV 文件', '*.csv')])
        if not path:
            return
        try:
            header = ['timestamp']
            header += [f'P{p+1}S{s+1}_T' for p in range(NUM_PORTS) for s in range(NUM_SENSORS)]
            header += [f'P{p+1}S{s+1}_H' for p in range(NUM_PORTS) for s in range(NUM_SENSORS)]
            header += ['气压', '雨滴报警', 'port', 'num']
            with open(path, 'w', newline='', encoding='utf-8-sig') as f:
                writer = csv.writer(f)
                writer.writerow(header)
                for rec in history:
                    temps = rec.get('temperatures', [])
                    humis = rec.get('humidities', [])
                    temps = (temps + [None] * NUM_NODES)[:NUM_NODES]
                    humis = (humis + [None] * NUM_NODES)[:NUM_NODES]
                    row = [rec.get('timestamp', '')] + temps + humis
                    row += [rec.get('pressure', ''), rec.get('rain', ''),
                            rec.get('port', ''), rec.get('num', '')]
                    writer.writerow(row)
            count = len(history)
            messagebox.showinfo('导出成功',
                                f'已导出从机{self.current_slave} {count} 条数据到\n{path}')
            self._log(f'已导出从机{self.current_slave} {count} 条数据到 {path}')
        except Exception as e:
            messagebox.showerror('导出失败', str(e))

    def _clear_data(self):
        for s in range(1, NUM_SLAVES + 1):
            self.slave_data[s].clear()
        self.log_lines.clear()
        self.log_text.config(state='normal')
        self.log_text.delete('1.0', tk.END)
        self.rsp_text.config(state='normal')
        self.rsp_text.delete('1.0', tk.END)
        self.cmd_log_text.config(state='normal')
        self.cmd_log_text.delete('1.0', tk.END)
        for _, lbl in self.temp_labels:
            lbl.config(text='--', fg=Theme.TEXT_DIM)
        for _, lbl in self.humi_labels:
            lbl.config(text='--', fg=Theme.TEXT_DIM)
        self.pressure_label.config(text='--')
        self.rain_label.config(text='未启用', fg=Theme.TEXT_DIM)
        self.log_text.config(state='disabled')
        self.rsp_text.config(state='disabled')
        self.cmd_log_text.config(state='disabled')
        if HAS_MATPLOTLIB:
            self._reset_chart_view()
        self._log('所有从机数据已清除（数据库记录保留）')

    def _on_close(self):
        self.serial_mgr.disconnect()
        self.db.close()
        self.destroy()


if __name__ == '__main__':
    app = App()
    app.mainloop()
