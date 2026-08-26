
# CAN Wizard

CAN Wizard is a project designed for communication using the CAN (Controller Area Network) protocol, primarily targeted at embedded systems utilizing the ESP32 microcontroller family. It is developed with ESP-IDF; the ESP32-C3 is the primary target, but any chip with a TWAI controller should work by changing the target and the GPIO/console settings in `menuconfig`.

Here are more information: 
- [Хабр Article](https://habr.com/ru/articles/793326/). (in Russian)
- [Same article in English](https://okhsunrog.ru/articles/2024/02/15/can_bus_sniffer/).

## Features

- **CAN sniffer and sender**: receives frames at any supported bit rate (up to 1 Mbit/s) and prints them live; sends data and remote frames.
- **Filtering**: plain hardware acceptance filters, or "smart" filters that combine the hardware filter with software matching so several ID ranges can be watched at once.
- **Bus monitoring**: live prompt showing the controller state and error counters, bus-off detection with optional automatic recovery, statistics.
- **Interactive console**: line editing, history (persisted in LittleFS), tab completion and hints. Received frames and log messages are printed without corrupting the line being edited.

## Requirements

- **Hardware**: 
  - ESP32-C3 microcontroller
  - SN65HVD230 CAN transceiver

- **Software**:
  - ESP-IDF 6.0 or newer (last tested with v6.0.2; note that the project uses the legacy `driver/twai.h` API, which is deprecated in 6.x but still functional)
  - CMake (for project build system)
  - Python (for ESP-IDF and related tools)

## Setup and Installation

1. **Clone the Repository**:
   Clone this repository using:
   ```bash
   git clone --recursive git@github.com:okhsunrog/can_wizard.git
   ```

2. **Install ESP-IDF**:
   Follow the official ESP-IDF installation guide for your operating system: [ESP-IDF Setup Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)

3. **Configure ESP-IDF**:
   Set up your environment by running the following commands:
   ```bash
   cd <project-directory>
   idf.py set-target esp32c3
   idf.py menuconfig
   ```

4. **Build and Flash**:
   Build the project and flash it to your microcontroller:
   ```bash
   idf.py build
   idf.py flash
   idf.py monitor
   ```

## Usage

Connect to the board with a serial terminal (`idf.py monitor`, PuTTY, minicom, ...). Type `help` for the list of commands and `help <command>` for details on one. The prompt shows the CAN controller state and the TX/RX error counters.

### CAN commands

| Command | Description |
|---------|-------------|
| `canup <speed> [-m mode] [-f] [-r]` | Install the driver and start the interface. `speed` in bps (e.g. `500000`); `-m normal\|no_ack\|listen_only`; `-f` use the configured filters; `-r` auto-recover after bus-off. |
| `candown` | Stop the interface and uninstall the driver (needed before `canup` with different settings). |
| `cansend <ID>#<data>` | Send a frame, all hex. 3 ID digits = standard frame, 4-8 = extended. `cansend 123#DEADBEEF`, `cansend 00008C03#r4` (remote frame, DLC 4). |
| `canstats` | Error counters, lost frames, queue usage. |
| `canstart` | Start the interface after a manual bus recovery. |
| `canrecover` | Start bus-off recovery when auto-recovery is off. |
| `canfilter -c <code> -m <mask> [-d]` | Set a plain hardware acceptance filter (see the ESP-IDF TWAI docs for the register layout). |
| `cansmartfilter <code#mask> ...` | Set up to `CONFIG_CAN_MAX_SMARTFILTERS_NUM` filters; a frame passes if `(ID & mask) == (code & mask)`. Extended IDs only. |

Filters set with `canfilter`/`cansmartfilter` take effect on the next `canup -f`.

### Other commands

`timestamp [-d]` prefixes every printed line with a timestamp, `clrhistory` clears the command history, `log_level <tag|*> <level>` changes ESP-IDF log verbosity, and `free`, `heap`, `tasks`, `version`, `restart` do what they say.

### Example session

```
not installed > canup 500000 -r
Using accept all filters.
Starting CAN in Normal Mode...
CAN driver installed
Auto recovery is enabled!
CAN driver started
error active [TEC: 0][REC: 0] > cansend 123#0102
sent can frame: ID: 00000123 dlc: 2 data: 0102
error active [TEC: 0][REC: 0] > recv can frame: ID: 000002A0 dlc: 8 data: 0011223344556677
```

## Project Structure

```
can_wizard/
├── components/
│   ├── console/                # Fork of the ESP-IDF console component (linenoise with async output support)
│   └── littlefs/               # esp_littlefs (upstream submodule)
├── main/
│   ├── can.c                   # CAN receive task, state tracking, filters
│   ├── cmd_can.c               # CAN console commands (canup, cansend, filters, ...)
│   ├── cmd_system.c            # System commands (free, heap, tasks, restart, log_level)
│   ├── cmd_utils.c             # Utility commands (timestamp, clrhistory)
│   ├── console.c               # Console setup, interactive task and async output task
│   ├── fs.c                    # LittleFS mount (command history storage)
│   └── xvprintf.c              # Ring-buffered printing used by the async output task
├── partitions.csv
└── sdkconfig.defaults
```

## TODO

- port from the deprecated legacy TWAI driver to `esp_driver_twai`
- test dumb mode on a real dumb terminal
- add standard ID filtering to cansmartfilter

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please submit a pull request or open an issue to discuss your ideas.

## Author

Danila Gornushko


