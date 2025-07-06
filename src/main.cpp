/**
 * Helium Gateway Mapper - LoRaWAN Version
 * 
 * Converts Reticulum-based asset tracker to LoRaWAN for mapping
 * Helium Network gateway coverage using ChirpStack backend.
 * 
 * Hardware: Heltec Wireless Tracker V1.1 (ESP32-S3 + SX1262)
 * Network: US915 LoRaWAN via Helium Network -> ChirpStack
 * 
 * Based on original Reticulum tracker by Akita
 * LoRaWAN conversion for gateway mapping use case
 */

#include <Arduino.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_sleep.h>
#include <esp_pm.h>
#include <driver/adc.h>
#include "secrets.h"

// ============================================================================
// FIRMWARE VERSION & BUILD INFO
// ============================================================================
#ifndef FIRMWARE_VERSION_MAJOR
#define FIRMWARE_VERSION_MAJOR 2
#endif
#ifndef FIRMWARE_VERSION_MINOR  
#define FIRMWARE_VERSION_MINOR 0
#endif

const char* FIRMWARE_VERSION_STRING = "v2.0-LoRaWAN";
const char* BUILD_TARGET = "Helium Gateway Mapper";

// ============================================================================
// HARDWARE PIN DEFINITIONS - Heltec Wireless Tracker V1.1
// ============================================================================

// LoRa SX1262 Pin Mapping
#define LORA_CS     8     // SX1262 NSS
#define LORA_RST    12    // SX1262 RESET  
#define LORA_DIO1   14    // SX1262 DIO1 (IRQ)
#define LORA_SCK    9     // SPI Clock
#define LORA_MISO   11    // SPI MISO
#define LORA_MOSI   10    // SPI MOSI
#define LORA_BUSY   13    // SX1262 BUSY

// GPS UART (UC6580 GNSS)
#define GPS_TX      44    // GPS TX (connect to ESP32 RX) - V1.1 specific
#define GPS_RX      43    // GPS RX (connect to ESP32 TX) - V1.1 specific  
#define GPS_BAUD    9600

// Power Management
#define GPS_POWER   3     // GPS Power Control (V1.1 CRITICAL: GPIO3)
#define VEXT_CTRL   36    // Vext control for external peripherals
#define BATTERY_ADC 1     // ADC1_CH0 for battery voltage

// Status LED
#define LED_PIN     35    // Status LED

// ============================================================================
// LORAWAN CONFIGURATION
// ============================================================================

// OTAA Keys are now defined in secrets.h
// Configure your actual device keys there (see secrets.h.example for format)

