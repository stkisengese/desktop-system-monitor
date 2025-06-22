# Desktop System Monitor

A comprehensive Linux desktop system monitoring application built with C++ and Dear ImGui, providing real-time visualization of system resources including CPU, memory, network, and thermal information.

## Overview

This project is a feature-rich system monitor that leverages the Linux `/proc` filesystem and hardware sensors to provide detailed insights into system performance. The application uses Dear ImGui for an immediate-mode graphical user interface, offering interactive charts, tables, and controls for monitoring various system components.

## Features

### System Information Window
- **Operating System Detection**: Displays the current Linux distribution
- **User Information**: Shows logged-in username and hostname  
- **Process Overview**: Real-time count of running, sleeping, zombie, and stopped processes
- **CPU Information**: Detailed CPU model and specification display

### Performance Monitoring (Tabbed Interface)
- **CPU Monitoring**:
  - Real-time CPU usage percentage with overlay text
  - Interactive performance graph with historical data
  - Configurable FPS control (1-30 FPS)
  - Adjustable Y-axis scale (0-100%, 0-200%)
  - Pause/Resume functionality

- **Thermal Monitoring**:
  - Current temperature display with overlay text
  - Temperature history graph with same controls as CPU
  - Support for multiple thermal sensors
  - Graceful handling of systems without thermal sensors

- **Fan Monitoring**:
  - Fan status display (Active/Inactive)
  - Current fan speed (RPM) and level information
  - Fan speed history visualization
  - Multi-fan system support

### Memory and Process Management
- **Memory Usage Visualization**:
  - Physical RAM usage with progress bars and percentages
  - Virtual memory (SWAP) usage display
  - Disk usage monitoring with color-coded indicators
  - Smart unit conversion (bytes to KB/MB/GB)

- **Process Table**:
  - Comprehensive process listing with PID, Name, State, CPU%, Memory%
  - Sortable columns with click-to-sort functionality
  - Real-time process filtering by name
  - Multi-row selection with Ctrl+Click and Shift+Click
  - Live updates every 3-5 seconds

### Network Monitoring
- **Interface Detection**: Automatic discovery of all network interfaces (lo, eth0, wlp5s0, etc.)
- **IP Address Display**: IPv4 addresses for each detected interface
- **Statistics Tables** (Tabbed RX/TX):
  - **RX Table**: Bytes, Packets, Errors, Drops, FIFO, Frame, Compressed, Multicast
  - **TX Table**: Bytes, Packets, Errors, Drops, FIFO, Collisions, Carrier, Compressed
- **Visual Usage Display**:
  - Progress bars for network usage (0GB to 2GB scale)
  - Smart unit conversion avoiding too-small or too-large values
  - Separate RX and TX visualization tabs

## Technical Architecture

### Core Components
- **main.cpp**: Application entry point and main rendering loop
- **system.cpp**: System information gathering and CPU monitoring
- **mem.cpp**: Memory usage and process management
- **network.cpp**: Network interface and statistics monitoring
- **header.h**: Shared data structures and function declarations

### Data Structures
```cpp
struct SystemInfo {
    string os_name, hostname, username, cpu_model;
    int total_processes, running_processes, sleeping_processes;
    int zombie_processes, stopped_processes;
};

struct MemoryInfo {
    unsigned long total_ram, available_ram, used_ram;
    unsigned long total_swap, used_swap;
    unsigned long total_disk, used_disk;
};

struct ThermalInfo {
    float temperature;
    bool available;
};

struct FanInfo {
    int speed, level;
    bool active, available;
};
```

### Data Sources
- **System Information**: `/proc/stat`, `/proc/sys/kernel/hostname`, `/proc/cpuinfo`
- **Memory Data**: `/proc/meminfo`, `statvfs()` system calls
- **Process Information**: `/proc/[pid]/stat`, `/proc/[pid]/comm`
- **Network Statistics**: `/proc/net/dev`, `getifaddrs()` system calls
- **Thermal Data**: `/sys/class/thermal/thermal_zone*/temp`
- **Fan Information**: `/sys/class/hwmon/hwmon*/fan*_input`

## Prerequisites

### System Requirements
- Linux operating system (Ubuntu, Debian, CentOS, etc.)
- GCC compiler with C++11 support or newer
- SDL2 development libraries
- OpenGL support

