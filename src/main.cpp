/**
 * Helium Gateway Mapper - SX126x-Arduino Implementation
 * 
 * Hardware: Heltec Wireless Tracker V1.1 (ESP32-S3 + SX1262)
 * Network: US915 LoRaWAN via Helium Network -> ChirpStack
 * 
 * Library: SX126x-Arduino by beegee-tokyo (native SX1262 support)
 * Eliminates hal_failed() crashes with proper BUSY pin handling
 */

#include <Arduino.h>
#include <SX126x-Arduino.h>
#include <LoRaWan-Arduino.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_sleep.h>
#include "secrets.h"

// ============================================================================
// FIRMWARE VERSION
// ============================================================================
#define FW_VERSION "1.0.0"

// ============================================================================
// HARDWARE DEFINITIONS (Heltec Wireless Tracker V1.1)
// ============================================================================
// SX1262 LoRa pins
#define LORA_CS      8    // NSS (Chip Select)
#define LORA_RST     12   // Reset
#define LORA_DIO1    14   // DIO1 (IRQ)
#define LORA_BUSY    13   // BUSY pin (critical for SX1262)
#define LORA_SCLK    9    // SPI Clock
#define LORA_MISO    11   // SPI MISO
#define LORA_MOSI    10   // SPI MOSI

// GPS pins (V1.1 specific)
#define GPS_RX       43   // GPS UART RX
#define GPS_TX       44   // GPS UART TX
#define GPS_POWER    3    // GPS power control (V1.1 specific)

// Status LEDs (remove duplicate definition)
// #define LED_BUILTIN  35  // Already defined in board variant

// ============================================================================
// LORAWAN CONFIGURATION
// ============================================================================
// Region and frequency band
#define LORAWAN_REGION    LORAMAC_REGION_US915
#define LORAWAN_SUBBAND   2  // US915 FSB2 (channels 8-15 + 65) for Helium

// Device configuration
#define LORAWAN_CLASS     CLASS_A
#define LORAWAN_ADR       true
#define LORAWAN_PUBLIC    true
#define LORAWAN_DUTY_CYCLE false
#define LORAWAN_DATARATE  DR_3
#define LORAWAN_TX_POWER  14  // dBm
#define LORAWAN_CONFIRMED false

// Timing
#define SEND_INTERVAL     120000  // 2 minutes
#define JOIN_RETRY_DELAY  30000   // 30 seconds

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
// GPS
TinyGPSPlus gps;
SoftwareSerial gpsSerial(GPS_RX, GPS_TX);

// Storage
Preferences preferences;

// Hardware configuration for SX126x-Arduino
hw_config hwConfig;

// LoRaWAN parameters
lmh_param_t lora_param_init = {
    LORAWAN_ADR,
    LORAWAN_DATARATE,
    LORAWAN_PUBLIC,
    3,  // Join trials
    LORAWAN_TX_POWER,
    LORAWAN_DUTY_CYCLE
};

// Device credentials (from secrets.h)
uint8_t nodeDeviceEUI[8];
uint8_t nodeAppEUI[8]; 
uint8_t nodeAppKey[16];

// ============================================================================
// GLOBAL STATE
// ============================================================================
struct GPSData {
    float latitude = 0.0;
    float longitude = 0.0;
    float altitude = 0.0;
    float hdop = 0.0;
    uint8_t satellites = 0;
    bool valid = false;
};

struct TrackerState {
    GPSData gps;
    float batteryVoltage = 0.0;
    uint32_t lastTransmission = 0;
    bool joined = false;
    bool transmissionActive = false;
    uint32_t transmissionStartTime = 0;
    uint16_t packetCounter = 0;
    uint8_t joinRetries = 0;
} state;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================
// Hardware
void initHardware();
void initGPS();
void initLoRa();
void controlPeripherals(bool enable);

// GPS
bool readGPS();
void powerGPS(bool enable);

// Power
float readBatteryVoltage();

// LoRaWAN
void startJoinProcedure();
bool createDataPacket(uint8_t* buffer, uint8_t* size);

// Status
void updateLED();
void printStatus();

// LoRaWAN callback implementations (Board functions provided by SX126x-Arduino library)
void lorawan_rx_handler(lmh_app_data_t *app_data);
void lorawan_has_joined_handler(void);
void lorawan_confirm_class_handler(DeviceClass_t Class);
void lorawan_join_failed_handler(void);
void lorawan_unconfirmed_finished(void);
void lorawan_confirmed_finished(bool result);