// LMIC callbacks
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey (u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

// LoRaWAN pin mapping for LMIC - SX1262 Configuration
const lmic_pinmap lmic_pins = {
    .nss = LORA_CS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LORA_RST,
    .dio = {LORA_DIO1, LMIC_UNUSED_PIN, LMIC_UNUSED_PIN},
    .rxtx_rx_active = 0,
    .rssi_cal = 8,
    .spi_freq = 8000000,
};

// ============================================================================
// GPS & DATA STRUCTURES
// ============================================================================

TinyGPSPlus gps;
SoftwareSerial gpsSerial(GPS_RX, GPS_TX);

// Asset data structure - keeps same format as original
struct AssetData {
    double latitude;
    double longitude;
    double altitude;
    uint32_t timestamp;
    float batteryVoltage;
    uint8_t satellites;
    float hdop;
    bool validFix;
};

// ============================================================================
// CONFIGURATION & STATE MANAGEMENT
// ============================================================================

Preferences preferences;

// Configuration parameters (stored in NVS)
uint32_t sleepTimeSeconds = 300;        // 5 minutes default
uint32_t gpsTimeoutSeconds = 60;        // 1 minute GPS timeout
bool enableUnconfirmedUplinks = true;   // Use unconfirmed for mapping
uint8_t spreadingFactor = 7;            // SF7 for balance of range/battery
uint8_t txPower = 14;                   // 14 dBm (max for most regions)

// Runtime state
static osjob_t sendjob;
static bool joined = false;
static bool txComplete = false;
static uint16_t sequenceNumber = 0;
AssetData lastValidReading;

// ============================================================================
// LED STATUS INDICATION
// ============================================================================

enum class LedState {
    OFF,
    ON,
    BLINK_SLOW,      // Searching for GPS
    BLINK_FAST,      // Joining network
    BLINK_DOUBLE,    // Transmitting
    BLINK_ERROR      // Error state
};

LedState currentLedState = LedState::OFF;
unsigned long lastLedUpdate = 0;
bool ledPhysicalState = false;

void updateLed() {
    unsigned long now = millis();
    bool shouldChange = false;
    
    switch(currentLedState) {
        case LedState::OFF:
            if (ledPhysicalState) {
                digitalWrite(LED_PIN, LOW);
                ledPhysicalState = false;
            }
            break;
            
        case LedState::ON:
            if (!ledPhysicalState) {
                digitalWrite(LED_PIN, HIGH);
                ledPhysicalState = true;
            }
            break;
            
        case LedState::BLINK_SLOW:
            if (now - lastLedUpdate > 1000) {
                shouldChange = true;
            }
            break;
            
        case LedState::BLINK_FAST:
            if (now - lastLedUpdate > 200) {
                shouldChange = true;
            }
            break;
            
        case LedState::BLINK_DOUBLE:
            // Double blink pattern: ON-OFF-ON-OFF-pause
            if (now - lastLedUpdate > 100) {
                shouldChange = true;
            }
            break;
            
        case LedState::BLINK_ERROR:
            if (now - lastLedUpdate > 100) {
                shouldChange = true;
            }
            break;
    }
    
    if (shouldChange) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        ledPhysicalState = !ledPhysicalState;
        lastLedUpdate = now;
    }
}

void setLedState(LedState state) {
    currentLedState = state;
    lastLedUpdate = millis();
}

// ============================================================================
// POWER MANAGEMENT
// ============================================================================

void controlPeripherals(bool powerOn) {
    // GPS Power Control (V1.1 CRITICAL) - HIGH = ON for GPS
    digitalWrite(GPS_POWER, powerOn ? HIGH : LOW);
    
    // Vext control: LOW = ON, HIGH = OFF (inverted logic for external peripherals)
    digitalWrite(VEXT_CTRL, powerOn ? LOW : HIGH);
    
    if (powerOn) {
        delay(200); // Allow power to stabilize (GPS needs more time)
        Serial.println("Peripherals powered ON (GPS + Vext)");
    } else {
        Serial.println("Peripherals powered OFF");
    }
}

float readBatteryVoltage() {
    // Heltec Wireless Tracker has voltage divider on battery
    // Typical scaling: 3.3V ADC reading = ~4.2V battery (adjust as needed)
    uint16_t adcValue = analogRead(BATTERY_ADC);
    float voltage = (adcValue / 4095.0) * 3.3 * 2.0; // 2.0 is voltage divider factor
    return voltage;
}

// ============================================================================
// GPS FUNCTIONS
// ============================================================================

bool acquireGpsFix(AssetData* data, uint32_t timeoutMs) {
    Serial.println("Acquiring GPS fix...");
    setLedState(LedState::BLINK_SLOW);
    
    unsigned long startTime = millis();
    bool fixObtained = false;
    
    gpsSerial.begin(GPS_BAUD);
    
    while (millis() - startTime < timeoutMs && !fixObtained) {
        updateLed();
        esp_task_wdt_reset();
        
        while (gpsSerial.available() > 0) {
            if (gps.encode(gpsSerial.read())) {
                if (gps.location.isValid() && gps.date.isValid() && gps.time.isValid()) {
                    data->latitude = gps.location.lat();
                    data->longitude = gps.location.lng();
                    data->altitude = gps.altitude.meters();
                    data->satellites = gps.satellites.value();
                    data->hdop = gps.hdop.hdop();
                    data->validFix = true;
                    
                    // Create timestamp from GPS time
                    data->timestamp = millis(); // Simplified - use GPS time if needed
                    
                    fixObtained = true;
                    Serial.printf("GPS Fix: %.6f, %.6f, %.1fm, %d sats\n", 
                                data->latitude, data->longitude, data->altitude, data->satellites);
                    break;
                }
            }
        }
        
        delay(100);
    }
    
    gpsSerial.end();
    
    if (!fixObtained) {
        Serial.println("GPS fix timeout");
        data->validFix = false;
    }
    
    return fixObtained;
}

