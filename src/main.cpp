/**
 * Helium Gateway Mapper - LoRaManager REAL GPS VERSION  
 * 
 * Hardware: Heltec Wireless Tracker V1.1 (ESP32-S3 + SX1262)
 * Network: US915 LoRaWAN via Helium Network -> ChirpStack
 * Library: LoRaManager (RadioLib wrapper)
 * 
 * REAL GPS: Uses TinyGPSPlus to get actual coordinates for gateway mapping
 */

#include <Arduino.h>
#include <LoRaManager.h>
#include <esp_adc_cal.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include "secrets.h"

// ============================================================================
// FIRMWARE VERSION
// ============================================================================
#define FIRMWARE_VERSION_MAJOR 3
#define FIRMWARE_VERSION_MINOR 2

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================

// LoRa SX1262 Pin Definitions (Heltec Wireless Tracker V1.1)
#define LORA_CS     8   // NSS
#define LORA_RST    12  // RESET  
#define LORA_DIO1   14  // DIO1 (IRQ)
#define LORA_BUSY   13  // BUSY (SX1262 specific)

// GPS Configuration - Heltec Wireless Tracker V1.1 Correct Pins
#define GPS_POWER 3        // GPIO3 - V1.1 GPS power control pin
#define GPS_RX 16          // GPIO16 - GPS RX pin (ESP32 receives from GPS)
#define GPS_TX 17          // GPIO17 - GPS TX pin (ESP32 transmits to GPS)
#define GPS_BAUD_RATE 9600 // UC6580 GPS module baud rate

// LED Pin Definition (Heltec Wireless Tracker V1.1)
#define LED_BUILTIN 35  // Built-in LED for Heltec V1.1

// Battery monitoring  
#define BATTERY_ADC ADC1_CHANNEL_5  // GPIO13 (same as LORA_BUSY - we'll read when radio is idle)
#define BATTERY_VOLTAGE_DIVIDER 2.0f // Adjust based on your board

// Timing constants
#define TRANSMISSION_INTERVAL_MS 300000  // 5 minutes for real deployment
#define JOIN_TIMEOUT_MS 60000           // 1 minute join timeout
#define GPS_TIMEOUT_MS 300000           // 5 minutes GPS timeout for outdoor acquisition
#define GPS_BAUD_RATE 9600              // Standard GPS baud rate

// ============================================================================
// LORAWAN CONFIGURATION  
// ============================================================================

// Convert byte arrays to proper format for LoRaManager
uint64_t joinEUI = 0xD51962BA5C783F68ULL;  // APPEUI in correct byte order
uint64_t devEUI = 0xbcb959046bf0c8e3ULL;   // DEVEUI: bcb9596b0f0c8e3 -> bcb959046bf0c8e3

// Convert APPKEY to hex string for LoRaManager
String appKeyHex = "E3EE86C89D7D5FB1FAE4C733E7BED2D8";
String nwkKeyHex = "E3EE86C89D7D5FB1FAE4C733E7BED2D8";  // Same as AppKey for LoRaWAN 1.0.x

// ============================================================================
// GPS CONFIGURATION
// ============================================================================

// GPS hardware
HardwareSerial gpsSerial(2);  // Use UART2 for GPS with explicit pins
TinyGPSPlus gps;

// GPS fix quality enum
enum GPSFixQuality {
    NO_FIX = 0,
    GPS_FIX_2D = 1,
    GPS_FIX_3D = 2,
    DGPS_FIX = 3
};

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct GPSData {
    float latitude;           // 4 bytes - REAL GPS DATA
    float longitude;          // 4 bytes - REAL GPS DATA
    uint16_t altitude;        // 2 bytes - REAL GPS DATA
    uint8_t satellites;       // 1 byte - REAL GPS DATA
    uint8_t hdop;            // 1 byte - REAL GPS DATA (scaled x10)
    uint16_t battery_mv;      // 2 bytes - REAL BATTERY DATA
    uint8_t packet_count;     // 1 byte - REAL COUNTER
    uint8_t firmware_version; // 1 byte - REAL VERSION
    // Total: 16 bytes (maintaining compatibility)
} __attribute__((packed));

// Last known location for fallback
struct LastKnownLocation {
    bool valid;
    float latitude;
    float longitude;
    uint16_t altitude;
    uint8_t satellites;
    uint8_t hdop;
    unsigned long timestamp;
} lastKnownGPS = {false, 0.0, 0.0, 0, 0, 99, 0};

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// LoRaManager instance
LoRaManager lora;

// State variables
bool isJoined = false;
uint32_t packetCounter = 0;
unsigned long lastTransmissionTime = 0;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Hardware
void initHardware();
float readBatteryVoltage();

