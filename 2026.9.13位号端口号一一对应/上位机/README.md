# 上位机

当前可用的智能通风系统上位机统一放在此目录。

## 目录约定

- `main.py`：唯一的上位机源码入口。
- `智能通风系统.exe`：由 `main.py` 打包生成的可执行文件。
- `tests/`：只存放上位机 Python 测试，不放 STM32 固件测试。

## 运行与验证

```powershell
python .\main.py
python .\tests\test_protocol.py
```

打包时从本目录执行：

```powershell
python -m PyInstaller --noconfirm --onefile --windowed --name 智能通风系统 --distpath . --workpath .\build --specpath .\build main.py
```