// ============================================================================
// LORAWAN EVENT HANDLING
// ============================================================================

void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            setLedState(LedState::BLINK_FAST);
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            joined = true;
            setLedState(LedState::ON);
            delay(1000);
            setLedState(LedState::OFF);
            
            // Disable link check validation (for unconfirmed uplinks)
            LMIC_setLinkCheckMode(0);
            
            // Set data rate and TX power
            LMIC_setDrTxpow(DR_SF7, txPower);
            
            Serial.printf("Joined network. DevAddr: %08X\n", LMIC.devaddr);
            break;
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            setLedState(LedState::BLINK_ERROR);
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            setLedState(LedState::BLINK_ERROR);
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            txComplete = true;
            setLedState(LedState::OFF);
            
            if (LMIC.txrxFlags & TXRX_ACK)
                Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
                Serial.printf("Received %d bytes of payload\n", LMIC.dataLen);
                // Handle downlink data if needed
            }
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
        case EV_TXSTART:
            Serial.println(F("EV_TXSTART"));
            setLedState(LedState::BLINK_DOUBLE);
            break;
        default:
            Serial.printf("Unknown event: %d\n", ev);
            break;
    }
}

// ============================================================================
// DATA TRANSMISSION
// ============================================================================

void sendAssetData(const AssetData& data) {
    if (!joined) {
        Serial.println("Not joined to network yet");
        return;
    }
    
    // Create payload (binary format for efficiency)
    uint8_t payload[32];
    uint8_t payloadLen = 0;
    
    // GPS coordinates (if valid)
    if (data.validFix) {
        // Latitude: 4 bytes (float)
        float lat = (float)data.latitude;
        memcpy(&payload[payloadLen], &lat, 4);
        payloadLen += 4;
        
        // Longitude: 4 bytes (float)
        float lon = (float)data.longitude;
        memcpy(&payload[payloadLen], &lon, 4);
        payloadLen += 4;
        
        // Altitude: 2 bytes (int16, meters)
        int16_t alt = (int16_t)data.altitude;
        payload[payloadLen++] = alt & 0xFF;
        payload[payloadLen++] = (alt >> 8) & 0xFF;
        
        // Satellites: 1 byte
        payload[payloadLen++] = data.satellites;
        
        // HDOP: 1 byte (scaled)
        payload[payloadLen++] = (uint8_t)(data.hdop * 10);
    }
    
    // Battery voltage: 1 byte (scaled to 0.1V resolution)
    payload[payloadLen++] = (uint8_t)(data.batteryVoltage * 10);
    
    // Sequence number: 2 bytes
    payload[payloadLen++] = sequenceNumber & 0xFF;
    payload[payloadLen++] = (sequenceNumber >> 8) & 0xFF;
    
    // Status flags: 1 byte
    uint8_t flags = 0;
    if (data.validFix) flags |= 0x01;
    payload[payloadLen++] = flags;
    
    // Send unconfirmed uplink
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println("OP_TXRXPEND, not sending");
    } else {
        txComplete = false;
        LMIC_setTxData2(1, payload, payloadLen, enableUnconfirmedUplinks ? 0 : 1);
        Serial.printf("Queued %d bytes for transmission\n", payloadLen);
        sequenceNumber++;
    }
}

// ============================================================================
// CONFIGURATION MANAGEMENT
// ============================================================================