// GPS
void initGPS();
void powerOnGPS();
void powerOffGPS();
bool acquireGPSFix(GPSData &data, uint32_t timeoutMs);
GPSFixQuality getGPSFixQuality();
// LoRaWAN
bool initLoRaWAN();
bool joinNetwork();

// Application
bool createGPSDataPacket(uint8_t *buffer, uint8_t &size);
void performDataTransmission();

// ============================================================================
// SETUP FUNCTION
// ============================================================================

void setup() {
    // Initialize CDC serial and wait for it to be ready (ESP32-S3 fix)
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    delay(1000); // Additional delay to ensure CDC is fully ready
    
    Serial.println("\n=== Helium Gateway Mapper - LoRaManager v3.2 ===");
    Serial.printf("🚀 Firmware: %d.%d\n", FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR);
    Serial.println("📡 Library: LoRaManager (RadioLib wrapper)");
    Serial.println("🛠️ Board: Heltec Wireless Tracker V1.1");
    Serial.println("🗺️  REAL GPS: TinyGPSPlus for actual coordinates");
    Serial.println("⚡ ESP32-S3 with USB CDC enabled");
    Serial.println("===============================================");
    
    // Initialize hardware
    Serial.println("1. 🔧 Initializing hardware...");
    initHardware();
    Serial.println("   ✅ Hardware init complete");
    
    // Initialize GPS
    Serial.println("2. 🛰️  Initializing GPS...");
    initGPS();
    Serial.println("   ✅ GPS init complete");
    
    // Initialize LoRaWAN
    Serial.println("3. 📡 Initializing LoRaWAN...");
    if (initLoRaWAN()) {
        Serial.println("   ✅ LoRaWAN init complete");
        
        Serial.println("4. 🤝 Joining network...");
        if (joinNetwork()) {
            Serial.println("   🎉 JOIN SUCCESS! Starting GPS mapping...");
            isJoined = true;
            lastTransmissionTime = millis();
            
            // Send first packet immediately
            performDataTransmission();
        } else {
            Serial.println("   ❌ JOIN FAILED - will retry in loop");
        }
    } else {
        Serial.println("   ❌ LoRaWAN init failed");
    }
    
    Serial.println("5. 🔄 Setup complete - entering main loop");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    // Try to join if not joined
    if (!isJoined) {
        Serial.println("\n🔄 Attempting to join network...");
        if (joinNetwork()) {
            Serial.println("🎉 JOIN SUCCESS!");
            isJoined = true;
            lastTransmissionTime = millis();
            performDataTransmission();
        } else {
            Serial.println("❌ Join failed, retrying in 30 seconds...");
            delay(30000);
        }
        return;
    }
    
    // Send data every 5 minutes if joined
    if (isJoined && (millis() - lastTransmissionTime) > TRANSMISSION_INTERVAL_MS) {
        Serial.println("\n📡 Starting periodic GPS transmission...");
        performDataTransmission();
        lastTransmissionTime = millis();
    }
    
    // Check if we're still joined
    if (!lora.isNetworkJoined()) {
        Serial.println("⚠️ Network connection lost, will rejoin...");
        isJoined = false;
    }
    
    delay(1000);
}

// ============================================================================
// HARDWARE FUNCTIONS
// ============================================================================

void initHardware() {
    // Initialize LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // LED on during init
    
    // Initialize GPS power control
    pinMode(GPS_POWER, OUTPUT);
    powerOffGPS(); // Start with GPS off
    
    // Brief delay for LED
    delay(500);
    digitalWrite(LED_BUILTIN, LOW); // LED off after init
}

float readBatteryVoltage() {
    // Configure ADC
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 0, &adc_chars);
    
    // Take multiple readings for stability
    uint32_t voltage_sum = 0;
    for (int i = 0; i < 10; i++) {
        voltage_sum += esp_adc_cal_raw_to_voltage(adc1_get_raw(BATTERY_ADC), &adc_chars);
        delay(1);
    }
    
    float voltage = (voltage_sum / 10.0f) * BATTERY_VOLTAGE_DIVIDER / 1000.0f; // Convert to volts
    return voltage;
}

// ============================================================================
// GPS FUNCTIONS
// ============================================================================

void initGPS() {
    // GPS uses explicit UART pins for V1.1 hardware
    Serial.println("   📍 GPS UART configured: RX=16, TX=17, Baud=9600");
}



void powerOnGPS() {
    pinMode(GPS_POWER, OUTPUT);
    digitalWrite(GPS_POWER, HIGH);  // HIGH = Power ON for V1.1 hardware
    Serial.println("   🔋 GPS powered ON (GPIO3 set HIGH)");
    delay(200); // Give power rail time to stabilize
}

