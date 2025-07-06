/**
 * Helium Gateway Mapper - LoRaManager DEBUG VERSION  
 * 
 * Hardware: Heltec Wireless Tracker V1.1 (ESP32-S3 + SX1262)
 * Network: US915 LoRaWAN via Helium Network -> ChirpStack
 * Library: LoRaManager (RadioLib wrapper)
 * 
 * DEBUG: Skips GPS, sends test data to verify LoRaWAN pipeline
 */

#include <Arduino.h>
#include <LoRaManager.h>
#include <esp_adc_cal.h>
#include "secrets.h"

// ============================================================================
// FIRMWARE VERSION
// ============================================================================
#define FIRMWARE_VERSION_MAJOR 3
#define FIRMWARE_VERSION_MINOR 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================

// LoRa SX1262 Pin Definitions (Heltec Wireless Tracker V1.1)
#define LORA_CS     8   // NSS
#define LORA_RST    12  // RESET  
#define LORA_DIO1   14  // DIO1 (IRQ)
#define LORA_BUSY   13  // BUSY (SX1262 specific)

// Battery monitoring  
#define BATTERY_ADC ADC1_CHANNEL_5  // GPIO13 (same as LORA_BUSY - we'll read when radio is idle)
#define BATTERY_VOLTAGE_DIVIDER 2.0f // Adjust based on your board

// Timing constants
#define TRANSMISSION_INTERVAL_MS 60000  // 1 minute for debugging
#define JOIN_TIMEOUT_MS 60000          // 1 minute join timeout

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
// DATA STRUCTURES
// ============================================================================

struct TestData {
    float latitude;           // 4 bytes - TEST DATA
    float longitude;          // 4 bytes - TEST DATA 
    uint16_t altitude;        // 2 bytes - TEST DATA
    uint8_t satellites;       // 1 byte - TEST DATA
    uint8_t hdop;            // 1 byte - TEST DATA
    uint16_t battery_mv;      // 2 bytes - REAL BATTERY DATA
    uint8_t packet_count;     // 1 byte - REAL COUNTER
    uint8_t firmware_version; // 1 byte - REAL VERSION
    // Total: 16 bytes
} __attribute__((packed));

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

// LoRaWAN
bool initLoRaWAN();
bool joinNetwork();

// Application
bool createTestDataPacket(uint8_t *buffer, uint8_t &size);
void performDataTransmission();

// ============================================================================
// SETUP FUNCTION
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(3000);
    
    Serial.println("\n=== Helium Gateway Mapper - LoRaManager v3.1 ===");
    Serial.printf("🚀 Firmware: %d.%d\n", FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR);
    Serial.println("📡 Library: LoRaManager (RadioLib wrapper)");
    Serial.println("🛠️ Board: Heltec Wireless Tracker V1.1 (FIXED!)");
    Serial.println("🧪 DEBUG MODE: Skipping GPS, sending test data");
    Serial.println("===============================================");
    
    // Initialize hardware
    Serial.println("1. 🔧 Initializing hardware...");
    initHardware();
    Serial.println("   ✅ Hardware init complete");
    
    // Initialize LoRaWAN
    Serial.println("2. 📡 Initializing LoRaWAN...");
    if (initLoRaWAN()) {
        Serial.println("   ✅ LoRaWAN init complete");
        
        Serial.println("3. 🤝 Joining network...");
        if (joinNetwork()) {
            Serial.println("   🎉 JOIN SUCCESS! Starting data transmission...");
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
    
    Serial.println("4. 🔄 Setup complete - entering main loop");
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
    
    // Send data every minute if joined
    if (isJoined && (millis() - lastTransmissionTime) > TRANSMISSION_INTERVAL_MS) {
        Serial.println("\n📡 Starting periodic transmission...");
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

bool createTestDataPacket(uint8_t *buffer, uint8_t &size) {
    TestData data;
    
    // Create test GPS data (simulate San Francisco area)
    data.latitude = 37.7749f + (packetCounter * 0.0001f);  // Slightly different each time
    data.longitude = -122.4194f + (packetCounter * 0.0001f);
    data.altitude = 100 + (packetCounter % 50);  // 100-150m
    data.satellites = 8 + (packetCounter % 4);   // 8-11 satellites
    data.hdop = 10 + (packetCounter % 20);       // HDOP 1.0-3.0 (stored as *10)
    
    // Real battery data
    float batteryV = readBatteryVoltage();
    data.battery_mv = (uint16_t)(batteryV * 1000.0f);
    
    // Real device info
    data.packet_count = (uint8_t)(packetCounter & 0xFF);
    data.firmware_version = (FIRMWARE_VERSION_MAJOR << 4) | FIRMWARE_VERSION_MINOR;
    
    // Copy to buffer
    memcpy(buffer, &data, sizeof(TestData));
    size = sizeof(TestData);
    
    Serial.printf("📦 Test packet #%d created (%d bytes):\n", packetCounter, size);
    Serial.printf("   🗺️  GPS: %.4f, %.4f, %dm\n", data.latitude, data.longitude, data.altitude);
    Serial.printf("   🛰️  Sats: %d, HDOP: %.1f\n", data.satellites, data.hdop / 10.0f);
    Serial.printf("   🔋 Battery: %dmV, FW: %d.%d\n", 
                  data.battery_mv, 
                  (data.firmware_version >> 4) & 0xF, 
                  data.firmware_version & 0xF);
    
    return true;
}

void performDataTransmission() {
    uint8_t buffer[32];
    uint8_t size;
    
    Serial.println("📡 Starting data transmission...");
    
    if (createTestDataPacket(buffer, size)) {
        packetCounter++;
        Serial.printf("📤 Sending packet #%d on port 1...\n", packetCounter);
        
        // Send unconfirmed data
        if (lora.sendData(buffer, size, 1, false)) {
            Serial.println("   ✅ Data sent successfully!");
            
            // Get signal quality
            Serial.printf("   📶 RSSI: %.1f dBm, SNR: %.1f dB\n", 
                         lora.getLastRssi(), lora.getLastSnr());
            
            // Blink LED to indicate success
            digitalWrite(LED_BUILTIN, HIGH);
            delay(100);
            digitalWrite(LED_BUILTIN, LOW);
            
        } else {
            Serial.printf("   ❌ Failed to send data! Error: %d\n", lora.getLastErrorCode());
            
            // Check if we lost network connection
            if (!lora.isNetworkJoined()) {
                Serial.println("   ⚠️ Network connection lost!");
                isJoined = false;
            }
        }
    } else {
        Serial.println("   ❌ Failed to create packet");
    }
    
    Serial.println("📡 Transmission cycle complete\n");
}