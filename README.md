# smartmeter-gateway

Read OBIS telegrams from an eBZ / Easymeter smart meter via the optical IR port and expose the decoded data to other systems via **MQTT** and/or an optional **Modbus slave** (TCP/RTU).

## Background

This project reads data telegrams from an eBZ / Easymeter smart meter using the meter’s infrared (IR) optical port on top of the meter. The meter provides telegrams using standardized **OBIS** identifiers (e.g. energy counter, power, per-phase values).

Reference documentation:

- [`docs/ebz_manual.pdf`](docs/ebz_manual.pdf)

### Hardware history

The initial prototype used an Arduino and a simple breadboard circuit:

- [photo transistor circuit](https://github.com/ahpohl/smartmeter/wiki/Arduino-breadboard)

For a robust permanent installation the breadboard circuit was replaced with an IR dongle built on a PCB (build process documented on the wiki):

- [IR dongle](https://github.com/ahpohl/smartmeter/wiki/IR-dongle-pcb)

Picture (meter with dongle installed):

![eBZ smart meter with IR dongle](docs/ebz_with_dongle.png)

### Processing the data (MQTT → storage → dashboards)

The gateway itself focuses on reliably reading and publishing smart meter data. In most setups you will want to process the MQTT messages further, for example:

- forward live readings into a database (e.g. PostgreSQL)
- aggregate data for long-term retention
- build dashboards for current power, daily/monthly consumption, cost estimates, etc.

A typical pipeline looks like this:

1. **smartmeter-gateway** publishes JSON to MQTT (`<mqtt.topic>/values`, `<mqtt.topic>/device`, `<mqtt.topic>/availability`)
2. **Node-RED** subscribes to the MQTT topics and transforms/validates the payload
3. Node-RED writes the resulting rows into **PostgreSQL**
4. **Grafana** reads from PostgreSQL and renders dashboards

The recommended PostgreSQL/Grafana setup (including schema, retention/aggregation ideas, and dashboard concepts) is documented on the project wiki:

- https://github.com/ahpohl/smartmeter-gateway/wiki

## Features

The gateway uses a producer/consumer architecture:

- Producer reads telegrams from the meter (serial device provided by the IR dongle)
- Consumers
  - MQTT publisher (default)
  - Modbus slave (optional)

### Producer

- Reads and parses OBIS telegrams from the serial port.
- Produces:
  - *values* (measurements like energy/power/voltages)
  - *device* metadata (serial, ids, etc.)
  - *availability* state (connected/disconnected style status)

### MQTT consumer

- Publishes JSON payloads to an MQTT broker.
- Publishes to three topics derived from the configured base topic `mqtt.topic`:

| Suffix | Topic | Purpose |
|---|---|---|
| `/values` | `<mqtt.topic>/values` | Live measurement values (JSON) |
| `/device` | `<mqtt.topic>/device` | Device metadata (JSON) |
| `/availability` | `<mqtt.topic>/availability` | Availability state (string/JSON) |

### Modbus (optional)

Optional Modbus support is intended for integrations that expect a register model similar to a Fronius smart meter (or similar register map):

- Supports both:
  - integer + scale factor registers
  - float registers (`use_float_model`)
- Supports:
  - Modbus TCP (bind address configurable; IPv4 or `::` for IPv6-any)
  - Modbus RTU

## Status and limitations

- PostgreSQL consumer is planned but not implemented
- TLS for MQTT (mosquitto TLS) is not yet supported (see Security considerations)

## Dependencies

- [libmodbus](https://libmodbus.org/) — Communicate with Modbus devices
- [libmosquitto](https://mosquitto.org/) — MQTT client library
- [yaml-cpp](https://github.com/jbeder/yaml-cpp) — YAML configuration parsing
- [spdlog](https://github.com/gabime/spdlog) — Structured logging

Ensure the development headers for the above libraries are installed on your system.

## Configuration (`config.yaml`)

Configuration is YAML-based. A full example is provided in:

- [`config.yaml`](config.yaml)

### Example configuration

```yaml
meter:
  device: /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_AB65FI6I-if00-port0
  preset: od_type
  #baud: 9600
  #data_bits: 7
  #stop_bits: 1
  #parity: even   # none | even | odd
  grid:
    power_factor: 0.95
    frequency: 50.00

modbus:
  tcp:
    listen: 0.0.0.0
    port: 502
  rtu:
    device: /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_AC009Y6C-if00-port0
    preset: sd_type
    #baud: 9600
    #data_bits: 8
    #stop_bits: 1
    #parity: none   # none | even | odd
  slave_id: 1
  request_timeout: 5
  idle_timeout: 60
  use_float_model: false

mqtt:
  broker: geapoh.home.arpa
  port: 1883
  topic: smartmeter-gateway
  #user: mqtt
  #password: "your secret password"
  queue_size: 100
  reconnect_delay:
    min: 2
    max: 64
    exponential: true

logger:
  level: info     # global default: off | error | warn | info | debug | trace
  modules:
    main: info
    meter: info
    modbus: info
    mqtt: info
```

### Parameter reference

#### `meter.*`

| Key | Type | Required | Example | Description |
|---|---|---:|---|---|
| `meter.device` | string | yes | `/dev/serial/by-id/...` | Serial device for the IR dongle / optical head. Prefer stable `/dev/serial/by-id/...` symlinks. |
| `meter.preset` | string | yes | `od_type` | Convenience preset for serial line settings matching a meter type. |
| `meter.baud` | int | no | `9600` | Override baud rate. |
| `meter.data_bits` | int | no | `7` | Override data bits. |
| `meter.stop_bits` | int | no | `1` | Override stop bits. |
| `meter.parity` | string | no | `even` | Override parity: `none`, `even`, `odd`. |
| `meter.grid.power_factor` | float | no | `0.95` | Assumed power factor used for derived values (if applicable). |
| `meter.grid.frequency` | float | no | `50.0` | Assumed grid frequency used for derived values (if applicable). |

#### `modbus.*`

| Key | Type | Required | Example | Description |
|---|---|---:|---|---|
| `modbus.tcp.listen` | string | no | `0.0.0.0` | Address to bind Modbus TCP server to. Use `::` for IPv6-any. |
| `modbus.tcp.port` | int | no | `502` | Modbus TCP port. Ports <1024 may require root/capabilities. |
| `modbus.rtu.device` | string | no | `/dev/serial/by-id/...` | Serial device for Modbus RTU. |
| `modbus.rtu.preset` | string | no | `sd_type` | Convenience preset for RTU serial settings. |
| `modbus.rtu.baud` | int | no | `9600` | RTU baud override. |
| `modbus.rtu.data_bits` | int | no | `8` | RTU data bits override. |
| `modbus.rtu.stop_bits` | int | no | `1` | RTU stop bits override. |
| `modbus.rtu.parity` | string | no | `none` | RTU parity override: `none`, `even`, `odd`. |
| `modbus.slave_id` | int | no | `1` | Modbus unit identifier. |
| `modbus.request_timeout` | int | no | `5` | Request timeout in seconds. |
| `modbus.idle_timeout` | int | no | `60` | Idle timeout in seconds. |
| `modbus.use_float_model` | bool | no | `false` | If `true`, expose values as floats instead of integer+scale model. |

#### `mqtt.*`

| Key | Type | Required | Example | Description |
|---|---|---:|---|---|
| `mqtt.broker` | string | yes (if MQTT used) | `localhost` | MQTT broker hostname/IP. |
| `mqtt.port` | int | no | `1883` | MQTT broker port. |
| `mqtt.topic` | string | yes (if MQTT used) | `smartmeter-gateway` | Base topic. The gateway publishes to `<topic>/values`, `<topic>/device`, `<topic>/availability`. |
| `mqtt.user` | string | no | `mqtt` | Username if broker requires authentication. |
| `mqtt.password` | string | no | `your secret password` | Password (quote if it contains special characters). |
| `mqtt.queue_size` | int | no | `100` | Internal outgoing publish queue size. |
| `mqtt.reconnect_delay.min` | int | no | `2` | Minimum reconnect delay in seconds. |
| `mqtt.reconnect_delay.max` | int | no | `64` | Maximum reconnect delay in seconds. |
| `mqtt.reconnect_delay.exponential` | bool | no | `true` | Use exponential backoff for reconnect delay. |

#### `logger.*`

| Key | Type | Required | Example | Description |
|---|---|---:|---|---|
| `logger.level` | string | no | `info` | Global log level: `off`, `error`, `warn`, `info`, `debug`, `trace`. |
| `logger.modules.*` | string | no | `info` | Per-module log override (e.g. `main`, `meter`, `modbus`, `mqtt`). |

## MQTT publishing

### Topics

Published topics are:

- `.../values` contains measurement values (energy/power/voltages etc.)
- `.../device` contains device identifiers/metadata
- `.../availability` contains availability information

### Payload reference (exact fields from code)

The JSON is created in `src/meter.cpp` and then published unchanged by `src/main.cpp` (the payload is the `jsonDump` string).

#### `<mqtt.topic>/values`

Top-level fields:

| Field | Type | Example | Notes |
|---|---|---|---|
| `time` | integer | `1730000000000` | Milliseconds since epoch. |
| `energy` | number | `17894.0` | Rounded to 1 decimal place. Parsed from OBIS `1-0:1.8.0*255`. |
| `power_active` | number | `228` | Rounded to 0 decimals. Parsed from OBIS `1-0:16.7.0*255`. |
| `power_apparent` | number | `240` | Derived from `power_active / power_factor`. |
| `power_reactive` | number | `75` | Derived using `tan(acos(power_factor)) * power_active`. |
| `power_factor` | number | `0.95` | From `meter.grid.power_factor` (defaults to 0.95). Rounded to 2 decimals. |
| `frequency` | number | `50.00` | From `meter.grid.frequency` (defaults to 50.0). Rounded to 2 decimals. |
| `voltage_ph` | number | `232.4` | Average phase-to-neutral voltage across phases. Rounded to 1 decimal. |
| `voltage_pp` | number | `400.3` | Average phase-to-phase voltage across phases. Rounded to 1 decimal. |
| `active_time` | integer | `150705410` | “Active sensor time” parsed from OBIS `0-0:96.8.0*255` as hex. |
| `phases` | array | `[...]` | Array with per-phase objects (see below). |

Per-phase objects inside `phases[]`:

Each entry contains:

| Field | Type | Notes |
|---|---|---|
| `id` | integer | 1, 2, 3 |
| `power_active` | number | Per-phase active power (rounded 0). Parsed from `1-0:36.7.0*255`, `1-0:56.7.0*255`, `1-0:76.7.0*255`. |
| `power_apparent` | number | Derived (rounded 0). |
| `power_reactive` | number | Derived (rounded 0). |
| `power_factor` | number | Same PF as global (rounded 2). |
| `voltage_ph` | number | Parsed from `1-0:32.7.0*255`, `1-0:52.7.0*255`, `1-0:72.7.0*255` (rounded 1). |
| `voltage_pp` | number | Derived line-to-line voltage for that phase (rounded 1). |
| `current` | number | Derived from `power_active / (voltage_ph * power_factor)` (rounded 3). |

#### `<mqtt.topic>/device`

Fields:

| Field | Type | Notes |
|---|---|---|
| `manufacturer` | string | Hardcoded: `EasyMeter` |
| `model` | string | Hardcoded: `DD3-BZ06-ETA-ODZ1` |
| `serial_number` | string | Parsed from OBIS `1-0:96.1.0*255` |
| `firmware_version` | string | Parsed from the leading `/..._...` telegram line |
| `options` | string | Built as `<PROJECT_VERSION>-<GIT_COMMIT_HASH>` |
| `phases` | integer | Hardcoded: `3` |
| `status` | string | Parsed from OBIS `1-0:96.5.0*255` |

#### `<mqtt.topic>/availability`

- On connect: payload `"connected"`  
- On disconnect: payload `"disconnected"`  

## Troubleshooting

### Permission denied opening the serial device

If the gateway cannot open `meter.device` (or Modbus RTU device):

1. Inspect permissions:

```sh
ls -l /dev/serial/by-id/
```

2. Add the runtime user to the appropriate group (commonly `dialout` on Debian/Ubuntu, `uucp` on Arch):

```sh
sudo usermod -aG dialout <username>
# log out / log in again, or restart the service/session
```

3. If running as a service/container, ensure:
- the device is passed through to the container
- the service user has group membership / udev rules

---

## Security considerations

Smart meter telemetry can reveal sensitive consumption patterns.

- If MQTT traffic crosses the public internet, use a VPN (WireGuard/OpenVPN) and keep the broker private.
- Restrict broker access (firewall + authentication).
- Since TLS is not yet supported in the current implementation, treat MQTT transport as LAN/VPN-only for now.

## License

[MIT](LICENSE)