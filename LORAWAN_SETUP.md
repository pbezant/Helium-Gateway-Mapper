# LoRaWAN Helium Gateway Mapper Setup Guide

## 🚀 **Phase 1 Complete - Ready to Deploy!**

This guide will get your LoRaWAN gateway mapper working with your ChirpStack + Helium Network setup.

## 📋 **Prerequisites**

- ✅ **ChirpStack LNS** configured with Helium Network integration
- ✅ **US915 frequency band** configured
- ✅ **Heltec Wireless Tracker V1.1** (ESP32-S3 + SX1262)
- ✅ **PlatformIO IDE** or Arduino IDE with ESP32 board support

## 🔧 **Hardware Setup**

### Board Configuration 
**CRITICAL FIX**: The original board name doesn't exist in PlatformIO. Use this configuration:

```ini
[env:heltec_wifi_lora_32_V3]
platform = espressif32
board = heltec_wifi_lora_32_V3
```

### Pin Mapping Verification (V1.1 Hardware)
The code uses these pin definitions for Heltec Wireless Tracker V1.1:

```cpp
// LoRa SX1262 Pins
#define LORA_CS     8     // SX1262 NSS
#define LORA_RST    12    // SX1262 RESET  
#define LORA_DIO1   14    // SX1262 DIO1 (IRQ)
#define LORA_SCK    9     // SPI Clock
#define LORA_MISO   11    // SPI MISO
#define LORA_MOSI   10    // SPI MOSI

// GPS UART (UC6580 GNSS) - V1.1 CRITICAL CHANGES
#define GPS_TX      44    // GPS TX (V1.1 specific!)
#define GPS_RX      43    // GPS RX (V1.1 specific!)
#define GPS_POWER   3     // GPS Power (V1.1 CRITICAL: GPIO3)

// Power & Status
#define VEXT_CTRL   36    // External peripheral power
#define BATTERY_ADC 1     // Battery voltage
#define LED_PIN     35    // Status LED
```

**🚨 V1.1 Hardware Notes:**
- GPS UART pins changed from 17/18 to 43/44
- GPS power control moved to GPIO3 (critical for GPS function)
- Vext control for external peripherals on GPIO36

## 🌐 **LoRaWAN Configuration**

### 1. Device Registration in ChirpStack

1. **Create Device Profile**:
   - Device type: Class A
   - LoRaWAN version: 1.0.3
   - Regional parameters: US915
   - Uplink interval: 300s (5 minutes)
   - ADR enabled: Yes

2. **Add Device**:
   - Generate **DevEUI** (8 bytes)
   - Generate **AppEUI** (8 bytes) 
   - Generate **AppKey** (16 bytes)
   - **Save these keys!**

### 2. Configure Device Keys

**Create secrets.h file** (this file is never committed to git):

```bash
# Copy the template
cp include/secrets.h.example include/secrets.h
```

Edit `include/secrets.h` and replace the placeholder keys:

```cpp
// REPLACE THESE WITH YOUR ACTUAL KEYS FROM CHIRPSTACK
static const u1_t PROGMEM APPEUI[8]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u1_t PROGMEM DEVEUI[8]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u1_t PROGMEM APPKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
```

**Key Format**: Copy keys from ChirpStack as MSB (Most Significant Byte first)

**Security**: `secrets.h` is in `.gitignore` - never commit your device keys!

### 3. US915 Frequency Configuration

The device is configured for **US915 FSB2** (channels 8-15 + 65):
- This matches Helium Network's frequency plan
- FSB2 uses channels 8-15 (903.9-905.3 MHz) + channel 65 (923.3 MHz)
- No changes needed unless your region differs

## 📡 **Deployment Process**

### 1. Initial Build & Upload

**✅ Build Verification** - First, test that the project compiles correctly:

```bash
# Using PlatformIO (recommended)
~/.platformio/penv/bin/pio run

# Expected output:
# RAM:   [=         ]   6.4% (used 20864 bytes from 327680 bytes)
# Flash: [=         ]   9.7% (used 322673 bytes from 3342336 bytes)
# ======================================= [SUCCESS] =======================================
```

**📤 Upload to Device**:

```bash
# Upload and monitor
~/.platformio/penv/bin/pio run --target upload --target monitor

# Alternative: Using Arduino IDE
# Select: Tools > Board > Heltec WiFi LoRa 32 (V3)
# Select: Tools > Port > [your device port]
# Upload sketch
```

**🚨 hal_failed() Issue RESOLVED**: If you previously encountered crashes during startup, this has been fixed with proper SPI timing and radio reset sequence. The device should now boot cleanly and display initialization messages.

### 2. First Boot Configuration

1. **Power on device**
2. **Connect to serial monitor** (115200 baud)
3. **Press any key within 5 seconds** to enter configuration mode
4. **Configure parameters**:

