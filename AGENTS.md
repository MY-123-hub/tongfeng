# LoRa ventilation firmware repository

## Layout

- `LoraMasterV1.1/`: master firmware: LoRa receive, DGUS display, and Modbus fan control.
- `LoraSlave/`: slave firmware: DS18B20 sampling and LoRa telemetry transmission.
- `protocol/`: shared protocol definitions. Update this first when either endpoint changes its data frame.
- `上位机/`: active PC application. Keep its Python source, executable release, and Python-only tests together here; do not mix STM32 firmware files into this directory.

## Developmen![[评委部署说明]]t rules

- Keep each firmware independently buildable from its own `MDK-ARM/*.uvprojx` project.
- Do not commit Keil generated output, per-user workspace settings, or CubeMX generated temporary files.
- Keep LoRa payloads bounded and validate received lengths, field counts, and array indexes.
- Changes spanning master and slave must update `protocol/LORA_TELEMETRY.md` and be checked on both devices.
