# 主机协议测试

`host/` 保存可以在 Windows 主机上编译运行的纯 C 单元测试。测试程序不得依赖 STM32 HAL、FreeRTOS 或硬件串口。

测试生成的 `.exe`、覆盖率文件和临时日志不放在本目录，统一输出到系统临时目录或项目现有 `tmp/`，避免混入固件源码。

运行全部主机测试：

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\host\run_tests.ps1
```

脚本会在 `-O0` 和 `-O2` 下分别启用严格警告并运行全部测试。
