# Helium Gateway Mapper Project

## Overview

This project converts an existing Reticulum-based asset tracker into a LoRaWAN device that maps Helium Network gateway coverage. The device will drive/walk around an area and track which Helium gateways receive the LoRaWAN packets, creating a coverage map for planning network deployments.

## Hardware

- **Board**: Heltec Wireless Tracker V1.1 (ESP32-S3 + SX1262 LoRa)
- **GPS**: Built-in for location tracking  
- **Battery**: Li-Po with ADC monitoring
- **Display**: Optional OLED for status

## Network Architecture

```
Device -> Helium Hotspot -> Helium Network -> ChirpStack LNS -> Coverage Database
```

## Core Differences from Reticulum Version

| Aspect | Reticulum Original | LoRaWAN Version |
|--------|-------------------|-----------------|
| **Protocol** | Reticulum mesh networking | LoRaWAN 1.0.3 |
| **Addressing** | Reticulum destination names | DevEUI, AppEUI, AppKey |
| **Activation** | Direct addressing | OTAA (Over The Air Activation) |
| **Region** | Custom frequency | US915 (Helium standard) |
| **Network** | Mesh routing | Star topology via gateways |
| **Backend** | Direct packet processing | ChirpStack LNS |

## Key Features to Preserve

- **GPS tracking** with power management
- **Deep sleep** for battery efficiency  
- **Serial configuration** system
- **Battery monitoring** and low-power modes
- **LED status indicators**
- **Data format** (GPS, battery, timestamps)

## Data Flow

1. **Wake from deep sleep** (every N minutes)
2. **Power up GPS** and get fix
3. **Read battery voltage**
4. **Transmit LoRaWAN packet** with location data
5. **Record gateway metadata** from downlinks (if any)
6. **Power down and sleep**

## Gateway Mapping Strategy

The device will send **unconfirmed uplinks** containing:
- GPS coordinates (lat/lon)
- Battery voltage
- Device timestamp
- Sequence counter

The ChirpStack backend will log:
- Which gateways received each packet
- Signal strength (RSSI) per gateway
- SNR (Signal to Noise Ratio)
- Gateway locations
- Spreading factor and frequency used

This data creates a coverage heat map showing:
- Gateway range and overlap
- Signal quality by location
- Network planning insights

## Technical Stack

- **LoRaWAN Library**: MCCI Arduino LoRaWAN
- **Platform**: PlatformIO with ESP32 framework
- **Backend**: ChirpStack connected to Helium network
- **Frequency**: US915 (channels 8-15 + 65 for Helium)
- **Class**: Class A (battery optimized)
- **Activation**: OTAA for security

## Project Goals

1. **Convert** existing Reticulum tracker to LoRaWAN
2. **Maintain** all power management and GPS functionality  
3. **Map** Helium gateway coverage in target area
4. **Generate** coverage reports for network planning
5. **Optimize** for long battery life (target: weeks of operation)

## Configuration Parameters

The device will be configurable via serial interface:
- **Sleep interval** (tracking frequency)
- **Spreading factor** (range vs battery trade-off)
- **Payload format** (data to include)
- **DevEUI/AppEUI/AppKey** (LoRaWAN credentials)
- **Battery thresholds** (low power warnings) 