void powerOffGPS() {
    digitalWrite(GPS_POWER, LOW);   // LOW = Power OFF for V1.1 hardware
    Serial.println("   🔋 GPS powered OFF (GPIO3 set LOW)");
}

bool acquireGPSFix(GPSData &data, uint32_t timeoutMs) {
    powerOnGPS();
    
    // Initialize GPS serial AFTER powering on with V1.1 specific pins
    gpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX, GPS_TX);
    Serial.println("   📍 GPS UART initialized: RX=16, TX=17, Baud=9600");
    
    // GPS initialization delay
    Serial.println("   ⏳ Waiting for UC6580 initialization...");
    delay(2000);
    
    Serial.printf("   🛰️  Acquiring GPS fix (timeout: %d seconds)...\n", timeoutMs / 1000);
    
    unsigned long startTime = millis();
    unsigned long lastStatusTime = startTime;
    bool fixAcquired = false;
    int bytesReceived = 0;
    int validSentences = 0;
    
    while (millis() - startTime < timeoutMs) {
        while (gpsSerial.available()) {
            char c = gpsSerial.read();
            bytesReceived++;
            
            if (gps.encode(c)) {
                validSentences++;
                // Check for valid fix with minimum requirements
                if (gps.location.isUpdated() && gps.location.isValid() &&
                    gps.satellites.isUpdated() && gps.satellites.isValid() && gps.satellites.value() >= 3 &&
                    gps.hdop.isUpdated() && gps.hdop.isValid() && gps.hdop.value() <= 500) {
                    
                    fixAcquired = true;
                    data.latitude = gps.location.lat();
                    data.longitude = gps.location.lng();
                    data.altitude = gps.altitude.isValid() ? (uint16_t)gps.altitude.meters() : 0;
                    data.satellites = gps.satellites.value();
                    data.hdop = gps.hdop.isValid() ? (uint8_t)(gps.hdop.hdop() * 10) : 99;
                    
                    // Update Last Known Location
                    lastKnownGPS.valid = true;
                    lastKnownGPS.latitude = data.latitude;
                    lastKnownGPS.longitude = data.longitude;
                    lastKnownGPS.altitude = data.altitude;
                    lastKnownGPS.satellites = data.satellites;
                    lastKnownGPS.hdop = data.hdop;
                    lastKnownGPS.timestamp = millis();
                    
                    Serial.printf("   🎉 GPS fix acquired! Lat: %.6f, Lon: %.6f, Sats: %d\n",
                                 data.latitude, data.longitude, data.satellites);
                    break;
                }
            }
        }
        
        if (fixAcquired) break;
        
        // Status update every 15 seconds
        if (millis() - lastStatusTime >= 15000) {
            Serial.printf("   📊 GPS Status: %d bytes, %d sentences, %d satellites visible\n",
                         bytesReceived, validSentences, gps.satellites.value());
            lastStatusTime = millis();
        }
        
        delay(100);
    }
    
    gpsSerial.end();
    powerOffGPS();
    
    if (fixAcquired) {
        Serial.println("   ✅ GPS fix acquired successfully");
        return true;
    } else {
        Serial.printf("   ❌ GPS fix failed - %d bytes, %d sentences, %d satellites\n",
                     bytesReceived, validSentences, gps.satellites.value());
        
        if (bytesReceived > 0) {
            Serial.println("   📡 GPS communication working but no fix acquired");
        } else {
            Serial.println("   🔧 No GPS communication detected");
        }
        return false;
    }
}

GPSFixQuality getGPSFixQuality() {
    if (!gps.location.isValid()) return NO_FIX;
    if (gps.satellites.value() >= 4) return GPS_FIX_3D;
    if (gps.satellites.value() >= 3) return GPS_FIX_2D;
    return NO_FIX;
}

// ============================================================================
// LORAWAN FUNCTIONS
// ============================================================================

bool initLoRaWAN() {
    // Initialize the LoRa module
    if (!lora.begin(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY)) {
        Serial.println("   ❌ LoRa hardware init failed!");
        return false;
    }
    Serial.println("   ✅ LoRa hardware init OK");
    
    // Set LoRaWAN credentials using hex strings
    if (!lora.setCredentialsHex(joinEUI, devEUI, appKeyHex, nwkKeyHex)) {
        Serial.println("   ❌ Failed to set credentials!");
        return false;
    }
    Serial.println("   ✅ Credentials configured");
    
    // Print credentials for verification
    Serial.printf("   📋 DevEUI: %016llX\n", devEUI);
    Serial.printf("   📋 JoinEUI: %016llX\n", joinEUI);
    Serial.printf("   📋 AppKey: %s\n", appKeyHex.c_str());
    
    return true;
}