void loadConfiguration() {
    preferences.begin("gateway_mapper", false);
    
    sleepTimeSeconds = preferences.getUInt("sleep_time", 300);
    gpsTimeoutSeconds = preferences.getUInt("gps_timeout", 60);
    enableUnconfirmedUplinks = preferences.getBool("unconfirmed", true);
    spreadingFactor = preferences.getUChar("sf", 7);
    txPower = preferences.getUChar("tx_power", 14);
    sequenceNumber = preferences.getUShort("seq_num", 0);
    
    preferences.end();
    
    Serial.println("Configuration loaded:");
    Serial.printf("  Sleep time: %d seconds\n", sleepTimeSeconds);
    Serial.printf("  GPS timeout: %d seconds\n", gpsTimeoutSeconds);
    Serial.printf("  Unconfirmed uplinks: %s\n", enableUnconfirmedUplinks ? "Yes" : "No");
    Serial.printf("  Spreading factor: SF%d\n", spreadingFactor);
    Serial.printf("  TX power: %d dBm\n", txPower);
    Serial.printf("  Sequence number: %d\n", sequenceNumber);
}

void saveConfiguration() {
    preferences.begin("gateway_mapper", false);
    
    preferences.putUInt("sleep_time", sleepTimeSeconds);
    preferences.putUInt("gps_timeout", gpsTimeoutSeconds);
    preferences.putBool("unconfirmed", enableUnconfirmedUplinks);
    preferences.putUChar("sf", spreadingFactor);
    preferences.putUChar("tx_power", txPower);
    preferences.putUShort("seq_num", sequenceNumber);
    
    preferences.end();
    
    Serial.println("Configuration saved to NVS");
}

// ============================================================================
// SERIAL CONFIGURATION INTERFACE
// ============================================================================

void handleSerialConfig() {
    Serial.println("\n=== Helium Gateway Mapper Configuration ===");
    Serial.println("Commands:");
    Serial.println("  sleep <seconds>     - Set sleep interval (min 30s)");
    Serial.println("  gps <seconds>       - Set GPS timeout (10-300s)");
    Serial.println("  sf <7-12>          - Set spreading factor");
    Serial.println("  power <2-20>       - Set TX power (dBm)");
    Serial.println("  confirmed <0|1>    - Enable/disable confirmed uplinks");
    Serial.println("  show               - Show current settings");
    Serial.println("  save               - Save settings to NVS");
    Serial.println("  reboot             - Restart device");
    Serial.println("  exit               - Exit configuration mode");
    Serial.println("\nEnter command:");
    
    unsigned long configStart = millis();
    while (millis() - configStart < 30000) { // 30 second timeout
        if (Serial.available()) {
            String cmd = Serial.readStringUntil('\n');
            cmd.trim();
            
            if (cmd.equalsIgnoreCase("exit")) {
                Serial.println("Exiting configuration mode");
                break;
            } else if (cmd.startsWith("sleep ")) {
                uint32_t val = cmd.substring(6).toInt();
                if (val >= 30) {
                    sleepTimeSeconds = val;
                    Serial.printf("Sleep interval set to: %d seconds\n", sleepTimeSeconds);
                } else {
                    Serial.println("Error: Sleep interval must be >= 30 seconds");
                }
            } else if (cmd.startsWith("gps ")) {
                uint32_t val = cmd.substring(4).toInt();
                if (val >= 10 && val <= 300) {
                    gpsTimeoutSeconds = val;
                    Serial.printf("GPS timeout set to: %d seconds\n", gpsTimeoutSeconds);
                } else {
                    Serial.println("Error: GPS timeout must be 10-300 seconds");
                }
            } else if (cmd.startsWith("sf ")) {
                uint8_t val = cmd.substring(3).toInt();
                if (val >= 7 && val <= 12) {
                    spreadingFactor = val;
                    Serial.printf("Spreading factor set to: SF%d\n", spreadingFactor);
                } else {
                    Serial.println("Error: Spreading factor must be 7-12");
                }
            } else if (cmd.startsWith("power ")) {
                uint8_t val = cmd.substring(6).toInt();
                if (val >= 2 && val <= 20) {
                    txPower = val;
                    Serial.printf("TX power set to: %d dBm\n", txPower);
                } else {
                    Serial.println("Error: TX power must be 2-20 dBm");
                }
            } else if (cmd.startsWith("confirmed ")) {
                uint8_t val = cmd.substring(10).toInt();
                enableUnconfirmedUplinks = (val == 0);
                Serial.printf("Confirmed uplinks: %s\n", enableUnconfirmedUplinks ? "No" : "Yes");
            } else if (cmd.equalsIgnoreCase("show")) {
                Serial.println("\nCurrent settings:");
                Serial.printf("  Sleep time: %d seconds\n", sleepTimeSeconds);
                Serial.printf("  GPS timeout: %d seconds\n", gpsTimeoutSeconds);
                Serial.printf("  Spreading factor: SF%d\n", spreadingFactor);
                Serial.printf("  TX power: %d dBm\n", txPower);
                Serial.printf("  Confirmed uplinks: %s\n", enableUnconfirmedUplinks ? "No" : "Yes");
                Serial.printf("  Battery voltage: %.2fV\n", readBatteryVoltage());
                Serial.printf("  Firmware: %s\n", FIRMWARE_VERSION_STRING);
            } else if (cmd.equalsIgnoreCase("save")) {
                saveConfiguration();
            } else if (cmd.equalsIgnoreCase("reboot")) {
                Serial.println("Rebooting...");
                ESP.restart();
            } else {
                Serial.println("Unknown command");
            }
        }
        delay(100);
    }
}

