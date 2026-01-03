[![Build](https://github.com/ahpohl/smartmeter-gateway/actions/workflows/build.yml/badge.svg)](https://github.com/ahpohl/smartmeter-gateway/actions/workflows/build.yml)

# smartmeter-gateway

smartmeter-gateway is a lightweight service that reads operational data from an eBZ / Easymeter smart meter and publishes it to MQTT as JSON and optionally provides a Modbus slave (TCP/RTU).

## Background

This project reads data telegrams using the meter’s infrared (IR) optical port on top of the meter. The meter provides telegrams using standardized **OBIS** identifiers (e.g. energy counter, power, per-phase values). See the [reference documentation](docs/ebz_manual.pdf) for details.

The initial prototype used an Arduino and a simple breadboard circuit with a [photo transistor circuit](https://github.com/ahpohl/smartmeter/wiki/Arduino-breadboard). For a robust permanent installation the breadboard circuit was replaced with an [IR dongle](https://github.com/ahpohl/smartmeter/wiki/IR-dongle-pcb) built on a PCB (build process documented on the wiki): 

![eBZ smart meter with IR dongle](docs/ebz_with_dongle.png)

## Features

- Reads and parses OBIS telegrams from the serial port.
- Publishes values, device info and connection availability as JSON messages to an MQTT broker 
- Fully configurable through a YAML configuration file
- Extensive, module-scoped logging
- Optional Modbus support for integrations that expect a register model similar to a Fronius smart meter
  - Supports both integer + scale factor and float registers
  - Modbus over TCP (IPv4/IPv6) and serial RTU
- Support for dropping user privileges - useful for hardened deployments and containers

## Status and limitations

- A PostgreSQL consumer is planned but not implemented yet.
- TLS in libmosquitto not yet supported. 

## Dependencies

- [libmosquitto](https://mosquitto.org/) — MQTT client library
- [yaml-cpp](https://github.com/jbeder/yaml-cpp) — YAML configuration parsing
- [spdlog](https://github.com/gabime/spdlog) — Structured logging
- [libmodbus](https://libmodbus.org/) — Communicate with Modbus devices

Ensure the development headers for the above libraries are installed on your system.

## Configuration

smartmeter-gateway is configured via a YAML file. Below is a complete example followed by a field-by-field reference. 

### Example config

```yaml
meter:
  device: /dev/ttyUSB0
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
    device: /dev/ttyUSB1
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
  broker: localhost
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

### Configuration reference

- meter
  - device: Serial device path of the IR head
  - preset: Serial preset for the meter interface
     - od_type: Optical device preset for the IR port on the meter, serial settings: 9600 baud, 7 data bits, even parity, 1 stop bit (9600 7E1)
    - sd_type: Standard device multi functional preset, serial settings: 9600 baud, 8 data bits, no parity, 1 stop bit (9600 8N1)
  - Note: you can override the preset by specifying any of the individual parameters below. If no preset is given, then all of the individual parameters must be provided
    - baud: Baud rate (e.g., 9600, 19200, 38400).
    - data_bits: Data bits (e.g., 5,6,7,8)
    - stop_bits: Stop bits (e.g., 1 or 2)
    - parity: Parity, allowed values are none, even, odd
  - grid (optional)
    - power_factor: Assumed PF used to derive apparent/reactive power (default 0.95)
    - frequency: Assumed mains frequency (required for simulation of a Fronius meter, default 50 Hz)

- modbus
  - Note: Configure at least one transport (tcp or rtu). If both are configured, TCP takes precedence over RTU
  - tcp
    - listen: Bind address for Modbus TCP slave (IPv4 or IPv6), e.g. 0.0.0.0 or ::
    - port: TCP port for Modbus (default is usually 502); ports <1024 may require root/capabilities (see --user/--group)
  - rtu
    - device: Serial device path (e.g. /dev/ttyUSB1)
    - preset: Serial preset for RTU line settings (od_type or sd_type), see meter config
  - slave_id: Modbus unit/slave ID (typically 1)
  - request_timeout: timeout between requests (indications) from the master (seconds)
  - idle_timeout: Idle timeout until forceful client disconnect (seconds)
  - use_float_model
      - true: exposes values using float registers
      - false: uses integer + scale factor registers

- mqtt
  - broker: Hostname or IP of the MQTT broker. 
  - port: MQTT broker port (1883 for unencrypted, 8883 for TLS, if supported by your setup).
  - topic: Base MQTT topic to publish under (e.g., fronius-bridge). Subtopics may be used for values/events/device/availability info.
  - user: Optional username for broker authentication.
  - password: Optional password for broker authentication.
  - queue_size: Size of the internal publish queue. Increase if bursts of data may outpace network/broker temporarily.
  - reconnect_delay
    - min: Initial delay (seconds) before reconnecting to MQTT after a failure.
    - max: Maximum delay (seconds) between reconnect attempts. 
    - exponential: If true, uses exponential backoff between min and max; if false, uses a fixed delay. 

- logger
  - level: Global default log level. Accepted values: off, error, warn, info, debug, trace.
  - modules: Per-module overrides for log levels.
    - main: Log level for the main module
    - meter: Log level for meter parsing/IO
    - mqtt: Log level for MQTT client interactions
    - modbus: Log level for Modbus
  Notes:
  - A module's level overrides the global level for that module.
  - Use debug/trace when troubleshooting connectivity or protocol issues.


## MQTT publishing

- Messages are published as JSON strings under the configured base topic.
- Subtopics include values (telemetry), device info (static metadata), and availability (connection state).
- Consumers should handle retained/non-retained semantics as configured by your deployment (and broker defaults).

### Topics and example payloads

- Topic: smartmeter-gateway/values
  ```json
  {
    "time": 1767449059987,
    "energy": 22557.3,
    "power_active": 361,
    "power_apparent": 380,
    "power_reactive": 119,
    "power_factor": 0.95,
    "phases": [
      {
        "id": 1,
        "power_active": 122,
        "power_apparent": 128,
        "power_reactive": 40,
        "power_factor": 0.95,
        "voltage_ph": 234,
        "voltage_pp": 404.2,
        "current": 0.547
      },
      {
        "id": 2,
        "power_active": 65,
        "power_apparent": 69,
        "power_reactive": 21,
        "power_factor": 0.95,
        "voltage_ph": 232.7,
        "voltage_pp": 404.3,
        "current": 0.295
      },
      {
        "id": 3,
        "power_active": 174,
        "power_apparent": 184,
        "power_reactive": 57,
        "power_factor": 0.95,
        "voltage_ph": 234.2,
        "voltage_pp": 405.5,
        "current": 0.784
      }
    ],
    "active_time": 188181675,
    "frequency": 50,
    "voltage_ph": 233.6,
    "voltage_pp": 404.7
  }
  ```

- Topic: smartmeter-gateway/device
  ```jsonc
  {
    "firmware_version": "107",
    "manufacturer": "EasyMeter",
    "model": "DD3-BZ06-ETA-ODZ1",
    "options": "1.0.0-3d376a0",
    "phases": 3,
    "serial_number": "1EBZ0100507409",
    "status": "001C0104"
  }
  ```

- Topic: smartmeter-gateway/availability
  ```
  connected
  ```
  or
  ```
  disconnected
  ```

### Field reference

| Field | Description | Units | OBIS | Notes |
|---|---|---:|---|---|
| time | Timestamp (Unix epoch) | ms | — | UTC milliseconds since epoch |
| energy | Cumulative imported energy | kWh | `1-0:1.8.0*255` |  |
| power_active | Total active power (all phases) | W | `1-0:16.7.0*255` |  |
| power_apparent | Total apparent power | VA | — | Derived from `power_active / power_factor` |
| power_reactive | Total reactive power | var | — | Derived from `tan(acos(power_factor)) * power_active` |
| power_factor | Power factor | — | — | Assumed; default 0.95 (config: `meter.grid.power_factor`) |
| frequency | Mains frequency | Hz | — | Assumed; default 50.0 (config: `meter.grid.frequency`) |
| voltage_ph | Average phase-to-neutral voltage | V | — | Derived (mean of `phases[].voltage_ph`) |
| voltage_pp | Average phase-to-phase voltage | V | — | Derived (mean of `phases[].voltage_pp`) |
| active_time | Meter active/sensor time | s | `0-0:96.8.0*255` | Parsed as **hex** |
| phases[].id | Phase index | — | — | 1..3 |
| phases[].power_active | Per-phase active power | W | `1-0:36.7.0*255` (L1), `1-0:56.7.0*255` (L2), `1-0:76.7.0*255` (L3) |  |
| phases[].power_apparent | Per-phase apparent power | VA | — | Derived |
| phases[].power_reactive | Per-phase reactive power | var | — | Derived |
| phases[].power_factor | Per-phase power factor | — | — | Assumed (same as top-level `power_factor`) |
| phases[].voltage_ph | Per-phase phase-to-neutral voltage | V | `1-0:32.7.0*255` (L1), `1-0:52.7.0*255` (L2), `1-0:72.7.0*255` (L3) | Rounded to 1 decimal |
| phases[].voltage_pp | Per-phase phase-to-phase voltage | V | — | Derived |
| phases[].current | Per-phase current | A | — | Derived from `power_active / (voltage_ph * power_factor)`, rounded to 3 decimals |
| manufacturer | Meter manufacturer | — | — | Currently hardcoded to `EasyMeter` |
| model | Meter model | — | — | Currently hardcoded to `DD3-BZ06-ETA-ODZ1` |
| serial_number | Meter serial / device ID | — | `1-0:96.1.0*255` |  |
| firmware_version | Meter firmware version | — | — | Parsed from the leading version line |
| status | Meter status word | — | `1-0:96.5.0*255` | hex string |
| phases | Number of phases | — | — | currently hardcoded to `3` |
| options | Gateway build/version info | — | — |  |
| availability | Connection state | — | — |  `connected` or `disconnected`; published on connect/disconnect/validation failure |

### Power factor

`power_factor` is published as a signed value with a range of **-1.0 .. 1.0**.

A typical interpretation is:
- **positive** values: lagging (inductive) load
- **negative** values: leading (capacitive) / feed-in

The smart meter does **not** provide a measured power factor in the OBIS telegrams used here. Therefore the gateway **assumes** a default value of 0.95 (configurable via `meter.grid.power_factor`), which is a reasonable approximation for a modern household with inverter-driven appliances.

This assumed value is used to derive:
- apparent power
- reactive power
- current

### MQTT publish defaults

- QoS: 1
- Retained: true
- Duplicate suppression: the publisher suppresses consecutive duplicates per topic (hash comparison of payload).
- Queueing: messages are queued per topic up to mqtt.queue_size and published when connected; reconnect uses exponential backoff as configured.
- Consumers should be prepared to receive retained messages on subscribe and handle at-least-once delivery semantics.

## Troubleshooting

- Master (client) connection timeouts: 
  - Increase modbus.response_timeout
  - Verify slave_id and transport (TCP vs RTU) match your setup.
- Frequent reconnects:
  - Check broker reachability and credentials.
  - Adjust mqtt.reconnect_delay and modbus.reconnect_delay backoff ranges.
- Permission denied opening the serial device
  - Inspect permissions
  - Add the runtime user to the appropriate group

## Security considerations

- Drop privileges after startup
  - If you need to bind to privileged ports (e.g. Modbus TCP on port 502) you may have to start as root
  - Use `--user` and `--group` so the process drops to an unprivileged account after initialization
  - Recommendation: create a dedicated service user (e.g. `meter`) with the minimum required permissions
  - If you don’t want to run as root at all, consider:
    - using a non-privileged Modbus port (e.g. 1502), or
    - granting only the required capability to bind low ports (e.g. `cap_net_bind_service`) instead of full root.

- Prefer running MQTT behind a trusted network or VPN
  - The published data can reveal detailed consumption patterns. Avoid exposing the broker directly to the internet.
  - If remote access is required, put the broker behind WireGuard/OpenVPN, or use a private broker reachable only via a VPN.

- If using authentication, set `mqtt.user` and `mqtt.password` and protect the config file
  - Store the config with restrictive permissions (e.g. readable only by the service user).
  - Avoid embedding credentials in container images; pass the config via a mounted volume or secrets mechanism. 

## License

[MIT](LICENSE)

---

*fronius-bridge* is not affiliated with or endorsed by Fronius International GmbH. 
