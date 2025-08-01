# ShackMate Antenna Tuner Controller

[![Version](https://img.shields.io/badge/version-2.1.0-blue.svg)](https://github.com/ShackMate/ShackMate-AntennaTuner)
[![Platform](https://img.shields.io/badge/platform-ESP32--S3-orange.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

A modular, professional-grade ESP32-S3 based antenna tuner controller with comprehensive CI-V integration, web dashboard, and hardware control capabilities. Designed for amateur radio operators requiring remote antenna tuner control with full protocol compliance.

## 🎯 **Key Features**

### **Hardware Control**
- **ESP32-S3 ATOM S3 Lite** platform with WiFi connectivity
- **MCP23017 I2C GPIO expander** for tuner button/indicator control
- **Real-time hardware monitoring** with 100ms indicator polling
- **Robust I2C communication** with automatic recovery and error handling
- **RGB status LED** for visual system status indication

### **CI-V Protocol Integration**
- **Full CI-V antenna tuner command support**:
  - `CMD 19` - System information (address discovery, IP reporting)
  - `CMD 30` - Model control (991-994 vs 998 variants)
  - `CMD 33` - LED indicator monitoring (tuning/SWR status)
  - `CMD 34` - Remote button control (6 tuner buttons)
- **Protocol compliance** with proper broadcast/direct command handling
- **Infinite loop prevention** with response detection logic
- **WebSocket-based communication** on port 4000
- **Dynamic CI-V addressing** (0xB8-0xBB for devices 1-4)

### **Web Dashboard**
- **Responsive HTML5 interface** with real-time updates
- **Model-specific UI adaptation** (991-994 vs 998 button behavior)
- **Live hardware monitoring** with tuning/SWR indicators
- **CI-V configuration management** with address/model selection
- **System diagnostics** with memory, CPU, and connectivity status
- **WebSocket communication** for instant updates

### **Advanced Button Control**
- **Model-aware behavior**:
  - **Model 991-994**: Latching ANT/AUTO buttons
  - **Model 998**: Momentary ANT button operation
- **Multiple control methods**:
  - Physical button simulation via MCP23017 outputs
  - CI-V remote commands with proper response handling
  - Web dashboard direct control
- **Debouncing and timing control** for reliable operation

### **Network & Discovery**
- **WiFi Manager** for easy configuration with captive portal
- **mDNS discovery** (`shackmate-tuner.local`)
- **UDP broadcast discovery** on port 4210
- **Over-the-Air (OTA) updates** for remote firmware management
- **Remote WebSocket client** for external CI-V integration

## 🏗️ **Architecture**

### **Modular Design**
The project follows a clean, modular architecture with clear separation of concerns:

```
📁 Project Structure
├── src/
│   ├── main.cpp              # 🚀 Application entry point & network management
│   ├── ConfigManager.cpp     # ⚙️ Settings & preferences handling
│   ├── HardwareManager.cpp   # 🔧 MCP23017 & hardware abstraction
│   └── ButtonManager.cpp     # 🎛️ Unified button control logic
├── include/
│   ├── Config.h              # 📝 Project constants & pin definitions
│   ├── ConfigManager.h       # ⚙️ Configuration management interface
│   ├── HardwareManager.h     # 🔧 Hardware control interface
│   └── ButtonManager.h       # 🎛️ Button management interface
├── lib/
│   ├── SMCIV/               # 📡 CI-V protocol implementation
│   └── MCP23017/            # 📚 GPIO expander library
└── data/
    └── index.html           # 🌐 Web dashboard interface
```

### **Core Components**

#### **ConfigManager**
- NVS (Non-Volatile Storage) management for persistent settings
- CI-V model configuration (991-994 vs 998)
- Device number and address calculation
- Button state persistence (ANT/AUTO positions)

#### **HardwareManager**
- MCP23017 I2C GPIO expander control
- Hardware diagnostics and recovery
- LED status indication with color coding
- Real-time indicator monitoring (tuning/SWR)

#### **ButtonManager**
- Unified button control abstraction
- Model-specific behavior implementation
- Pulse timing and output control
- State management for latching buttons

#### **SMCIV Library**
- Complete CI-V protocol implementation
- WebSocket message handling
- Command parsing and response generation
- Callback-based integration architecture

## 🔌 **Hardware Connections**

### **ESP32-S3 ATOM S3 Lite Pinout**
```
Pin 35 → RGB Status LED
Pin 38 → I2C SDA (to MCP23017)
Pin 39 → I2C SCL (to MCP23017)
```

### **MCP23017 GPIO Expander** (Address: 0x27)
```
Input Pins (Tuner Indicators):
├── PA0 (Pin 21) → Tuning Indicator
└── PA5 (Pin 26) → SWR Indicator

Output Pins (Tuner Button Controls):
├── PA1 (Pin 22) → Capacitor Down (C-DN)
├── PA3 (Pin 24) → Inductor Down (L-DN)  
├── PA4 (Pin 25) → Tune Button
├── PA6 (Pin 27) → Capacitor Up (C-UP)
├── PA7 (Pin 28) → Antenna Button (ANT)
└── PB0 (Pin 1)  → Inductor Up (L-UP)
```

## 🚀 **Quick Start**

### **1. Hardware Setup**
1. Connect MCP23017 to ESP32-S3 via I2C (SDA: Pin 38, SCL: Pin 39)
2. Wire tuner control lines to MCP23017 outputs
3. Connect tuner indicator lines to MCP23017 inputs
4. Power the ESP32-S3 via USB-C

### **2. Firmware Installation**
```bash
# Clone the repository
git clone https://github.com/ShackMate/ShackMate-AntennaTuner.git
cd ShackMate-AntennaTuner

# Install PlatformIO dependencies
pio lib install

# Upload firmware and filesystem
pio run --target upload
pio run --target uploadfs
```

### **3. Initial Configuration**
1. **WiFi Setup**: Device creates "shackmate-tuner" access point on first boot
2. **Connect**: Join AP and configure WiFi credentials via captive portal
3. **Access Dashboard**: Navigate to `http://shackmate-tuner.local` or device IP
4. **Configure CI-V**: Set device number (1-4) and model type (991-994 vs 998)

### **4. CI-V Integration**
```bash
# WebSocket connection on port 4000
ws://[device-ip]:4000

# Example CI-V commands (hex format):
FE FE B8 E0 19 00 FD          # System info request
FE FE B8 E0 30 01 FD          # Read tuner model
FE FE B8 E0 33 FD             # Read indicator status
FE FE B8 E0 34 02 FD          # Press TUNE button
```

## 📊 **CI-V Command Reference**

### **CMD 19 - System Information**
| SubCommand | Description | Response Format |
|------------|-------------|-----------------|
| `19 00` | Address discovery | `FE FE EE [addr] 19 00 [addr] FD` |
| `19 01` | IP address request | `FE FE EE [addr] 19 01 [ip1] [ip2] [ip3] [ip4] FD` |

### **CMD 30 - Model Control**
| SubCommand | Description | Response Format |
|------------|-------------|-----------------|
| `30 01` | Read model | `FE FE [from] [addr] 30 [model] FD` |
| `30 00 [data]` | Set model | `FE FE [from] [addr] FB FD` (ACK) |

**Model Codes**: `00` = 991-994, `01` = 998

### **CMD 33 - LED Indicators**
| Command | Description | Response Format |
|---------|-------------|-----------------|
| `33` | Read indicators | `FE FE [from] [addr] 33 [status] FD` |

**Status Bits**: `00` = All OFF, `01` = Tuning ON, `02` = SWR ON, `03` = Both ON

### **CMD 34 - Remote Buttons**
| Button Code | Function | Model Behavior |
|-------------|----------|----------------|
| `00` | ANT | 991-994: Toggle latch, 998: Momentary pulse |
| `01` | ANT 2 | Same as ANT button |
| `02` | TUNE | 200ms pulse (all models) |
| `03` | C-UP | 200ms pulse (all models) |
| `04` | C-DN | 200ms pulse (all models) |
| `05` | L-UP | 200ms pulse (all models) |
| `06` | L-DN | 200ms pulse (all models) |

## 🔧 **Configuration**

### **Device Settings**
- **Device Number**: 1-4 (determines CI-V address 0xB8-0xBB)
- **CI-V Model**: 991-994 (latching) or 998 (momentary)
- **WiFi Credentials**: Managed via WiFiManager
- **Network Discovery**: Automatic via mDNS and UDP broadcast

### **Advanced Options**
All configuration stored in NVS (Non-Volatile Storage):
- Button states (ANT/AUTO positions)
- Last known CI-V model setting
- Network preferences
- Hardware calibration data

## 🔍 **Monitoring & Diagnostics**

### **Web Dashboard Status**
- **System Health**: CPU frequency, memory usage, flash storage
- **Network Status**: WiFi connection, IP address, remote connections
- **Hardware Status**: I2C communication, MCP23017 functionality
- **CI-V Status**: Address, model, last command timestamp
- **Indicator Status**: Real-time tuning/SWR monitoring

### **Debug Output**
Enable detailed logging via Serial Monitor (115200 baud):
```cpp
#define DEBUG_ENABLED 1  // Enable debug output
```

### **LED Status Indicators**
| Color | Pattern | Meaning |
|-------|---------|---------|
| � Red | Blinking | WiFi disconnected |
| � Green | Solid | WiFi connected (no remote CI-V) |
| � Blue | Solid | CI-V remote connected |
| � Purple | Blinking | Captive portal active |
| ⚪ White | Blinking fast | OTA update in progress |

## 🛠️ **Troubleshooting**

### **Common Issues**

#### **CI-V Commands Not Responding**
- Verify WebSocket connection on port 4000
- Check CI-V address calculation (0xB7 + device number)
- Ensure proper hex format with spaces: `FE FE B8 E0 19 00 FD`

#### **Hardware Not Responding**
- Check I2C connections (SDA: Pin 38, SCL: Pin 39)
- Verify MCP23017 address (0x27)
- Use web dashboard diagnostics to test MCP23017

#### **Web Dashboard Not Loading**
- Check WiFi connection and IP address
- Try `http://shackmate-tuner.local` or direct IP
- Verify filesystem upload with `pio run --target uploadfs`

#### **Infinite CI-V Response Loops**
- Built-in response detection prevents loops
- Check for proper command vs response identification
- Verify FROM address in CI-V messages

### **Factory Reset**
1. Hold button during power-on (if available)
2. Or use web dashboard "Factory Reset" option
3. Or erase NVS: `pio run --target erase`

## 🔄 **Development & Updates**

### **OTA Updates**
```bash
# Configure OTA target IP in platformio.ini
pio run --target upload --environment m5stack-atoms3-ota
```

### **Custom Development**
- Modular architecture allows easy feature additions
- Well-documented callback system for hardware integration
- Comprehensive error handling and recovery mechanisms
- PlatformIO build system with dependency management

### **Contributing**
1. Fork the repository
2. Create feature branch: `git checkout -b feature/new-feature`
3. Commit changes: `git commit -m 'Add new feature'`
4. Push to branch: `git push origin feature/new-feature`
5. Submit pull request

## 📜 **License**

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🤝 **Support**

- **Documentation**: [Wiki](https://github.com/ShackMate/ShackMate-AntennaTuner/wiki)
- **Issues**: [GitHub Issues](https://github.com/ShackMate/ShackMate-AntennaTuner/issues)
- **Discussions**: [GitHub Discussions](https://github.com/ShackMate/ShackMate-AntennaTuner/discussions)

## 🏆 **Credits**

- **Author**: Half Baked Circuits
- **Version**: 2.1.0 (Refactored)
- **Platform**: ESP32-S3 with Arduino Framework
- **Built with**: PlatformIO, WebSockets, AsyncTCP, WiFiManager

---

*Professional antenna tuner control for the modern ham shack* 📡
