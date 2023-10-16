// Customizations for BMW f21 n13
// (2013 model year).

#if !defined(RACECHRONO_BIY_BLE_DEVICE_F21_H)
#define RACECHRONO_BIY_BLE_DEVICE_F21_H

uint8_t getUpdateRateDivider(uint32_t can_id) {
  // Throttle pedal - 25Hz
  if (can_id == 217) {
    return 1;
  }

  // Brake pedal - 100Hz (output set to 25)
  if (can_id == 239) {
    return 4;
  }

  // RPM - 100Hz
  if (can_id == 165) {
    return 1;
  }

  // Engine Oil - 10Hz (output set to 2)
  if (can_id == 1017) {
    return 5;
  }

  // Gear - 50Hz (output set to 5)
  if (can_id == 243) {
    return 10;
  }

  // Gearbox Oil - 10Hz (output set to 2)
  if (can_id == 922) {
    return 5;
  }

  // Steering angle - 25Hz
  if (can_id == 769) {
    return 1;
  }

  return DEFAULT_UPDATE_RATE_DIVIDER;
}

#endif // RACECHRONO_BIY_BLE_DEVICE_F21_H
