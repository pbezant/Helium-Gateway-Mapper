# Technical Specifications - LoRaWAN Helium Gateway Mapper

## LoRaWAN Configuration

### Regional Parameters
- **Region**: US915 (North America)
- **Frequency Plan**: US915 FSB2 (channels 8-15 + 65)
- **Class**: Class A (lowest power, bi-directional)
- **Activation**: OTAA (Over The Air Activation)

### Radio Parameters
- **Transceiver**: SX1262 (on Heltec board)
- **Default SF**: SF7 (balance of range vs battery)
- **Bandwidth**: 125 kHz (standard for US915)
- **Coding Rate**: 4/5
- **TX Power**: 14 dBm (adjustable based on regulations)

### Network Configuration
- **Network Server**: ChirpStack LNS
- **Join Server**: ChirpStack (integrated)
- **Application Server**: ChirpStack (integrated)
- **Backend**: Helium Network via ChirpStack integration

## Library Selection

### Primary Choice: MCCI Arduino LoRaWAN
- **Pros**: 
  - Mature, well-maintained library
  - Excellent US915 support
  - Works with ChirpStack and Helium
  - Good power management features
  - Session save/restore capabilities
- **Library**: `mcci-catena/arduino-lorawan`
- **Dependency**: `mcci-catena/arduino-lmic` v4.0.0+

### Alternative: Heltec ESP32 LoRaWAN (if license available)
- **Pros**: Optimized for Heltec boards, built-in RTC support
- **Cons**: Requires unique license per board, limited to Heltec hardware
- **Status**: Discontinued maintenance as of Dec 2022

## Pin Configuration (Heltec Wireless Tracker V1.1)

```cpp
// LoRa SX1262 Pin Mapping
const lmic_pinmap lmic_pins = {
    .nss = 8,                    // NSS (Chip Select)
    .rxtx = LMIC_UNUSED_PIN,     // Not used with SX1262
    .rst = 12,                   // Reset pin
    .dio = {14, 13, LMIC_UNUSED_PIN}, // DIO0, DIO1, DIO2
    .rxtx_rx_active = 0,
    .rssi_cal = 0,
    .spi_freq = 8000000          // 8MHz SPI
};
```

## Data Payload Structure

### Asset Data Format (preserving existing structure)
```cpp
struct AssetData {
    double latitude;              // GPS latitude (degrees)
    double longitude;             // GPS longitude (degrees) 
    float altitude;               // GPS altitude (meters)
    uint32_t gps_time;           // GPS time (HHMMSSCC)
    uint32_t gps_date;           // GPS date (DDMMYY)
    uint8_t satellites;          // Number of satellites
    float hdop;                  // Horizontal dilution of precision
    float battery_voltage;       // Battery voltage (V)
    uint8_t fix_status;          // GPS fix status
    uint8_t firmware_version_major;
    uint8_t firmware_version_minor;
    uint16_t sequence_number;    // Added for packet tracking
    uint32_t timestamp;          // Unix timestamp (optional)
} __attribute__((packed));
```

### Payload Encoding Options
1. **Binary**: Raw struct (compact, ~50 bytes)
2. **Cayenne LPP**: Standard format for easy visualization
3. **Custom JSON**: Human readable but larger

## Power Management Strategy

### Sleep Modes
- **Deep Sleep**: ESP32 in hibernation between transmissions
- **Wake Sources**: RTC timer (configurable interval)
- **RAM Preservation**: Critical session data in RTC memory
- **Peripheral Control**: GPS/sensors powered via VEXT pin

### Battery Optimization
- **Transmission Interval**: 5-15 minutes (configurable)
- **Adaptive SF**: Increase SF in low battery mode for better success rate
- **GPS Timeout**: 30-60 seconds max GPS acquisition time
- **Minimal Retries**: Unconfirmed uplinks to reduce airtime

## LoRaWAN Session Management

### OTAA Keys (user configurable)
```cpp
// Device EUI (8 bytes, little-endian)
static const u1_t PROGMEM DEVEUI[8] = { 0x00, 0x00, ... };

// Application EUI (8 bytes, little-endian) 
static const u1_t PROGMEM APPEUI[8] = { 0x00, 0x00, ... };

// Application Key (16 bytes, big-endian)
static const u1_t PROGMEM APPKEY[16] = { 0x00, 0x00, ... };
```

### Session Persistence
- **Save Location**: ESP32 NVS (non-volatile storage)
- **Session Data**: Network session key, app session key, device address, frame counters
- **Benefits**: Faster rejoin after deep sleep, preserves frame counters

## Gateway Mapping Data Collection

### ChirpStack Integration
The ChirpStack LNS will automatically log:
- **Gateway Reception**: Which gateways received each uplink
- **Signal Metrics**: RSSI, SNR per gateway
- **RF Parameters**: Frequency, spreading factor, timestamp
- **Location Data**: Gateway coordinates from ChirpStack database

### Data Export Format
```json
{
  "device_eui": "0004A30B001234AB",
  "timestamp": "2024-01-15T10:30:00Z",
  "location": {
    "latitude": 40.7128,
    "longitude": -74.0060,
    "altitude": 10.5
  },
  "battery": 3.7,
  "gateways": [
    {
      "gateway_id": "helium-hotspot-123",
      "location": {"lat": 40.7130, "lon": -74.0055},
      "rssi": -95,
      "snr": 7.5,
      "frequency": 903900000,
      "spreading_factor": 7
    }
  ]
}
```

## Development Tools

### PlatformIO Configuration
```ini
[env:heltec_wireless_tracker]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200

lib_deps = 
    mcci-catena/arduino-lorawan@^0.10.0
    mcci-catena/arduino-lmic@^4.1.1
    makkakilla/esp-gps@^1.0.2

build_flags = 
    -D ARDUINO_LMIC_CFG_NETWORK_TTN=1
    -D ARDUINO_LMIC_CFG_SUBBAND=2
    -D CFG_us915=1
    -D CFG_sx1262=1
    -D DISABLE_PING=1
    -D DISABLE_BEACONS=1
```

### Debug Configuration
- **Serial Monitoring**: 115200 baud for configuration and debug
- **LMIC Debug**: Compile-time debug levels available
- **LED Indicators**: Visual status for GPS, LoRaWAN, errors
- **WDT**: Watchdog timer to prevent lockups

## Testing Strategy

### Range Testing
1. **Static Tests**: Known locations with different SFs
2. **Mobile Tests**: Walking/driving routes to map coverage
3. **Boundary Tests**: Find edge of gateway coverage areas
4. **Interference Tests**: Urban vs rural environments

### Validation Metrics
- **Packet Success Rate**: % of packets received by at least one gateway
- **Gateway Count**: Number of gateways receiving each packet
- **Signal Quality**: RSSI/SNR distributions across locations
- **Battery Life**: Operational time vs transmission frequency 