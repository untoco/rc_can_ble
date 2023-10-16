#if !defined(RACECHRONO_BIY_BLE_DEVICE_CONFIG_H)
#define RACECHRONO_BIY_BLE_DEVICE_CONFIG_H

// Change the value to customize the name of your device.
#define DEVICE_NAME "BLE CAN device"
#define EZSBC_BOARD

// OTA updates over wifi if the OTA_UPDATES flag is defined.
// At bootup the device will search for the defined SSID and start up a webserver upon connection.
// The new .bin file can be uploaded by navigating to http://<ip address>/update

//#define OTA_UPDATES
//#define OTA_WIFI_SSID "SSID"
//#define OTA_WIFI_PASSWORD "PASSWORD"

// We use RaceChronoPidMap to keep track of stuff for each CAN ID.
// In this implementation, we're going to ignore "update intervals" requested by
// RaceChrono, and instead send every Nth CAN message we receive, per CAN ID, where
// N is different for different PIDs.
const uint8_t DEFAULT_UPDATE_RATE_DIVIDER = 10;

// You need to pick one of the provided configurations below, or define your own
// configuration. Your configuration should define the following constant and
// function:

// Defines the baud rate to use for the CAN bus. For example, 500k baud rate
// should be written as 500 * 1e3.
extern const long BAUD_RATE;  // 500k.

// Returns an "update rate divider for a given CAN ID.
// If the value is N, only every Nth message received from the CAN bus will be
// communicated to RaceChrono via BLE.
uint8_t getUpdateRateDivider(uint32_t can_id);

#include "f21.h"

#endif // RACECHRONO_BIY_BLE_DEVICE_CONFIG_H
