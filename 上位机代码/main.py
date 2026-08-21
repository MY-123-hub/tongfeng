#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""智能通风系统上位机。

正式通信链路：从机采集 → 主机转发 → 控制室 LoRa 网关 → USART1/USB → 本上位机。
上位机与控制室 USART1 只使用 protocol/LORA_TELEMETRY.md 定义的二进制帧：
AA 55 01 [类型] [发送角色/组号] [接收角色/组号] [流水号LE] [长度] [数据] [CRC16LE]。
上位机下发主机命令时使用控制室身份 01/00；控制室原样转发主机返回帧。
"""

import os, csv, json, sqlite3
from datetime import datetime
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
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False

NUM_PORTS = 6
NUM_SENSORS = 6
NUM_NODES = NUM_PORTS * NUM_SENSORS   # 36
NUM_SLAVES = 4
NUM_HOSTS = 4


from upper_protocol import LoRaProtocol


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
            'SELECT timestamp, temperatures, humidities, pressure, rain, port, num '
            'FROM sensor_data WHERE slave=? ORDER BY id DESC LIMIT ?', (slave, limit)
        ).fetchall()
        rows.reverse()
        result = []
        for r in rows:
            try:
                temps = json.loads(r[1]) if r[1] else []
            except Exception:
                temps = []
            try:
                humis = json.loads(r[2]) if r[2] else []
            except Exception:
                humis = []
            result.append({
                'timestamp': r[0], 'temperatures': temps, 'humidities': humis,
                'pressure': r[3], 'rain': r[4], 'port': r[5], 'num': r[6]})
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
#  串口管理
#  只接收正式二进制 LoRa 帧 (AA 55 ...)
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

    def next_flow_id(self):
        self._flow_id = (self._flow_id + 1) & 0xFFFF
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
        """从缓冲区提取完整正式二进制帧；帧外噪声直接丢弃。"""
        while self._buffer:
            idx = -1
            for i in range(len(self._buffer) - 1):
                if self._buffer[i] == 0xAA and self._buffer[i + 1] == 0x55:
                    idx = i
                    break
            if idx < 0:
                # 无帧头：保留末尾可能成为下一帧帧头的 AA。
                if self._buffer[-1] == 0xAA:
                    self._buffer[:] = b'\xAA'
                else:
                    self._buffer.clear()
                return
            if idx > 0:
                # AA 55 前为噪声，直接丢弃。
                del self._buffer[:idx]
            # 此时缓冲区以 AA 55 开头
            if len(self._buffer) < 11:
                return  # 头部不完整, 等待
            data_len = self._buffer[10]
            if data_len > LoRaProtocol.MAX_DATA_LEN:
                del self._buffer[0]
                continue
            total = 11 + data_len + 2
            if len(self._buffer) < total:
                return  # 整帧不完整, 等待
            frame = bytes(self._buffer[:total])
            del self._buffer[:total]
            if self.callback:
                self.callback(frame)

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

        # 每个主机组保留独立的 36 点温度历史；数据库字段 slave 仅兼容旧表名。
        self.slave_data = {i: deque(maxlen=500) for i in range(1, NUM_SLAVES + 1)}
        self.current_slave = 1
        self.log_lines = []
        self.pending_commands = {}
        self.host_control_buttons = {i: [] for i in range(1, NUM_HOSTS + 1)}

        # 从数据库加载历史
        try:
            for s in range(1, NUM_SLAVES + 1):
                for r in self.db.load_recent(s, 500):
                    self.slave_data[s].append(r)
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
        """4 个主机组切换按钮；每组显示同组从机采集的 36 点温度。"""
        bar = tk.Frame(self, bg=Theme.BG, height=45)
        bar.pack(fill=tk.X)
        bar.pack_propagate(False)
        tk.Label(bar, text='选择主机组: ', bg=Theme.BG, fg=Theme.TEXT_DIM,
                 font=('', 11)).pack(side=tk.LEFT, padx=(15, 8), pady=8)
        self.slave_btns = []
        for s in range(1, NUM_SLAVES + 1):
            st = 'SlaveTabSel.TButton' if s == self.current_slave else 'SlaveTab.TButton'
            btn = ttk.Button(bar, text=f'  M{s}（S{s}）  ', style=st,
                             command=lambda s=s: self._switch_slave(s))
            btn.pack(side=tk.LEFT, padx=4, pady=6)
            self.slave_btns.append(btn)
        # 实时状态指示
        self.slave_status_labels = []
        for s in range(1, NUM_SLAVES + 1):
            dot = tk.Canvas(bar, width=8, height=8, bg=Theme.BG, highlightthickness=0)
            dot.pack(side=tk.LEFT, padx=(10, 2), pady=8)
            oid = dot.create_oval(0, 0, 8, 8, fill=Theme.TEXT_DIM, outline='')
            lbl = tk.Label(bar, text=f'M{s}', bg=Theme.BG, fg=Theme.TEXT_DIM, font=('', 9))
            lbl.pack(side=tk.LEFT)
            self.slave_status_labels.append((oid, dot))

    def _switch_slave(self, s):
        self.current_slave = s
        for i, btn in enumerate(self.slave_btns):
            st = 'SlaveTabSel.TButton' if (i + 1) == s else 'SlaveTab.TButton'
            btn.configure(style=st)
        self._update_display()
        if HAS_MATPLOTLIB:
            self._update_chart()

    # -------------------- 数据监控 Tab --------------------
    def _build_monitor_tab(self):
        tab = tk.Frame(self.notebook, bg=Theme.BG)
        self.notebook.add(tab, text='  数据监控  ')

        # --- 顶部：气压卡片 + 雨滴报警卡片 ---
        top_frame = tk.Frame(tab, bg=Theme.BG)
        top_frame.pack(fill=tk.X, padx=10, pady=(8, 3))
        p_card = tk.Frame(top_frame, bg=Theme.BG_CARD, width=200, height=70)
        p_card.pack(side=tk.LEFT, padx=5)
        p_card.pack_propagate(False)
        ttk.Label(p_card, text='气压压强', style='CardTitle.TLabel').pack(anchor='w', padx=10, pady=(6, 0))
        self.pressure_label = tk.Label(p_card, text='--', bg=Theme.BG_CARD,
                                        fg=Theme.ORANGE, font=('', 18, 'bold'))
        self.pressure_label.pack(anchor='w', padx=10)
        tk.Label(p_card, text='kPa', bg=Theme.BG_CARD,
                 fg=Theme.TEXT_DIM, font=('', 10)).pack(anchor='w', padx=10)

        r_card = tk.Frame(top_frame, bg=Theme.BG_CARD, width=200, height=70)
        r_card.pack(side=tk.LEFT, padx=5)
        r_card.pack_propagate(False)
        ttk.Label(r_card, text='雨滴报警', style='CardTitle.TLabel').pack(anchor='w', padx=10, pady=(6, 0))
        self.rain_label = tk.Label(r_card, text='未检测', bg=Theme.BG_CARD,
                                    fg=Theme.GREEN_DK, font=('', 18, 'bold'))
        self.rain_label.pack(anchor='w', padx=10)
        tk.Label(r_card, text='雨滴传感器', bg=Theme.BG_CARD,
                 fg=Theme.TEXT_DIM, font=('', 10)).pack(anchor='w', padx=10)

        # 当前主机组指示
        self.slave_indicator = tk.Label(top_frame, text='当前显示: M1（对应 S1）', bg=Theme.BG,
                                         fg=Theme.ACCENT, font=('', 14, 'bold'))
        self.slave_indicator.pack(side=tk.LEFT, padx=20)

        # --- 36 路温度表格 ---
        temp_frame = ttk.LabelFrame(tab, text=' 36路温度节点 (6 Port × 6 传感器) ',
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
        self.humi_labels = []  # 当前正式协议不传湿度，保留空集合兼容存量导出代码。
        for p in range(NUM_PORTS):
            tk.Label(grid_frame, text=f'端口{p+1}', bg=Theme.BG_CARD,
                     fg=Theme.PORT_COLORS[p], font=('', 9, 'bold')).grid(
                row=p+1, column=0, padx=2, pady=1, sticky='ew')
            for s in range(NUM_SENSORS):
                cell = tk.Frame(grid_frame, bg=Theme.BG_CARD)
                cell.grid(row=p+1, column=s+1, padx=2, pady=1, sticky='nsew')
                t_lbl = tk.Label(cell, text='--', bg=Theme.BG_CARD,
                                 fg=Theme.TEXT_HI, font=('', 11, 'bold'))
                t_lbl.pack(anchor='center')
                idx = p * NUM_SENSORS + s
                self.temp_labels.append((idx, t_lbl))

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
            type_combo['values'] = ['温度']
            type_combo.pack(side=tk.LEFT, padx=5)
            type_combo.bind('<<ComboboxSelected>>', lambda e: self._update_chart())
            ttk.Label(sel_frame, text=' 端口: ', style='Dim.TLabel').pack(side=tk.LEFT)
            self.chart_port_var = tk.StringVar(value='全部端口')
            port_combo = ttk.Combobox(sel_frame, textvariable=self.chart_port_var,
                                       width=10, state='readonly')
            port_combo['values'] = ['全部端口'] + [f'端口{p+1}' for p in range(NUM_PORTS)]
            port_combo.pack(side=tk.LEFT, padx=5)
            port_combo.bind('<<ComboboxSelected>>', lambda e: self._update_chart())

            self.fig = Figure(figsize=(10, 3), dpi=100, facecolor=Theme.CHART_BG)
            self.ax = self.fig.add_subplot(111)
            self._style_chart()
            self.canvas = FigureCanvasTkAgg(self.fig, chart_outer)
            self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
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

        # --- 主机控制: 4 行, 每行一条主机全部命令 ---
        host_frame = ttk.LabelFrame(tab, text=' 主机控制 (LoRa: 控制室 → 主机1~4) ',
                                    style='Section.TLabelframe')
        host_frame.pack(fill=tk.X, padx=10, pady=5)

        self.freq_vars = []
        self.temp_vars = []
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

            # 风机/自动/查询/请求温度
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
            for widget in sub.winfo_children():
                if isinstance(widget, ttk.Button):
                    self.host_control_buttons[g].append(widget)

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
        title = f'M{self.current_slave}（对应 S{self.current_slave}）- ' + ('温度趋势' if is_temp else '湿度趋势')
        self.ax.set_title(title, color=Theme.TEXT, fontsize=13)
        self.ax.set_xlabel('时间', color=Theme.TEXT_DIM, fontsize=11)
        self.ax.set_ylabel('℃' if is_temp else '%RH', color=Theme.TEXT_DIM, fontsize=11)
        self.ax.tick_params(colors=Theme.TEXT_DIM, labelsize=10)
        for spine in self.ax.spines.values():
            spine.set_color(Theme.CHART_GD)
        self.ax.grid(True, color=Theme.CHART_GD, linewidth=0.5)

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
        x = list(range(len(history)))

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
                        self.ax.plot(x, vals, color=base, linewidth=0.7,
                                     alpha=0.4 + 0.6 * (s + 1) / NUM_SENSORS,
                                     label=f'P{p+1}S{s+1}')
            handles, labels = self.ax.get_legend_handles_labels()
            seen = set()
            sh, sl = [], []
            for h, l in zip(handles, labels):
                pk = l[:2]
                if pk not in seen:
                    seen.add(pk)
                    sh.append(h)
                    sl.append(pk)
            if sh:
                self.ax.legend(sh, sl, loc='upper left', fontsize=8,
                               facecolor=Theme.BG_CARD, edgecolor=Theme.BORDER,
                               labelcolor=Theme.TEXT_DIM, ncol=6)
        else:
            p = int(port_sel.replace('端口', '')) - 1
            for s in range(NUM_SENSORS):
                idx = p * NUM_SENSORS + s
                vals = []
                for rec in history:
                    arr = rec.get(key, [])
                    vals.append(arr[idx] if idx < len(arr) and arr[idx] is not None else None)
                if any(v is not None for v in vals):
                    self.ax.plot(x, vals, color=Theme.PORT_COLORS[p],
                                 linewidth=1.5,
                                 alpha=0.4 + 0.6 * (s + 1) / NUM_SENSORS,
                                 label=f'传感器{s+1}')
            self.ax.legend(loc='upper left', fontsize=9,
                           facecolor=Theme.BG_CARD, edgecolor=Theme.BORDER,
                           labelcolor=Theme.TEXT_DIM)

        self.fig.tight_layout()
        self.canvas.draw()

    # -------------------- 数据处理 --------------------
    def _on_data_received(self, data):
        self.after(0, self._process_binary_frame, data)

    # -------------------- LoRa 二进制帧处理 --------------------
    # 控制室 USART1 原样转发主机响应帧；CRC 从版本字节起算且不包含 AA55。
    def _process_binary_frame(self, frame: bytes):
        parsed = LoRaProtocol.parse_packet(frame)
        hex_str = frame.hex(' ').upper()
        if not parsed:
            self._log(f'[CRC错误/格式错误] {hex_str}', 'recv')
            return
        if not LoRaProtocol.validate_control_room_response(parsed):
            self._log(f'[非控制室响应，已丢弃] {hex_str}', 'recv')
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
        elif msg_type == LoRaProtocol.MSG_ACK:
            self._handle_ack(parsed)
        elif msg_type == LoRaProtocol.MSG_RESULT:
            self._handle_result(parsed)
        elif msg_type == LoRaProtocol.MSG_ERROR:
            self._handle_error(parsed)

    def _handle_temp_36(self, parsed):
        """处理主机转发的完整 36 点温度帧。"""
        data = parsed['data']
        if len(data) != 72:
            self._log(f'[温度数据不足] 期望 72 字节, 实际 {len(data)} 字节')
            return
        temps = LoRaProtocol.decode_temperatures(data)
        # 主机转发，发送组号与同组从机编号一一对应。
        slave = parsed['sender_group']
        if not (1 <= slave <= NUM_SLAVES):
            self._log(f'[组号异常] sender_group={slave}, 无法关联从机')
            return
        record = {
            'timestamp': datetime.now().strftime('%H:%M:%S'),
            'slave': slave,
            'temperatures': temps,
            'humidities': [],          # LoRa 协议无湿度
            'pressure': None,          # LoRa 协议无气压
            'rain': None,              # LoRa 协议无雨滴
            'port': None,
            'num': None,
            'flow_id': parsed['flow_id'],
        }
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
        status_map = {0: '已接受并开始处理', 1: '重复命令，未重复执行', 2: '已拒绝'}
        st_str = status_map.get(status, hex(status) if status is not None else '?')
        self._show_response(
            f'ACK 流水号={parsed["flow_id"]} 状态={st_str} 原因={reason} | {data.hex(" ").upper()}')

    def _handle_result(self, parsed):
        # 数据区: 结果、模式、风机、频率、目标温度 (具体定义见设备侧)
        data = parsed['data']
        result = LoRaProtocol.decode_result(data)
        self._show_response(
            f'RESULT 流水号={parsed["flow_id"]} 结果={result["result_code"]}({result["result_name"]}) '
            f'模式={result["mode_name"]} 风机={result["fan_state_name"]} '
            f'频率={result["frequency_hz"]:.2f}Hz 目标温度={result["target_temperature_c"]:.1f}℃ '
            f'| {data.hex(" ").upper()}')
        self._complete_command(parsed['flow_id'])

    def _handle_error(self, parsed):
        # 数据区: 错误码、补充信息
        data = parsed['data']
        error = LoRaProtocol.decode_error(data)
        self._show_response(
            f'ERROR 流水号={parsed["flow_id"]} 错误码={error["code"]}({error["name"]}) '
            f'| {data.hex(" ").upper()}')
        self._complete_command(parsed['flow_id'])

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
        self.slave_indicator.config(text=f'当前显示: M{self.current_slave}（对应 S{self.current_slave}）')
        if not history:
            for _, lbl in self.temp_labels:
                lbl.config(text='--', fg=Theme.TEXT_DIM)
            for _, lbl in self.humi_labels:
                lbl.config(text='--', fg=Theme.TEXT_DIM)
            self.pressure_label.config(text='--')
            self.rain_label.config(text='未检测', fg=Theme.TEXT_DIM)
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

        p = data.get('pressure')
        if p is not None:
            self.pressure_label.config(text=f'{p:.1f}')
        else:
            self.pressure_label.config(text='--')

        rain = data.get('rain')
        if rain is not None:
            if rain == 1:
                self.rain_label.config(text='下雨报警!', fg=Theme.RED_DK)
            else:
                self.rain_label.config(text='未检测', fg=Theme.GREEN_DK)
        else:
            self.rain_label.config(text='未检测', fg=Theme.TEXT_DIM)

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
    def _set_host_controls_enabled(self, host_group, enabled):
        for button in self.host_control_buttons.get(host_group, []):
            button.configure(state='normal' if enabled else 'disabled')

    def _complete_command(self, flow_id):
        pending = self.pending_commands.pop(flow_id, None)
        if pending is not None:
            self._set_host_controls_enabled(pending['host_group'], True)

    def _send_lora_packet(self, packet: bytes, description: str, host_group=None, flow_id=None):
        """发送 LoRa 二进制报文并记录日志
        上位机与控制室 USART1 使用同一份正式二进制帧；控制室将主机命令转发到 LoRa。
        """
        if host_group is not None and any(item['host_group'] == host_group
                                          for item in self.pending_commands.values()):
            messagebox.showwarning('提示', f'主机{host_group} 有命令等待 RESULT 或 ERROR')
            return False
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
        if host_group is not None and flow_id is not None:
            self.pending_commands[flow_id] = {'host_group': host_group, 'description': description}
            self._set_host_controls_enabled(host_group, False)
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
            packet, f'SET_FREQ 主机{host_group} = {freq:.2f} Hz (流水号 {flow_id})', host_group, flow_id)

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
            packet, f'SET_TARGET_TEMP 主机{host_group} = {temp:.1f} ℃ (流水号 {flow_id})', host_group, flow_id)

    def _send_manual_run(self, host_group):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_manual_run(host_group, flow_id)
        self._send_lora_packet(
            packet, f'MANUAL_RUN 主机{host_group} (流水号 {flow_id})', host_group, flow_id)

    def _send_manual_stop(self, host_group):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_manual_stop(host_group, flow_id)
        self._send_lora_packet(
            packet, f'MANUAL_STOP 主机{host_group} (流水号 {flow_id})', host_group, flow_id)

    def _send_set_auto(self, host_group):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_set_auto(host_group, flow_id)
        self._send_lora_packet(
            packet, f'SET_AUTO 主机{host_group} (流水号 {flow_id})', host_group, flow_id)

    def _send_query_status(self, host_group):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_query_status(host_group, flow_id)
        self._send_lora_packet(
            packet, f'QUERY_STATUS 主机{host_group} (流水号 {flow_id})', host_group, flow_id)

    def _send_read_temp(self, host_group, force=False):
        flow_id = self.serial_mgr.next_flow_id()
        packet = LoRaProtocol.cmd_read_temp(host_group, flow_id, force_resample=force)
        mode = '强制重采样' if force else '允许缓存'
        self._send_lora_packet(
            packet, f'READ_TEMP 主机{host_group} ({mode}, 流水号 {flow_id})', host_group, flow_id)

    def _send_read_temp_all(self, force=False):
        """依次向所有主机请求温度 (可选, 控制室自动轮询)"""
        if not self.serial_mgr.is_connected:
            messagebox.showwarning('提示', '请先连接串口')
            return
        for g in range(1, NUM_HOSTS + 1):
            self._send_read_temp(g, force=force)

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
            messagebox.showwarning('提示', f'M{self.current_slave}（对应 S{self.current_slave}）暂无数据可导出')
            return
        default_name = f'M{self.current_slave}_S{self.current_slave}_' + datetime.now().strftime('%Y%m%d_%H%M%S') + '.csv'
        path = filedialog.asksaveasfilename(
            title=f'导出主机 M{self.current_slave} 温度 CSV', defaultextension='.csv',
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
                                f'已导出主机 M{self.current_slave} {count} 条数据到\n{path}')
            self._log(f'已导出主机 M{self.current_slave} {count} 条数据到 {path}')
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
        self.rain_label.config(text='未检测', fg=Theme.TEXT_DIM)
        self.log_text.config(state='disabled')
        self.rsp_text.config(state='disabled')
        self.cmd_log_text.config(state='disabled')
        if HAS_MATPLOTLIB:
            self._style_chart()
            self.canvas.draw()
        self._log('所有主机组数据已清除（数据库记录保留）')

    def _on_close(self):
        self.serial_mgr.disconnect()
        self.db.close()
        self.destroy()


if __name__ == '__main__':
    app = App()
    app.mainloop()
