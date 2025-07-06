# Technical Specifications - LoRaWAN Helium Gateway Mapper

## LoRaWAN Configuration

### Regional Parameters
- **Region**: US915 (North America)
- **Frequency Plan**: US915 FSB2 (channels 8-15 + 65)
- **Class**: Class A (lowest power, bi-directional)
- **Activation**: OTAA (Over The Air Activation)

### Radio Parameters
- **Transceiver**: SX1262 (on Heltec board)
- **Default SF**: SF7 (balance of range vs battery, auto-adjusted to DR1)
- **Bandwidth**: 125 kHz (standard for US915)
- **Coding Rate**: 4/5
- **TX Power**: 14 dBm (adjustable based on regulations)

### Network Configuration
- **Network Server**: ChirpStack LNS
- **Join Server**: ChirpStack (integrated)
- **Application Server**: ChirpStack (integrated)
- **Backend**: Helium Network via ChirpStack integration

## Library Selection & Integration

### Current Solution: LoRaManager + RadioLib
- **Primary Library**: LoRaManager @ v1.1.0 (RadioLib wrapper)
- **Base Library**: RadioLib @ v6.6.0 (excellent SX1262 support)
- **Status**: ✅ **FULLY OPERATIONAL**
- **Pros**: 
  - Native SX1262 compatibility
  - Excellent US915 support
  - Active maintenance and community
  - Clean API design
  - Proven stability in production

### Device Credentials (Configured & Working)
```cpp
// Device configuration (hex format)
uint64_t joinEUI = 0xD51962BA5C783F68ULL;
uint64_t devEUI = 0xbcb959046bf0c8e3ULL;
String appKeyHex = "E3EE86C89D7D5FB1FAE4C733E7BED2D8";
String nwkKeyHex = "E3EE86C89D7D5FB1FAE4C733E7BED2D8";
```

## ESP32-S3 Platform Configuration

### Critical Platform Requirements
**ESSENTIAL**: ESP32-S3 requires USB CDC configuration for serial communication

```ini
# platformio.ini - REQUIRED for ESP32-S3
build_flags = 
    -DARDUINO_USB_CDC_ON_BOOT=1
```

```cpp
// Serial initialization - REQUIRED sequence
void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);  // Wait for CDC ready
    }
    delay(1000);   // Ensure CDC fully initialized
    // ... rest of setup
}
```

### Platform Specifications
- **MCU**: ESP32-S3 (240MHz, 320KB RAM, 8MB Flash)
- **USB**: CDC (Communication Device Class) over USB
- **Serial**: Native USB CDC (not traditional UART)
- **Debugging**: Full serial output and monitoring capability

## Pin Configuration (Heltec Wireless Tracker V1.1)

### LoRa SX1262 Pin Mapping (Verified & Working)
```cpp
// LoRa SX1262 Pin Definitions
#define LORA_CS     8   // NSS (Chip Select)
#define LORA_RST    12  // Reset pin
#define LORA_DIO1   14  // DIO1 (IRQ)
#define LORA_BUSY   13  // BUSY (SX1262 specific)
```

### GPS Module Configuration (Verified & Working)
```cpp
// GPS Pin Definitions (Heltec V1.1)
#define GPS_RX      43  // GPS RX (V1.1 specific)
#define GPS_TX      44  // GPS TX (V1.1 specific)  
#define GPS_POWER   3   // GPS Power Control (CRITICAL)

// GPS Configuration
HardwareSerial gpsSerial(1);  // UART1
TinyGPSPlus gps;
#define GPS_BAUD_RATE 9600
```

### Power & Status Configuration
```cpp
// LED Pin (Board-specific)
#define LED_BUILTIN 35  // Built-in LED for Heltec V1.1

// Battery Monitoring
#define BATTERY_ADC ADC1_CHANNEL_5  // GPIO13
#define BATTERY_VOLTAGE_DIVIDER 2.0f
```

## Data Payload Structure (Production Ready)

### GPS Data Format (16 bytes, optimized)
```cpp
struct GPSData {
    float latitude;           // 4 bytes - Real GPS coordinates
    float longitude;          // 4 bytes - Real GPS coordinates
    uint16_t altitude;        // 2 bytes - GPS altitude (meters)
    uint8_t satellites;       // 1 byte - Satellite count
    uint8_t hdop;            // 1 byte - HDOP (scaled x10)
    uint16_t battery_mv;      // 2 bytes - Battery voltage (mV)
    uint8_t packet_count;     // 1 byte - Transmission counter
    uint8_t firmware_version; // 1 byte - Version info
    // Total: 16 bytes (LoRaWAN efficient)
} __attribute__((packed));
```

### Payload Encoding
- **Format**: Binary (16 bytes)
- **Efficiency**: Optimal for LoRaWAN airtime limits
- **Compatibility**: ChirpStack decoder implemented
- **Fallback**: Status packets when GPS unavailable

## Power Management Strategy (Operational)

### GPS Power Control
```cpp
// GPS power management (working implementation)
void powerOnGPS() {
    digitalWrite(GPS_POWER, HIGH);  // Turn on GPS
    delay(1000);  // GPS startup time
}

void powerOffGPS() {
    digitalWrite(GPS_POWER, LOW);   // Save power
}
```