```
Commands:
  sleep <seconds>     - Set sleep interval (min 30s)
  gps <seconds>       - Set GPS timeout (10-300s)  
  sf <7-12>          - Set spreading factor
  power <2-20>       - Set TX power (dBm)
  confirmed <0|1>    - Enable/disable confirmed uplinks
  show               - Show current settings
  save               - Save settings to NVS
  reboot             - Restart device
```

### 3. Recommended Initial Settings

```
sf 7              # Good balance of range vs battery
power 14          # Maximum allowed in most regions
confirmed 0       # Use unconfirmed uplinks for mapping
gps 60            # 60 second GPS timeout
sleep 300         # 5 minute transmission interval
save              # Save configuration
reboot            # Restart to apply settings
```

## 📊 **Payload Format**

The device sends binary payloads in this format:

| Bytes | Field | Description |
|-------|--------|-------------|
| 0-3   | Latitude | Float (degrees) |
| 4-7   | Longitude | Float (degrees) |
| 8-9   | Altitude | Int16 (meters) |
| 10    | Satellites | Uint8 (count) |
| 11    | HDOP | Uint8 (scaled x10) |
| 12    | Battery | Uint8 (scaled x10, 0.1V resolution) |
| 13-14 | Sequence | Uint16 (packet counter) |
| 15    | Flags | Uint8 (bit 0: GPS valid) |

## 🔍 **Troubleshooting**

### Join Issues

**Symptom**: Device shows "EV_JOIN_FAILED" repeatedly
**Solutions**:
1. Verify DevEUI/AppEUI/AppKey are correct
2. Check ChirpStack device is enabled
3. Ensure device is within range of Helium hotspot
4. Verify US915 frequency band configuration

### GPS Issues

**Symptom**: No GPS fix acquired
**Solutions**:
1. Ensure device is outdoors with clear sky view
2. Increase GPS timeout: `gps 120`
3. Check GPS antenna connection
4. Verify Vext power is working

### Battery Issues

**Symptom**: Incorrect battery voltage readings
**Solutions**:
1. Check voltage divider factor in `readBatteryVoltage()`
2. Adjust scaling factor based on your hardware
3. Measure actual battery voltage vs ADC reading

### Range Issues

**Symptom**: Packets not reaching gateways
**Solutions**:
1. Increase TX power: `power 20` (check local regulations)
2. Lower spreading factor: `sf 12` (reduces range but increases reliability)
3. Move closer to known Helium hotspot
4. Check antenna connection

## 📈 **ChirpStack Data Analysis**

### Gateway Coverage Analysis

1. **Device Data**: View incoming packets with GPS coordinates
2. **Gateway Metadata**: See which gateways received each packet
3. **Signal Strength**: Analyze RSSI/SNR values per gateway
4. **Coverage Maps**: Plot GPS points with gateway associations

### Payload Decoder

Add this JavaScript decoder to your ChirpStack application:

```javascript
function Decode(fPort, bytes) {
    var decoded = {};
    
    if (bytes.length >= 16) {
        // Parse coordinates
        var lat = new Float32Array(bytes.slice(0, 4).buffer)[0];
        var lon = new Float32Array(bytes.slice(4, 8).buffer)[0];
        var alt = (bytes[8] | (bytes[9] << 8));
        
        decoded.latitude = lat;
        decoded.longitude = lon;
        decoded.altitude = alt;
        decoded.satellites = bytes[10];
        decoded.hdop = bytes[11] / 10.0;
        decoded.battery_voltage = bytes[12] / 10.0;
        decoded.sequence = bytes[13] | (bytes[14] << 8);
        decoded.gps_valid = (bytes[15] & 0x01) ? true : false;
    }
    
    return decoded;
}
```

## 🔋 **Battery Life Optimization**

### Expected Battery Life

- **Sleep interval**: 5 minutes → ~2-3 weeks
- **Sleep interval**: 15 minutes → ~6-8 weeks  
- **Sleep interval**: 30 minutes → ~10-12 weeks

### Power Optimization Tips

1. **Increase sleep interval**: `sleep 900` (15 minutes)
2. **Reduce GPS timeout**: `gps 30` (30 seconds)
3. **Lower TX power**: `power 10` (if coverage allows)
4. **Use SF7**: Faster transmission = less airtime

## 🏁 **Next Steps**

1. **Test join procedure** with your ChirpStack setup
2. **Verify GPS acquisition** in outdoor location
3. **Confirm data reception** in ChirpStack console
4. **Deploy for coverage mapping** in your target area
5. **Analyze coverage data** to identify gateway gaps

## 📞 **Support**

If you encounter issues:
1. Check serial monitor output for error messages
2. Verify ChirpStack logs for join attempts
3. Confirm Helium hotspot is online and operational
4. Test in different locations to isolate range issues

**Your gateway mapper is ready to rock! 🚀** 