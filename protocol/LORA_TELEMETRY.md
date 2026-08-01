# LoRa temperature telemetry

## Direction

`LoraSlave` sends one telemetry frame per channel to `LoraMasterV1.1` once per second. `PORT` identifies the channel and is in the range `00` to `05`.

## Current frame

```text
PORT:00,NUM:06,TM:23.5/24.0/0.0/0.0/0.0/0.0,DHT11_H:0.0,DHT11_T:0.0,Pressure:0.0
```

| Field | Meaning | Current requirement |
| --- | --- | --- |
| `PORT` | Channel index | Integer, `0..5` |
| `NUM` | Number of temperature slots | Integer, `1..6`; the slave currently always sends `06` |
| `TM` | DS18B20 temperatures | `NUM` decimal values separated by `/`; unit: °C |
| `DHT11_H` | Relative humidity | Decimal value; currently a placeholder `0.0` |
| `DHT11_T` | Ambient temperature | Decimal value; currently a placeholder `0.0` |
| `Pressure` | Pressure value | Decimal value; currently a placeholder `0.0` |

## Compatibility rules

- The slave must not emit more than six temperature values.
- The master must reject malformed frames rather than use partial fields.
- Decimal fields use `.` and no units in the payload.
- A channel with fewer physical sensors currently pads missing slots with `0.0`. Before using temperature `0.0` as an alarm/control input, define a separate validity flag or send the actual sensor count in `NUM`.