### Dependencies
```bash
# Ubuntu/Debian
sudo apt install libsdl2-dev build-essential

# CentOS/RHEL
sudo yum install SDL2-devel gcc-c++

# Arch Linux
sudo pacman -S sdl2 gcc
```

## Installation and Build

### Clone and Build
```bash
# Clone the repository
git clone https://github.com/stkisengese/desktop-system-monitor.git
cd desktop-system-monitor

# Build the project
make

# Run the application
./system-monitor
```

### File Structure
```
system-monitor/
├── header.h                    # Shared headers and data structures
├── main.cpp                    # Main application loop
├── system.cpp                  # System monitoring functions
├── mem.cpp                     # Memory and process monitoring
├── network.cpp                 # Network monitoring functions
├── Makefile                    # Build configuration
└── imgui/                      # Dear ImGui library
    └── lib/
        ├── backend/            # SDL2 + OpenGL3 backend
        ├── gl3w/              # OpenGL loader
        └── [imgui files]      # Core ImGui implementation
```

## Usage

### Navigation
- **System Window**: Overview of system information and performance graphs
- **Memory Window**: RAM/SWAP usage and process management
- **Network Window**: Network interface statistics and usage visualization

### Interactive Controls
- **Graph Controls**: Use pause/resume buttons to freeze data collection
- **FPS Slider**: Adjust graph update frequency (1-30 FPS)
- **Scale Slider**: Modify Y-axis range for better data visualization
- **Process Filtering**: Type in the filter box to search processes by name
- **Multi-Selection**: Use Ctrl+Click or Shift+Click for multiple process selection

### Performance Tips
- Reduce FPS for lower CPU usage by the monitor itself
- Use pause functionality when analyzing specific time periods
- Filter processes to focus on specific applications

## Development Roadmap

### Completed Features ✅
- Basic system information display
- Real-time CPU monitoring with graphs
- Memory usage visualization
- Process table with sorting and filtering
- Network interface detection and statistics

### Planned Enhancements 🔄
- [ ] Thermal monitoring implementation
- [ ] Fan speed monitoring and control
- [ ] Advanced process management (kill, priority changes)
- [ ] Configuration file support
- [ ] Export functionality for performance data
- [ ] Multi-threaded data collection optimization

## Performance Characteristics

### Resource Usage
- **CPU Usage**: < 5% on modern systems
- **Memory Footprint**: < 50MB typical usage
- **Update Intervals**:
  - System info: Every 5 seconds
  - CPU/Thermal/Fan: Every 1 second
  - Memory: Every 3 seconds
  - Processes: Every 5 seconds
  - Network: Every 2 seconds

## Error Handling

The application gracefully handles common issues:
- Missing `/proc` filesystem entries
- Permission denied errors
- Unavailable hardware sensors (thermal, fan)
- Network interfaces going offline
- Process enumeration race conditions

When features are unavailable, the application displays "N/A" and continues monitoring available components.

## Troubleshooting

### Common Issues
- **Blank thermal data**: System may not have accessible thermal sensors
- **Missing fan information**: Fan sensors might not be available or accessible
- **Permission errors**: Some system files may require elevated privileges
- **Network statistics not updating**: Check if `/proc/net/dev` is accessible

### Debug Mode
Run with verbose output to diagnose issues:
```bash
./system-monitor --debug
```

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/new-monitoring`)
3. Commit your changes (`git commit -am 'Add new monitoring feature'`)
4. Push to the branch (`git push origin feature/new-monitoring`)
5. Create a Pull Request

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- **Dear ImGui**: Excellent immediate-mode GUI library
- **SDL2**: Cross-platform development library
- **Linux Kernel**: For providing the `/proc` and `/sys` filesystems
- **Community**: Various online resources and documentation

## Technical Notes

### Dear ImGui Integration
This project uses Dear ImGui's immediate-mode rendering approach, where the application state is separate from the graphics library. The SDL2 + OpenGL3 backend handles input and rendering, while the application focuses on data collection and presentation logic.

### Linux Filesystem Usage
Extensive use of Linux virtual filesystems:
- `/proc`: Process and kernel information
- `/sys`: Hardware device information and control
- Standard system calls for network and disk information

### Thread Safety
Data collection and GUI rendering are designed to be thread-safe, with proper synchronization for shared data structures and update mechanisms.

---

*For additional support or questions, please refer to the project documentation or create an issue in the repository.*
