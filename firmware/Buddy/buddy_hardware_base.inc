/** Convert a packed BCD byte from the RTC into a decimal value. */
uint8_t bcdToDec(uint8_t value) {
  return ((value >> 4) * 10) + (value & 0x0F);
}

/** Convert a decimal value into packed BCD for the RTC. */
uint8_t decToBcd(uint8_t value) {
  return ((value / 10) << 4) | (value % 10);
}

/** Write a raw byte buffer to an I2C device. */
bool i2cWriteBytes(uint8_t addr, const uint8_t *data, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

/** Read one or more bytes from an I2C register. */
bool i2cReadRegister(uint8_t addr, uint8_t reg, uint8_t *data, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  return Wire.requestFrom((int)addr, (int)len) == (int)len && Wire.readBytes(data, len) == len;
}

/** Write one byte to an I2C register. */
bool i2cWriteRegister(uint8_t addr, uint8_t reg, uint8_t value) {
  uint8_t buf[] = {reg, value};
  return i2cWriteBytes(addr, buf, sizeof(buf));
}

/** Read one byte from an I2C register, returning a fallback on failure. */
uint8_t i2cReadRegister8(uint8_t addr, uint8_t reg, uint8_t fallback = 0) {
  uint8_t value = fallback;
  i2cReadRegister(addr, reg, &value, 1);
  return value;
}

