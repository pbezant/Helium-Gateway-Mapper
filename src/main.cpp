/**
 * Helium Gateway Mapper - DEBUG VERSION
 * 
 * Hardware: Heltec Wireless Tracker V1.1 (ESP32-S3 + SX1262)
 * Network: US915 LoRaWAN via Helium Network -> ChirpStack
 * 
 * DEBUG: Skips GPS, sends test data to verify LoRaWAN pipeline
 */

#include <Arduino.h>
#include <SX126x-Arduino.h>
#include <LoRaWan-Arduino.h>
#include <Preferences.h>
#include <esp_adc_cal.h>
#include "secrets.h"

// ============================================================================
// FIRMWARE VERSION
// ============================================================================
#define FIRMWARE_VERSION_MAJOR 2
#define FIRMWARE_VERSION_MINOR 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================

// LoRa SX1262 Pin Definitions (Heltec Wireless Tracker V1.1)
#define LORA_CS     8   // NSS
#define LORA_RST    12  // RESET  
#define LORA_DIO1   14  // DIO1 (IRQ)
#define LORA_BUSY   13  // BUSY (SX1262 specific)
#define LORA_MISO   11  // SPI MISO
#define LORA_MOSI   10  // SPI MOSI
#define LORA_SCK    9   // SPI SCK

// Battery monitoring
#define BATTERY_ADC ADC1_CHANNEL_5  // GPIO13
#define BATTERY_VOLTAGE_DIVIDER 2.0f // Adjust based on your board

// Timing constants
#define TRANSMISSION_INTERVAL_MS 60000  // 1 minute for debugging (was 5 minutes)

// ============================================================================
// LORAWAN CONFIGURATION  
// ============================================================================

// Regional settings
#define LORAWAN_REGION       LORAMAC_REGION_US915
#define LORAWAN_DATARATE     DR_0
#define LORAWAN_PUBLIC       true
#define LORAWAN_ADR          false
#define LORAWAN_TX_POWER     TX_POWER_0
#define LORAWAN_DUTY_CYCLE   false

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

// LoRaWAN parameters
lmh_param_t lora_param_init = {
    LORAWAN_ADR,
    LORAWAN_DATARATE,
    LORAWAN_PUBLIC,
    3,  // Join trials
    LORAWAN_TX_POWER,
    LORAWAN_DUTY_CYCLE
};

// Hardware configuration
hw_config hwConfig;

// Forward declarations for callbacks
void lorawan_rx_handler(lmh_app_data_t *app_data);
void lorawan_has_joined_handler(void);
void lorawan_confirm_class_handler(DeviceClass_t Class);
void lorawan_join_failed_handler(void);
void lorawan_unconfirmed_finished(void);
void lorawan_confirmed_finished(bool result);

// LoRaWAN callbacks structure
static lmh_callback_t lora_callbacks = {
    BoardGetBatteryLevel, BoardGetUniqueId, BoardGetRandomSeed,
    lorawan_rx_handler, lorawan_has_joined_handler, lorawan_confirm_class_handler,
    lorawan_join_failed_handler, lorawan_unconfirmed_finished, lorawan_confirmed_finished
};

// OTAA Keys from secrets.h  
uint8_t nodeDeviceEUI[8];
uint8_t nodeAppEUI[8]; 
uint8_t nodeAppKey[16];

// State variables
bool isJoined = false;
bool transmissionComplete = false;
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
void startJoinProcedure();

// Application
bool createTestDataPacket(uint8_t *buffer, uint8_t &size);
void performDataTransmission();

// ============================================================================
// SETUP FUNCTION
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\\n=== Helium Gateway Mapper - DEBUG ===");
    Serial.printf("Firmware: %d.%d\\n", FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR);
    Serial.println("DEBUG MODE: Skipping GPS, sending test data");
    Serial.println("=====================================");
    
    // Initialize hardware
    Serial.println("1. Initializing hardware...");
    initHardware();
    Serial.println("   Hardware init complete");
    
    // Initialize LoRaWAN
    Serial.println("2. Initializing LoRaWAN...");
    if (initLoRaWAN()) {
        Serial.println("   LoRaWAN init complete");
        
        Serial.println("3. Starting join procedure...");
        startJoinProcedure();
        
        // Wait for join with timeout
        Serial.println("4. Waiting for network join...");
        unsigned long joinStart = millis();
        while (!isJoined && (millis() - joinStart) < 60000) {
            delay(1000);
            Serial.print(".");
        }
        Serial.println();
        
        if (isJoined) {
            Serial.println("5. ✅ JOIN SUCCESS! Starting data transmission...");
            lastTransmissionTime = millis();
            performDataTransmission();
        } else {
            Serial.println("5. ❌ JOIN FAILED - will retry...");
        }
    } else {
        Serial.println("   ❌ LoRaWAN init failed");
    }
    
    Serial.println("6. Setup complete - entering main loop");
}