// ============================================================================
// LORAWAN CALLBACKS
// ============================================================================
static lmh_callback_t lora_callbacks = {
    BoardGetBatteryLevel,
    BoardGetUniqueId, 
    BoardGetRandomSeed,
    lorawan_rx_handler,
    lorawan_has_joined_handler,
    lorawan_confirm_class_handler,
    lorawan_join_failed_handler,
    lorawan_unconfirmed_finished,
    lorawan_confirmed_finished
};

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("=====================================");
    Serial.println("   Helium Gateway Mapper - SX126x");
    Serial.println("=====================================");
    Serial.printf("Firmware: %s\n", FW_VERSION);
    Serial.printf("Target: Heltec Wireless Tracker V1.1\n");
    Serial.printf("Chip: ESP32-S3 + SX1262\n");
    Serial.println("=====================================");

    // Initialize hardware
    initHardware();
    
    // Initialize GPS
    initGPS();
    
    // Initialize LoRa/LoRaWAN
    initLoRa();
    
    // Print initial status
    printStatus();
    
    // Start join procedure if not already joined
    if (!state.joined) {
        Serial.println("Starting LoRaWAN join procedure...");
        startJoinProcedure();
    }
    
    Serial.println("Setup complete. Entering main loop...");
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    // Update GPS data
    readGPS();
    
    // Update battery voltage
    state.batteryVoltage = readBatteryVoltage();
    
    // Handle transmission timing
    if (state.joined && !state.transmissionActive) {
        if (millis() - state.lastTransmission > SEND_INTERVAL) {
            // Time to send a packet
            if (state.gps.valid) {
                uint8_t buffer[32];
                uint8_t size;
                
                if (createDataPacket(buffer, &size)) {
                    Serial.println("\n--- Sending LoRaWAN Packet ---");
                    Serial.printf("GPS: %.6f,%.6f Alt:%.1fm HDOP:%.1f Sats:%d\n",
                        state.gps.latitude, state.gps.longitude, 
                        state.gps.altitude, state.gps.hdop, state.gps.satellites);
                    Serial.printf("Battery: %.2fV\n", state.batteryVoltage);
                    Serial.printf("Packet size: %d bytes\n", size);
                    
                    // Prepare packet structure
                    lmh_app_data_t app_data = {
                        .buffer = buffer,
                        .buffsize = size,
                        .port = 1,
                        .rssi = 0,
                        .snr = 0
                    };
                    
                    // Send the packet
                    lmh_error_status result = lmh_send(&app_data, LMH_UNCONFIRMED_MSG);
                    if (result == LMH_SUCCESS) {
                        state.transmissionActive = true;
                        state.transmissionStartTime = millis();
                        state.packetCounter++;
                        Serial.println("Packet queued for transmission");
                    } else {
                        Serial.printf("Failed to queue packet: %d\n", result);
                    }
                } else {
                    Serial.println("Failed to create data packet");
                }
            } else {
                Serial.println("Skipping transmission - no valid GPS fix");
                state.lastTransmission = millis(); // Reset timer
            }
        }
    }
    
    // Handle join retry timing
    if (!state.joined && (millis() > JOIN_RETRY_DELAY * (state.joinRetries + 1))) {
        Serial.printf("Join retry %d...\n", state.joinRetries + 1);
        startJoinProcedure();
        state.joinRetries++;
        
        if (state.joinRetries >= 10) {
            Serial.println("Too many join failures, restarting...");
            ESP.restart();
        }
    }
    
    // Handle transmission timeout
    if (state.transmissionActive && (millis() - state.transmissionStartTime > 30000)) {
        Serial.println("Transmission timeout, resetting state");
        state.transmissionActive = false;
        state.lastTransmission = millis();
    }
    
    // Update status LED
    updateLED();
    
    // Brief delay to prevent watchdog issues
    delay(100);
}

// ============================================================================
// HARDWARE INITIALIZATION
// ============================================================================
void initHardware() {
    Serial.println("Initializing hardware...");
    
    // Configure LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    
    // Initialize preferences
    preferences.begin("tracker", false);
    
    // Control peripherals (start with GPS on for initial fix)
    controlPeripherals(true);
    
    Serial.println("Hardware initialization complete");
}

void initGPS() {
    Serial.println("Initializing GPS...");
    
    // Power on GPS
    powerGPS(true);
    delay(100);
    
    // Start GPS serial
    gpsSerial.begin(9600);
    
    Serial.println("GPS initialization complete");
}

