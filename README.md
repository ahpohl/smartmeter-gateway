[![Build](https://github.com/ahpohl/smartmeter-gateway/actions/workflows/build.yml/badge.svg)](https://github.com/ahpohl/smartmeter-gateway/actions/workflows/build.yml)

# smartmeter-gateway

smartmeter-gateway is a lightweight service that reads operational data from an eBZ / Easymeter smart meter and publishes it to MQTT as JSON and optionally provides a Modbus slave (TCP/RTU).

## Background

This project reads data telegrams using the meter’s infrared (IR) optical port on top of the meter. The meter provides telegrams using standardized OBIS identifiers (e.g. energy counter, power, per-phase values). See the [reference documentation](docs/ebz_manual.pdf) for details.

The initial prototype used an Arduino and a simple breadboard circuit with a [photo transistor circuit](https://github.com/ahpohl/smartmeter/wiki/Arduino-breadboard). For a robust permanent installation the breadboard circuit was replaced with an [IR dongle](https://github.com/ahpohl/smartmeter/wiki/IR-dongle-pcb) built on a PCB (build process documented on the wiki) 

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
     - od_type: Optical device preset for the IR port on the meter, 9600 baud, 7 data bits, even parity, 1 stop bit (9600 7E1)
    - sd_type: Standard device, multi functional preset, 9600 baud, 8 data bits, no parity, 1 stop bit (9600 8N1)
  - Note: you can override the preset by specifying any of the individual parameters below. If no preset is given, then all of the individual parameters must be provided
    - baud: Baud rate (e.g. 9600, 19200, 38400).
    - data_bits: Data bits (5,6,7,8)
    - stop_bits: Stop bits (1,2)
    - parity: Parity, allowed values are none, even, odd
  - grid (optional)
    - power_factor: Assumed PF used to derive apparent/reactive power (default 0.95)
    - frequency: Assumed mains frequency (required for simulation of a Fronius meter, default 50 Hz)

- modbus
  - Note: Configure at least one transport (tcp or rtu). If both are configured, TCP takes precedence over RTU
  - tcp
    - listen: Bind address for Modbus TCP slave (IPv4 or IPv6), e.g. 0.0.0.0 or ::
    - port: TCP port for Modbus (default 502)
  - rtu
    - device: Serial device path (e.g. /dev/ttyUSB1)
    - preset: Serial preset for RTU line settings (od_type or sd_type), see meter config
  - slave_id: Modbus unit/slave ID (typically 1)
  - request_timeout: timeout between requests (indications) from the master (seconds)
  - idle_timeout: disconnect client if no activity (seconds)
  - use_float_model
      - true: exposes values using float registers
      - false: uses integer + scale factor registers

- mqtt
  - broker: Hostname or IP of the MQTT broker. 
  - port: MQTT broker port (1883 for unencrypted, 8883 for TLS, if supported by your setup).
  - topic: Base MQTT topic to publish under (e.g., smartmeter-gateway). Subtopics may be used for values/device/availability info.
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
| energy | Cumulative imported energy | kWh | 1-0:1.8.0\*255 | — |
| power_active | Total active power (all phases) | W | 1-0:16.7.0\*255 | — |
| power_apparent | Total apparent power | VA | — | Derived |
| power_reactive | Total reactive power | var | — | Derived |
| power_factor | Power factor | — | — | Assumed |
| frequency | Mains frequency | Hz | — | Assumed |
| voltage_ph | Average phase-to-neutral voltage | V | — | Derived |
| voltage_pp | Average phase-to-phase voltage | V | — | Derived |
| active_time | Meter active/sensor time | s | 0-0:96.8.0\*255 | — |
| phases[].id | Phase index | — | — | 1..3 |
| phases[].power_active | Per-phase active power | W | 1-0:36.7.0\*255 (L1) 1-0:56.7.0\*255 (L2) 1-0:76.7.0\*255 (L3) | — |
| phases[].power_apparent | Per-phase apparent power | VA | — | Derived |
| phases[].power_reactive | Per-phase reactive power | var | — | Derived |
| phases[].power_factor | Per-phase power factor | — | — | Assumed (same as top-level) |
| phases[].voltage_ph | Per-phase phase-to-neutral voltage | V | 1-0:32.7.0\*255 (L1) 1-0:52.7.0\*255 (L2) 1-0:72.7.0\*255 (L3) | — |
| phases[].voltage_pp | Per-phase phase-to-phase voltage | V | — | Derived |
| phases[].current | Per-phase current | A | — | Derived |
| manufacturer | Meter manufacturer | — | — | Currently hardcoded to "EasyMeter" |
| model | Meter model | — | — | Currently hardcoded to "DD3-BZ06-ETA-ODZ1" |
| serial_number | Meter serial / device ID | — | 1-0:96.1.0\*255 | — |
| firmware_version | Meter firmware version | — | — | Parsed from the leading version line "/" |
| status | Meter status word | — | 1-0:96.5.0\255 | hex string |
| phases | Number of phases | — | — | currently hardcoded to "3" |
| options | Gateway build/version info | — | — | — |
| availability | Connection state | — | — | "connected" or "disconnected"; published on connect/disconnect/validation failure |

### Derived quantities

#### Apparent power, reactive power, current

The smart meter telegram provides active power (<code>P</code>, in W). Because the meter does not provide a measured power factor, the gateway uses an assumed power factor with the following convention:
- <code>pf &gt; 0</code>: lagging (inductive) ⇒ <code>Q &gt; 0</code>
- <code>pf &lt; 0</code>: leading (capacitive / feed-in) ⇒ <code>Q &lt; 0</code>

The gateway additionally derives:

1) Apparent power <code>S</code> (in VA): <code>S = |P| / |pf|</code>
2) Reactive power <code>Q</code> (in var): <code>|Q| = |P| * tan(acos(|pf|))</code>
3) Per-phase currents (in A):