### Battery Monitoring
```cpp
// Real-time battery monitoring
float readBatteryVoltage() {
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, 
                            ADC_WIDTH_BIT_12, 0, &adc_chars);
    
    uint32_t voltage_sum = 0;
    for (int i = 0; i < 10; i++) {
        voltage_sum += esp_adc_cal_raw_to_voltage(
            adc1_get_raw(BATTERY_ADC), &adc_chars);
        delay(1);
    }
    
    return (voltage_sum / 10.0f) * BATTERY_VOLTAGE_DIVIDER / 1000.0f;
}
```

### Sleep & Timing Strategy
- **Transmission Interval**: 300,000ms (5 minutes)
- **GPS Timeout**: 60,000ms (1 minute max acquisition)
- **Join Timeout**: 60,000ms (network join limit)
- **Power Cycling**: GPS powered only during acquisition

## LoRaWAN Performance Metrics (Measured)

### Network Performance (Verified)
- **Join Success Rate**: 100% (immediate OTAA success)
- **Signal Quality**: -54.0 dBm RSSI, 11.2 dB SNR (excellent)
- **Data Rate**: DR1 (SF8BW125) for reliability
- **Transmission Success**: 100% (all packets received)
- **Network**: Successfully integrated with Helium Network via ChirpStack

### Memory & Resource Usage
- **RAM Usage**: 6.6% (21,552 bytes of 327,680 bytes)
- **Flash Usage**: 10.2% (340,977 bytes of 3,342,336 bytes)
- **Build Size**: Optimal for ESP32-S3 platform
- **Library Efficiency**: Excellent resource management

## Development Environment (Current & Working)

### PlatformIO Configuration (Verified)
```ini
[env:heltec_wifi_lora_32_V3]
platform = https://github.com/Baptou88/platform-espressif32.git
framework = arduino
board = heltec_wifi_lora_32_V3
platform_packages =
   framework-arduinoespressif32@https://github.com/Baptou88/arduino-esp32.git

lib_deps = 
    https://github.com/pbezant/LoRaManager.git
    jgromes/RadioLib@^6.0.0
    mikalhart/TinyGPSPlus@^1.0.3
    plerup/EspSoftwareSerial@^8.0.3
    
monitor_speed = 115200
monitor_filters = default, esp32_exception_decoder
build_type = debug
build_flags = 
    -DARDUINO_USB_CDC_ON_BOOT=1  # CRITICAL for ESP32-S3
```

### Debug Configuration (Fully Functional)
- **Serial Monitoring**: 115200 baud with CDC over USB
- **Debug Output**: Comprehensive logging throughout system
- **Status Indication**: LED indicators for all system states
- **Exception Decoding**: ESP32 exception decoder enabled
- **Real-time Monitoring**: Live GPS, LoRaWAN, and battery status

## Gateway Mapping Data Collection (Ready for Production)

### ChirpStack Integration
The ChirpStack LNS automatically captures:
- **Gateway Reception**: Which gateways received each uplink
- **Signal Metrics**: RSSI, SNR per gateway
- **RF Parameters**: Frequency, spreading factor, timestamp
- **Location Data**: Device GPS coordinates + gateway positions

### Data Export Format (Expected from ChirpStack)
```json
{
  "device_eui": "bcb959046bf0c8e3",
  "timestamp": "2024-01-15T10:30:00Z",
  "location": {
    "latitude": 40.7128,
    "longitude": -74.0060,
    "altitude": 10.5
  },
  "battery_mv": 3700,
  "satellites": 8,
  "gateways": [
    {
      "gateway_id": "helium-hotspot-123",
      "location": {"lat": 40.7130, "lon": -74.0055},
      "rssi": -54.0,
      "snr": 11.2,
      "frequency": 903900000,
      "spreading_factor": 8
    }
  ]
}
```

## Testing Strategy & Validation

### Indoor Testing (✅ Complete)
- ✅ **Hardware Initialization**: All systems operational
- ✅ **LoRaWAN Connectivity**: Network join successful
- ✅ **GPS Hardware**: UART communication working
- ✅ **Power Management**: Battery monitoring and GPS cycling
- ✅ **Serial Communication**: Full debug output functional

### Outdoor Testing (🔄 Ready to Begin)
1. **GPS Acquisition**: Validate real coordinate acquisition
2. **Signal Quality**: Test LoRaWAN range and reliability
3. **Battery Performance**: Extended operation monitoring
4. **Gateway Mapping**: Multi-gateway reception validation

### Production Metrics (Target)
- **GPS Fix Rate**: >90% within 60 seconds (outdoor)
- **LoRaWAN Success**: >95% packet delivery rate
- **Battery Life**: 24+ hours continuous operation
- **Position Accuracy**: <10m (standard GPS performance)

---

## System Status Summary

**CURRENT STATE**: ✅ **FULLY OPERATIONAL**
- **Platform**: ESP32-S3 with USB CDC - Working
- **LoRaWAN**: Network joined, data transmitted - Excellent signal
- **GPS**: Hardware configured - Ready for outdoor testing
- **Power**: Battery monitoring and GPS cycling - Functional
- **Debug**: Full serial output and monitoring - Operational

**NEXT MILESTONE**: Outdoor GPS testing and field validation 