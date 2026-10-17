Shtc3Reading readShtc3() {
  Shtc3Reading out = {false, 0, 0};
  const uint8_t wake[] = {0x35, 0x17};
  const uint8_t measure[] = {0x78, 0x66};
  const uint8_t sleep[] = {0xB0, 0x98};
  uint8_t buf[6];

  if (!i2cWriteBytes(I2C_SHTC3_ADDR, wake, sizeof(wake))) {
    i2cWriteBytes(I2C_SHTC3_ADDR, sleep, sizeof(sleep));
    return out;
  }
  delay(2);
  if (!i2cWriteBytes(I2C_SHTC3_ADDR, measure, sizeof(measure))) {
    i2cWriteBytes(I2C_SHTC3_ADDR, sleep, sizeof(sleep));
    return out;
  }
  delay(20);
  if (Wire.requestFrom(I2C_SHTC3_ADDR, 6) != 6) {
    i2cWriteBytes(I2C_SHTC3_ADDR, sleep, sizeof(sleep));
    return out;
  }
  size_t received = Wire.readBytes(buf, 6);
  bool slept = i2cWriteBytes(I2C_SHTC3_ADDR, sleep, sizeof(sleep));
  if (received != sizeof(buf) || !slept) return out;
  if (crc8Shtc3(buf, 2) != buf[2] || crc8Shtc3(buf + 3, 2) != buf[5]) {
    return out;
  }
  uint16_t rawTemp = (buf[0] << 8) | buf[1];
  uint16_t rawHum = (buf[3] << 8) | buf[4];
  out.tempC = 175.0f * rawTemp / 65536.0f - 45.0f - 4.0f;
  out.humidity = 100.0f * rawHum / 65536.0f;
  out.ok = true;
  return out;
}
