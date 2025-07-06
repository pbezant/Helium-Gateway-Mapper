# Helium Gateway Mapper

A LoRaWAN-based asset tracker that maps Helium Network gateway coverage by transmitting GPS location data and recording which gateways receive the packets.

## 🎯 Project Overview

This project converts an existing Reticulum-based asset tracker to use LoRaWAN and the Helium Network for coverage mapping. The device transmits location data via unconfirmed LoRaWAN uplinks while moving through an area, allowing analysis of gateway coverage, signal strength, and network performance.

### Key Features

- **GPS Tracking**: High-precision location tracking with power management
- **LoRaWAN Connectivity**: US915 band optimized for Helium Network  
- **Gateway Mapping**: Records which Helium gateways receive each transmission
- **Power Optimized**: Deep sleep mode for multi-week battery operation
- **ChirpStack Integration**: Backend data collection and analysis
- **Serial Configuration**: Easy parameter adjustment via UART

## 🏗️ Architecture

```
[Mobile Tracker] --LoRaWAN--> [Helium Hotspots] --Internet--> [ChirpStack LNS] --> [Coverage Analysis]
```

- **Hardware**: Heltec Wireless Tracker V1.1 (ESP32-S3 + SX1262)
- **Network**: Helium Network via ChirpStack LNS integration
- **Backend**: PostgreSQL database with coverage analysis tools
- **Protocol**: LoRaWAN 1.0.3 Class A with OTAA activation

## 📋 Requirements

### Hardware
- Heltec Wireless Tracker V1.1 (or compatible ESP32 + SX1262 board)
- GPS antenna (usually included)
- Li-Po battery (3.7V, 1000mAh+ recommended)

### Network Infrastructure  
- Helium Network coverage in target area
- ChirpStack LNS instance connected to Helium
- Helium OUI and DevAddr range (for device provisioning)

### Development Environment
- PlatformIO (recommended) or Arduino IDE
- ESP32 board support package
- USB cable for programming and configuration

## 🚀 Quick Start

### 1. Development Environment Setup

```bash
# Install PlatformIO
pip install platformio

# Clone repository
git clone <repository-url>
cd helium-gateway-mapper

# Build and upload
pio run --target upload --target monitor
```

### 2. Device Configuration

Connect via serial terminal (115200 baud) and configure:

```
CMD> deveui 0004A30B001234AB     # Set device EUI
CMD> appeui 0000000000000001     # Set application EUI  
CMD> appkey 01020304...          # Set application key
CMD> sleep 600                   # Set 10-minute intervals
CMD> save                        # Save configuration
CMD> reboot                      # Restart device
```

### 3. ChirpStack Setup

1. Add device to ChirpStack with matching EUIs and AppKey
2. Configure application to log gateway metadata
3. Set up data export for coverage analysis

## 📖 Documentation

- **[Project Overview](docs/project.md)** - Goals and requirements
- **[Technical Specs](docs/technical.md)** - LoRaWAN configuration and library details  
- **[Architecture](docs/architecture.md)** - System design and data flow
- **[Status](docs/status.md)** - Current progress and next steps

## 🔧 Configuration Parameters

| Parameter | Description | Default | Range |
|-----------|-------------|---------|-------|
| `sleep` | Transmission interval (seconds) | 600 | 60-3600 |
| `deveui` | LoRaWAN Device EUI | - | 8 bytes hex |
| `appeui` | LoRaWAN Application EUI | - | 8 bytes hex |
| `appkey` | LoRaWAN Application Key | - | 16 bytes hex |

Additional parameters for power management, GPS timeouts, and spreading factor selection.

## 📊 Data Format

### Transmitted Payload (Binary)
```cpp
struct AssetData {
    double latitude;              // GPS coordinates
    double longitude;             
    float altitude;               // Meters above sea level
    float battery_voltage;        // Battery voltage
    uint8_t satellites;           // GPS satellite count
    uint8_t fix_status;          // GPS fix quality
    uint16_t sequence_number;     // Packet counter
    // Additional metadata...
} __attribute__((packed));
```

### Gateway Metadata (ChirpStack)
- Signal strength (RSSI) per gateway
- Signal-to-noise ratio (SNR)
- Gateway location coordinates
- Spreading factor and frequency used
- Timestamp and packet routing info

## 🗺️ Coverage Mapping

The system generates coverage maps showing:

- **Gateway Range**: Maximum reception distance per gateway
- **Signal Quality**: RSSI/SNR heat maps across the coverage area
- **Overlap Analysis**: Areas covered by multiple gateways
- **Dead Zones**: Areas with poor or no coverage
- **Network Planning**: Optimal locations for new gateways

## ⚡ Power Management

- **Deep Sleep**: <10µA between transmissions
- **GPS Power Control**: VEXT pin management for battery optimization
- **Adaptive Intervals**: Longer sleep in low battery conditions
- **Battery Monitoring**: Voltage-based power management decisions

Target battery life: **1+ week** with 10-minute transmission intervals

## 🔄 Development Status

**Current Phase**: Implementation Planning  
**Target**: Q1 2025 deployment for coverage mapping

### Recent Progress
- ✅ Requirements analysis and architecture design
- ✅ Library selection (MCCI Arduino LoRaWAN)
- ✅ ChirpStack integration research
- ✅ Technical documentation completed

### Next Steps
- 🚧 PlatformIO environment setup
- 🚧 MCCI LoRaWAN library integration
- 🚧 GPS and power management porting
- 🚧 ChirpStack backend configuration

## 🤝 Contributing

This project supports Helium Network expansion through data-driven gateway deployment decisions. Contributions welcome for:

- Additional hardware platform support
- ChirpStack data analysis tools
- Coverage visualization improvements
- Power optimization enhancements

## 📄 License

[License information to be added]

## 📞 Support

For technical questions or deployment assistance:
- Check documentation in `/docs`
- Review existing GitHub issues
- Contact project maintainers

---

**Disclaimer**: This project is for network analysis and planning purposes. Ensure compliance with local regulations for LoRaWAN device operation and location tracking. 