void initLoRa() {
    Serial.println("Initializing LoRa/LoRaWAN...");
    
    // Initialize device credentials from secrets.h
    memcpy(nodeDeviceEUI, DEVEUI, 8);
    memcpy(nodeAppEUI, APPEUI, 8);
    memcpy(nodeAppKey, APPKEY, 16);
    
    // Configure hardware for SX126x-Arduino library
    hwConfig.CHIP_TYPE = SX1262_CHIP;
    hwConfig.PIN_LORA_RESET = LORA_RST;
    hwConfig.PIN_LORA_NSS = LORA_CS;
    hwConfig.PIN_LORA_SCLK = LORA_SCLK;
    hwConfig.PIN_LORA_MISO = LORA_MISO;
    hwConfig.PIN_LORA_DIO_1 = LORA_DIO1;
    hwConfig.PIN_LORA_BUSY = LORA_BUSY;
    hwConfig.PIN_LORA_MOSI = LORA_MOSI;
    hwConfig.RADIO_TXEN = -1;      // Not used on this board
    hwConfig.RADIO_RXEN = -1;      // Not used on this board
    hwConfig.USE_DIO2_ANT_SWITCH = false;
    hwConfig.USE_DIO3_TCXO = true;  // Heltec boards use DIO3 for TCXO
    hwConfig.USE_DIO3_ANT_SWITCH = false;
    hwConfig.USE_LDO = false;       // Use DCDC converter
    hwConfig.USE_RXEN_ANT_PWR = false;
    
    // Initialize LoRa hardware
    if (lora_hardware_init(hwConfig) != 0) {
        Serial.println("ERROR: LoRa hardware initialization failed!");
        return;
    }
    
    // Set up credentials
    lmh_setDevEui(nodeDeviceEUI);
    lmh_setAppEui(nodeAppEUI);
    lmh_setAppKey(nodeAppKey);
    
    // Initialize LoRaWAN
    if (lmh_init(&lora_callbacks, lora_param_init, true, LORAWAN_CLASS, LORAWAN_REGION) != LMH_SUCCESS) {
        Serial.println("ERROR: LoRaWAN initialization failed!");
        return;
    }
    
    // Set subband for US915 (Helium uses FSB2)
    if (!lmh_setSubBandChannels(LORAWAN_SUBBAND)) {
        Serial.println("ERROR: Failed to set subband!");
        return;
    }
    
    Serial.println("LoRa/LoRaWAN initialization complete");
}

// ============================================================================
// GPS FUNCTIONS
// ============================================================================
bool readGPS() {
    bool newData = false;
    
    while (gpsSerial.available()) {
        if (gps.encode(gpsSerial.read())) {
            newData = true;
        }
    }
    
    if (newData && gps.location.isValid()) {
        state.gps.latitude = gps.location.lat();
        state.gps.longitude = gps.location.lng();
        state.gps.altitude = gps.altitude.meters();
        state.gps.hdop = gps.hdop.hdop();
        state.gps.satellites = gps.satellites.value();
        state.gps.valid = true;
        return true;
    }
    
    return false;
}

void powerGPS(bool enable) {
    digitalWrite(GPS_POWER, enable ? HIGH : LOW);
    if (enable) {
        delay(100); // Allow GPS module to stabilize
    }
}

// ============================================================================
// POWER MANAGEMENT
// ============================================================================
void controlPeripherals(bool enable) {
    // For now, just control GPS power
    powerGPS(enable);
}

float readBatteryVoltage() {
    // ESP32-S3 ADC reading for battery voltage
    // This may need calibration based on actual hardware
    uint32_t raw = analogRead(A0);
    float voltage = (raw * 3.3 * 2.0) / 4095.0; // Assuming 2:1 voltage divider
    return voltage;
}

// ============================================================================
// LORAWAN FUNCTIONS
// ============================================================================
void startJoinProcedure() {
    lmh_join();
    Serial.println("Join procedure started...");
}

