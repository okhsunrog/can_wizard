
# CAN Wizard

CAN Wizard is a project designed for communication using the CAN (Controller Area Network) protocol, primarily targeted at embedded systems utilizing the ESP32 microcontroller family. This project is developed using ESP-IDF and supports ESP32-C3, allowing flexible development for different use cases.

Here are more information: 
- [Хабр Article](https://habr.com/ru/articles/793326/). (in Russian)
- [Same article in English](https://okhsunrog.ru/articles/2024/02/15/can_bus_sniffer/).

## Features

- **CAN Communication**: Implements CAN communication protocols, enabling data exchange between devices over a CAN bus.
- **File System Integration**: Includes file system operations for handling configuration or logging.
- **Custom serial Console**: A custom serial console implementation for interacting with the system and issuing commands.
- **Modular Design**: Organized in components for easier maintenance and scalability, including linked lists and command utilities.

## Requirements

- **Hardware**: 
  - ESP32-C3 microcontroller
  - SN65HVD230 CAN transceiver

- **Software**:
  - ESP-IDF (version x.x.x or newer)
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

### Console Commands

The CAN Wizard project provides several commands that can be executed through a console interface:

- `can_send <data>`: Send CAN data over the bus.
- `can_receive`: Receive CAN data from the bus.
- `can_status`: Display the current CAN status.

Additional commands can be explored through the console by typing `help`.

### File System Operations

The project includes basic file system operations to read and write configuration or log files. These operations can be accessed through the `fs_*` commands in the console.

## Project Structure

```
can_wizard-main/
├── components/
│   └── C-Linked-List/          # Linked list implementation used in the project
├── main/
│   ├── can.c                   # CAN communication implementation
│   ├── console.c               # Custom console implementation
│   ├── cmd_can.c               # CAN command handlers
│   ├── fs.c                    # File system operations
│   └── main.c                  # Main entry point
└── CMakeLists.txt              # Project build system configuration
```

## TODO:
- code refactoring
- test dumb mode
- fix prompt flickering with some commands
- add standard ID filtering to cansmartfilter

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please submit a pull request or open an issue to discuss your ideas.

## Author

Danila Gornushko