- <code>I<sub>1</sub> = P<sub>1</sub> / (V<sub>1</sub> · pf)</code>
- <code>I<sub>2</sub> = P<sub>2</sub> / (V<sub>2</sub> · pf)</code>
- <code>I<sub>3</sub> = P<sub>3</sub> / (V<sub>3</sub> · pf)</code>

4) Total current (in A):
- <code>current = I<sub>1</sub> + I<sub>2</sub> + I<sub>3</sub></code>

#### Phase-to-phase voltage

The gateway derives phase-to-phase (line-to-line) voltages from the measured phase-to-neutral voltages using the magnitude of the phasor difference of two phase voltages with a 120° phase shift.
- <code>V<sub>12</sub> = sqrt(V<sub>1</sub><sup>2</sup> + V<sub>2</sub><sup>2</sup> + V<sub>1</sub>·V<sub>2</sub>)</code>
- <code>V<sub>23</sub> = sqrt(V<sub>2</sub><sup>2</sup> + V<sub>3</sub><sup>2</sup> + V<sub>2</sub>·V<sub>3</sub>)</code>
- <code>V<sub>31</sub> = sqrt(V<sub>3</sub><sup>2</sup> + V<sub>1</sub><sup>2</sup> + V<sub>3</sub>·V<sub>1</sub>)</code>

And the published aggregate <code>voltage_pp</code> is the mean of these three values:
- <code>voltage_pp = (V<sub>12</sub> + V<sub>23</sub> + V<sub>31</sub>) / 3</code>

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
  - Adjust `mqtt.reconnect_delay` and `modbus.reconnect_delay` backoff ranges.
- Permission denied opening the serial device
  - Inspect device permissions (e.g. `ls -la`)
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
- If using authentication, set `mqtt.user` and `mqtt.password` and protect the config file

## License

[MIT](LICENSE)