bool createDataPacket(uint8_t* buffer, uint8_t* size) {
    if (!state.gps.valid) {
        return false;
    }
    
    // Create binary packet (16 bytes total)
    // Format: [Lat(4)] [Lon(4)] [Alt(2)] [HDOP(2)] [Sats(1)] [Battery(2)] [Counter(1)]
    
    int32_t lat = (int32_t)(state.gps.latitude * 1e7);
    int32_t lon = (int32_t)(state.gps.longitude * 1e7);
    int16_t alt = (int16_t)(state.gps.altitude);
    uint16_t hdop = (uint16_t)(state.gps.hdop * 100);
    uint8_t sats = state.gps.satellites;
    uint16_t battery = (uint16_t)(state.batteryVoltage * 100);
    uint8_t counter = state.packetCounter & 0xFF;
    
    uint8_t idx = 0;
    
    // Latitude (4 bytes, little endian)
    buffer[idx++] = (lat >> 0) & 0xFF;
    buffer[idx++] = (lat >> 8) & 0xFF;
    buffer[idx++] = (lat >> 16) & 0xFF;
    buffer[idx++] = (lat >> 24) & 0xFF;
    
    // Longitude (4 bytes, little endian)  
    buffer[idx++] = (lon >> 0) & 0xFF;
    buffer[idx++] = (lon >> 8) & 0xFF;
    buffer[idx++] = (lon >> 16) & 0xFF;
    buffer[idx++] = (lon >> 24) & 0xFF;
    
    // Altitude (2 bytes, little endian)
    buffer[idx++] = (alt >> 0) & 0xFF;
    buffer[idx++] = (alt >> 8) & 0xFF;
    
    // HDOP (2 bytes, little endian)
    buffer[idx++] = (hdop >> 0) & 0xFF;
    buffer[idx++] = (hdop >> 8) & 0xFF;
    
    // Satellites (1 byte)
    buffer[idx++] = sats;
    
    // Battery voltage (2 bytes, little endian)
    buffer[idx++] = (battery >> 0) & 0xFF;
    buffer[idx++] = (battery >> 8) & 0xFF;
    
    // Packet counter (1 byte)
    buffer[idx++] = counter;
    
    *size = idx;
    return true;
}

// ============================================================================
// STATUS FUNCTIONS
// ============================================================================
void updateLED() {
    static uint32_t lastBlink = 0;
    static bool ledState = false;
    
    if (millis() - lastBlink > 1000) {
        if (state.joined) {
            // Slow blink when joined
            digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
            ledState = !ledState;
        } else {
            // Fast blink when joining
            digitalWrite(LED_BUILTIN, (millis() / 200) % 2 ? HIGH : LOW);
        }
        lastBlink = millis();
    }
}

void printStatus() {
    Serial.println("\n--- Tracker Status ---");
    Serial.printf("Joined: %s\n", state.joined ? "Yes" : "No");
    Serial.printf("GPS Valid: %s\n", state.gps.valid ? "Yes" : "No");
    if (state.gps.valid) {
        Serial.printf("GPS: %.6f,%.6f Alt:%.1fm\n", 
            state.gps.latitude, state.gps.longitude, state.gps.altitude);
        Serial.printf("HDOP: %.1f Satellites: %d\n", state.gps.hdop, state.gps.satellites);
    }
    Serial.printf("Battery: %.2fV\n", state.batteryVoltage);
    Serial.printf("Packets sent: %d\n", state.packetCounter);
    Serial.println("---------------------\n");
}

// ============================================================================
// LORAWAN CALLBACK IMPLEMENTATIONS  
// ============================================================================
// Note: BoardGetBatteryLevel(), BoardGetUniqueId(), and BoardGetRandomSeed() 
// are provided by the SX126x-Arduino library

void lorawan_rx_handler(lmh_app_data_t *app_data) {
    Serial.println("LoRa packet received:");
    Serial.printf("Port: %d\n", app_data->port);
    Serial.printf("Size: %d\n", app_data->buffsize);
    Serial.print("Data: ");
    for (int i = 0; i < app_data->buffsize; i++) {
        Serial.printf("%02X ", app_data->buffer[i]);
    }
    Serial.println();
}

void lorawan_has_joined_handler(void) {
    Serial.println("✅ OTAA join succeeded!");
    state.joined = true;
    state.joinRetries = 0;
    state.lastTransmission = millis(); // Reset transmission timer
    printStatus();
}

void lorawan_confirm_class_handler(DeviceClass_t Class) {
    Serial.printf("Class changed to: %c\n", "ABC"[Class]);
}

void lorawan_join_failed_handler(void) {
    Serial.println("❌ OTAA join failed");
    state.joined = false;
    // Will retry automatically in main loop
}

void lorawan_unconfirmed_finished(void) {
    Serial.println("Unconfirmed packet transmission finished");
    state.transmissionActive = false;
    state.lastTransmission = millis();
}

void lorawan_confirmed_finished(bool result) {
    if (result) {
        Serial.println("✅ Confirmed packet transmission successful (ACK received)");
    } else {
        Serial.println("❌ Confirmed packet transmission failed (no ACK)");
    }
    state.transmissionActive = false;
    state.lastTransmission = millis();
}