void loop() {
    // Send data every minute in debug mode
    if (isJoined && (millis() - lastTransmissionTime) > TRANSMISSION_INTERVAL_MS) {
        Serial.println("\\n🔄 Starting periodic transmission...");
        performDataTransmission();
        lastTransmissionTime = millis();
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
    
    // Configure SX1262 hardware
    hwConfig.CHIP_TYPE = SX1262_CHIP;
    hwConfig.PIN_LORA_RESET = LORA_RST;
    hwConfig.PIN_LORA_NSS = LORA_CS;
    hwConfig.PIN_LORA_SCLK = LORA_SCK;
    hwConfig.PIN_LORA_MISO = LORA_MISO;
    hwConfig.PIN_LORA_MOSI = LORA_MOSI;
    hwConfig.PIN_LORA_DIO_1 = LORA_DIO1;
    hwConfig.PIN_LORA_BUSY = LORA_BUSY;
    hwConfig.RADIO_TXEN = -1;   // Not used
    hwConfig.RADIO_RXEN = -1;   // Not used
    hwConfig.USE_DIO2_ANT_SWITCH = true;
    hwConfig.USE_DIO3_TCXO = true;
    hwConfig.USE_DIO3_ANT_SWITCH = false;
    
    digitalWrite(LED_BUILTIN, LOW); // LED off after init
}

float readBatteryVoltage() {
    // Configure ADC
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 0, &adc_chars);
    
    // Take multiple readings for stability
    uint32_t voltage_sum = 0;
    for (int i = 0; i < 10; i++) {
        voltage_sum += esp_adc_cal_raw_to_voltage(adc1_get_raw(BATTERY_ADC), &adc_chars);
        delay(1);
    }
    
    float voltage = (voltage_sum / 10.0f) * BATTERY_VOLTAGE_DIVIDER / 1000.0f; // Convert to volts
    Serial.printf("   Battery: %.2fV\\n", voltage);
    return voltage;
}

// ============================================================================
// LORAWAN FUNCTIONS
// ============================================================================

bool initLoRaWAN() {
    // Initialize OTAA keys
    memcpy(nodeDeviceEUI, DEVEUI, 8);
    memcpy(nodeAppEUI, APPEUI, 8);
    memcpy(nodeAppKey, APPKEY, 16);
    
    // Initialize hardware
    uint32_t err_code = lora_hardware_init(hwConfig);
    if (err_code != 0) {
        Serial.printf("   ❌ LoRa hardware init failed: %d\\n", err_code);
        return false;
    }
    Serial.println("   LoRa hardware init OK");
    
    // Initialize LoRaWAN stack
    err_code = lmh_init(&lora_callbacks, lora_param_init, true, CLASS_A, LORAWAN_REGION);
    if (err_code != 0) {
        Serial.printf("   ❌ LoRaWAN stack init failed: %d\\n", err_code);
        return false;
    }
    Serial.println("   LoRaWAN stack init OK");
    
    // Set device keys
    lmh_setDevEui(nodeDeviceEUI);
    lmh_setAppEui(nodeAppEUI);
    lmh_setAppKey(nodeAppKey);
    Serial.println("   Device keys configured");
    
    return true;
}

void startJoinProcedure() {
    lmh_join();
    Serial.println("   Join request sent");
}

// LoRaWAN Callbacks
void lorawan_rx_handler(lmh_app_data_t *app_data) {
    Serial.printf("📥 Received downlink: port %d, size %d\\n", app_data->port, app_data->buffsize);
}

void lorawan_has_joined_handler(void) {
    Serial.println("🎉 NETWORK JOINED SUCCESSFULLY!");
    isJoined = true;
    
    // Blink LED to indicate success
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(200);
        digitalWrite(LED_BUILTIN, LOW);
        delay(200);
    }
}

void lorawan_confirm_class_handler(DeviceClass_t Class) {
    Serial.printf("📋 Device class confirmed: %d\\n", Class);
}

void lorawan_join_failed_handler(void) {
    Serial.println("❌ JOIN FAILED!");
    isJoined = false;
}

void lorawan_unconfirmed_finished(void) {
    Serial.println("📤 Unconfirmed message sent successfully");
    transmissionComplete = true;
}

void lorawan_confirmed_finished(bool result) {
    Serial.printf("📤 Confirmed message sent: %s\\n", result ? "SUCCESS" : "FAILED");
    transmissionComplete = true;
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
    
    Serial.printf("📦 Test packet #%d created (%d bytes):\\n", packetCounter, size);
    Serial.printf("   GPS: %.4f, %.4f, %dm\\n", data.latitude, data.longitude, data.altitude);
    Serial.printf("   Sats: %d, HDOP: %.1f\\n", data.satellites, data.hdop / 10.0f);
    Serial.printf("   Battery: %dmV, FW: %d.%d\\n", 
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
        // Prepare packet structure
        lmh_app_data_t app_data = {
            .buffer = buffer,
            .buffsize = size,
            .port = 1,
            .rssi = 0,
            .snr = 0
        };
        
        packetCounter++;
        Serial.printf("📤 Sending packet #%d...\\n", packetCounter);
        transmissionComplete = false;
        
        // Send the packet
        lmh_error_status result = lmh_send(&app_data, LMH_UNCONFIRMED_MSG);
        if (result == LMH_SUCCESS) {
            Serial.println("   ✅ Packet queued successfully");
            
            // Wait for transmission to complete
            unsigned long txStart = millis();
            while (!transmissionComplete && (millis() - txStart) < 30000) {
                delay(100);
            }
            
            if (transmissionComplete) {
                Serial.println("   ✅ Transmission completed successfully!");
                
                // Blink LED to indicate success
                digitalWrite(LED_BUILTIN, HIGH);
                delay(100);
                digitalWrite(LED_BUILTIN, LOW);
            } else {
                Serial.println("   ⚠️ Transmission timeout");
            }
        } else {
            Serial.printf("   ❌ Failed to queue packet: %d\\n", result);
        }
    } else {
        Serial.println("   ❌ Failed to create packet");
    }
    
    Serial.println("📡 Transmission cycle complete\\n");
}