bool joinNetwork() {
    Serial.println("   🤝 Sending join request...");
    
    if (lora.joinNetwork()) {
        Serial.println("   ✅ Network joined successfully!");
        
        // Get signal quality
        Serial.printf("   📶 RSSI: %.1f dBm, SNR: %.1f dB\n", 
                     lora.getLastRssi(), lora.getLastSnr());
        
        // Blink LED to indicate success
        for (int i = 0; i < 3; i++) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(200);
            digitalWrite(LED_BUILTIN, LOW);
            delay(200);
        }
        
        return true;
    } else {
        Serial.printf("   ❌ Join failed! Error code: %d\n", lora.getLastErrorCode());
        return false;
    }
}

// ============================================================================
// APPLICATION FUNCTIONS
// ============================================================================

bool createGPSDataPacket(uint8_t *buffer, uint8_t &size) {
    GPSData data;
    bool usingRealGPS = false;
    
    // Try to get real GPS fix
    if (acquireGPSFix(data, GPS_TIMEOUT_MS)) {
        usingRealGPS = true;
        Serial.println("📦 Using REAL GPS data");
    } else if (lastKnownGPS.valid) {
        // Use last known location as fallback
        data.latitude = lastKnownGPS.latitude;
        data.longitude = lastKnownGPS.longitude;
        data.altitude = lastKnownGPS.altitude;
        data.satellites = lastKnownGPS.satellites;
        data.hdop = lastKnownGPS.hdop;
        Serial.println("📦 Using LAST KNOWN GPS location");
    } else {
        // No GPS data available - send packet with zero coordinates but real battery/device info
        data.latitude = 0.0f;
        data.longitude = 0.0f;
        data.altitude = 0;
        data.satellites = 0;
        data.hdop = 99;  // Invalid HDOP to indicate no GPS
        Serial.println("📦 NO GPS data available - sending status packet");
    }
    
    // Real battery data
    float batteryV = readBatteryVoltage();
    data.battery_mv = (uint16_t)(batteryV * 1000.0f);
    
    // Real device info
    data.packet_count = (uint8_t)(packetCounter & 0xFF);
    data.firmware_version = (FIRMWARE_VERSION_MAJOR << 4) | FIRMWARE_VERSION_MINOR;
    
    // Copy to buffer
    memcpy(buffer, &data, sizeof(GPSData));
    size = sizeof(GPSData);
    
    Serial.printf("📦 GPS packet #%d created (%d bytes):\n", packetCounter, size);
    if (data.latitude != 0.0f || data.longitude != 0.0f) {
        Serial.printf("   🗺️  Location: %.6f, %.6f, %dm\n", data.latitude, data.longitude, data.altitude);
        Serial.printf("   🛰️  Quality: %d sats, HDOP: %.1f\n", data.satellites, data.hdop / 10.0f);
    } else {
        Serial.println("   🗺️  Location: NO GPS FIX");
    }
    Serial.printf("   🔋 Battery: %dmV (%.2fV), FW: %d.%d\n", 
                  data.battery_mv, batteryV,
                  (data.firmware_version >> 4) & 0xF, 
                  data.firmware_version & 0xF);
    
    return true;
}

void performDataTransmission() {
    uint8_t buffer[32];
    uint8_t size;
    
    Serial.println("📡 Starting GPS data transmission...");
    
    if (createGPSDataPacket(buffer, size)) {
        packetCounter++;
        Serial.printf("📤 Sending GPS packet #%d on port 1...\n", packetCounter);
        
        // Send unconfirmed data
        if (lora.sendData(buffer, size, 1, false)) {
            Serial.println("   ✅ GPS data sent successfully!");
            
            // Get signal quality
            Serial.printf("   📶 RSSI: %.1f dBm, SNR: %.1f dB\n", 
                         lora.getLastRssi(), lora.getLastSnr());
            
            // Blink LED to indicate success
            digitalWrite(LED_BUILTIN, HIGH);
            delay(100);
            digitalWrite(LED_BUILTIN, LOW);
            
        } else {
            Serial.printf("   ❌ Failed to send GPS data! Error: %d\n", lora.getLastErrorCode());
            
            // Check if we lost network connection
            if (!lora.isNetworkJoined()) {
                Serial.println("   ⚠️ Network connection lost!");
                isJoined = false;
            }
        }
    } else {
        Serial.println("   ❌ Failed to create GPS packet");
    }
    
    Serial.println("📡 GPS transmission cycle complete\n");
}