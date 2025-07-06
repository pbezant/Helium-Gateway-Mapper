/**
 * Project-specific LMIC configuration for Helium Gateway Mapper
 * 
 * This file explicitly configures the LMIC library for:
 * - SX1262 radio (not SX1276)
 * - US915 frequency band
 * - Helium Network compatibility
 */

#ifndef _LMIC_PROJECT_CONFIG_H_
#define _LMIC_PROJECT_CONFIG_H_

// Select target radio - CRITICAL for SX1262
#define CFG_sx1262_radio 1

// Select LoRaWAN regional band  
#define CFG_us915 1

// Disable unused regions to save flash space
#define CFG_LMIC_US_like 1

// Disable other regions
#undef CFG_eu868
#undef CFG_eu433
#undef CFG_as923
#undef CFG_au915
#undef CFG_in866
#undef CFG_kr920

// Enable long messages (for GPS data)
#define LMIC_ENABLE_long_messages 1

// Enable device time requests
#define LMIC_ENABLE_DeviceTimeReq 1

// Disable features not needed for Class A uplink-only operation
#define DISABLE_PING 1
#define DISABLE_BEACONS 1
#define DISABLE_JOIN 0

// Configure for battery operation
#define LMIC_ENABLE_event_logging 0  // Disable to save power

// Debug configuration (disable for production)
#define LMIC_DEBUG_LEVEL 0

// Helium Network specific settings
#define LMIC_ENABLE_arbitrary_clock_error 1

#endif // _LMIC_PROJECT_CONFIG_H_ 