// ============================================================================
// MAIN PROGRAM
// ============================================================================

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n=== Helium Gateway Mapper ===");
    Serial.printf("Firmware: %s\n", FIRMWARE_VERSION_STRING);
    Serial.printf("Target: %s\n", BUILD_TARGET);
    
    // Initialize pins
    pinMode(LED_PIN, OUTPUT);
    pinMode(GPS_POWER, OUTPUT);   // GPS power control (V1.1 critical)
    pinMode(VEXT_CTRL, OUTPUT);   // External peripheral power
    controlPeripherals(false);    // Start with peripherals off
    
    // Initialize WDT
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);
    
    // Load configuration
    loadConfiguration();
    
    // Check for configuration mode
    Serial.println("Press any key within 5 seconds to enter configuration mode...");
    unsigned long configCheck = millis();
    while (millis() - configCheck < 5000) {
        if (Serial.available()) {
            Serial.read(); // Clear buffer
            handleSerialConfig();
            break;
        }
        delay(100);
    }
    
    // Initialize SPI for LoRa
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    
    // Initialize LMIC
    os_init();
    LMIC_reset();
    
    // Configure for US915 with FSB2 (channels 8-15 + 65)
    LMIC_selectSubBand(1);
    
    // Set initial data rate and TX power
    LMIC_setDrTxpow(DR_SF7, txPower);
    
    // Start joining
    LMIC_startJoining();
    
    Serial.println("Starting LoRaWAN join procedure...");
    setLedState(LedState::BLINK_FAST);
}

void loop() {
    // Run LMIC scheduler
    os_runloop_once();
    
    // Update LED
    updateLed();
    
    // Pet watchdog
    esp_task_wdt_reset();
    
    // Check if we should perform a tracking cycle
    static unsigned long lastTrackingCycle = 0;
    if (joined && (millis() - lastTrackingCycle > (sleepTimeSeconds * 1000))) {
        lastTrackingCycle = millis();
        
        Serial.println("Starting tracking cycle...");
        
        // Power on GPS and other peripherals
        controlPeripherals(true);
        
        // Acquire GPS data
        AssetData currentData;
        currentData.batteryVoltage = readBatteryVoltage();
        
        if (acquireGpsFix(&currentData, gpsTimeoutSeconds * 1000)) {
            lastValidReading = currentData;
            Serial.println("GPS fix acquired, transmitting data...");
            sendAssetData(currentData);
        } else {
            Serial.println("GPS fix failed, transmitting last known location...");
            currentData = lastValidReading;
            currentData.batteryVoltage = readBatteryVoltage();
            currentData.validFix = false;
            sendAssetData(currentData);
        }
        
        // Wait for transmission to complete
        while (!txComplete) {
            os_runloop_once();
            updateLed();
            esp_task_wdt_reset();
            delay(10);
        }
        
        // Power off peripherals
        controlPeripherals(false);
        
        Serial.println("Tracking cycle complete");
    }
    
    delay(10);
}