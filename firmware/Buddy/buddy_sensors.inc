/** Convert a calendar date into days since the sketch's epoch base. */
uint32_t daysFromCivil(uint16_t y, uint8_t m, uint8_t d) {
  y -= m <= 2;
  const uint16_t era = y / 400;
  const uint16_t yoe = y - era * 400;
  const uint16_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365UL + yoe / 4 - yoe / 100 + doy;
  return era * 146097UL + doe - 730425UL;
}

/** Check whether an RTC date is within the supported sane range. */
bool validDate(const RtcDateTime &dt) {
  return dt.year >= 2025 && dt.year <= 2099 && dt.month >= 1 && dt.month <= 12 && dt.day >= 1 && dt.day <= 31;
}

/** Return true when the given year has a February 29. */
bool isLeapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/** Return the number of days in a month, including leap years. */
uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return days[month - 1];
}

/** Add minutes to a date/time value, rolling over hour/day/month/year. */
void addMinutes(RtcDateTime &dt, uint8_t minutes) {
  dt.minute += minutes;
  while (dt.minute >= 60) {
    dt.minute -= 60;
    dt.hour++;
  }
  while (dt.hour >= 24) {
    dt.hour -= 24;
    dt.day++;
  }
  if (dt.month >= 1 && dt.month <= 12 && dt.day > daysInMonth(dt.year, dt.month)) {
    dt.day = 1;
    dt.month++;
    if (dt.month > 12) {
      dt.month = 1;
      dt.year++;
    }
  }
}

/** Convert a date/time into seconds since the sketch's epoch base. */
uint32_t toEpoch(const RtcDateTime &dt) {
  if (!validDate(dt)) {
    return millis() / 1000UL;
  }
  return daysFromCivil(dt.year, dt.month, dt.day) * 86400UL +
         dt.hour * 3600UL + dt.minute * 60UL + dt.second;
}

/** Convert a date into a stable day number. */
uint32_t todayOrdinal(const RtcDateTime &dt) {
  if (!validDate(dt)) {
    return millis() / 86400000UL;
  }
  return daysFromCivil(dt.year, dt.month, dt.day);
}

/** Read the PCF85063 RTC into a RtcDateTime structure. */
bool readRtc(RtcDateTime &dt) {
  uint8_t buf[7];
  if (!i2cReadRegister(I2C_RTC_ADDR, 0x04, buf, sizeof(buf))) {
    return false;
  }
  dt.second = bcdToDec(buf[0] & 0x7F);
  dt.minute = bcdToDec(buf[1] & 0x7F);
  dt.hour = bcdToDec(buf[2] & 0x3F);
  dt.day = bcdToDec(buf[3] & 0x3F);
  dt.month = bcdToDec(buf[5] & 0x1F);
  dt.year = 2000 + bcdToDec(buf[6]);
  return validDate(dt);
}

/** Write a RtcDateTime value into the PCF85063 RTC. */
bool setRtc(const RtcDateTime &dt) {
  uint8_t buf[] = {
    0x04,
    decToBcd(dt.second),
    decToBcd(dt.minute),
    decToBcd(dt.hour),
    decToBcd(dt.day),
    0,
    decToBcd(dt.month),
    decToBcd(dt.year - 2000),
  };
  return i2cWriteBytes(I2C_RTC_ADDR, buf, sizeof(buf));
}

/** Convert the three-letter C/C++ build month into a numeric month. */
uint8_t monthFromBuildName(const char *name) {
  if (strncmp(name, "Jan", 3) == 0) return 1;
  if (strncmp(name, "Feb", 3) == 0) return 2;
  if (strncmp(name, "Mar", 3) == 0) return 3;
  if (strncmp(name, "Apr", 3) == 0) return 4;
  if (strncmp(name, "May", 3) == 0) return 5;
  if (strncmp(name, "Jun", 3) == 0) return 6;
  if (strncmp(name, "Jul", 3) == 0) return 7;
  if (strncmp(name, "Aug", 3) == 0) return 8;
  if (strncmp(name, "Sep", 3) == 0) return 9;
  if (strncmp(name, "Oct", 3) == 0) return 10;
  if (strncmp(name, "Nov", 3) == 0) return 11;
  if (strncmp(name, "Dec", 3) == 0) return 12;
  return 1;
}

/** Build the RTC seed time from compile date/time plus the upload offset. */
RtcDateTime buildDateTime() {
  const char *date = __DATE__;
  const char *time = __TIME__;
  RtcDateTime dt;
  dt.month = monthFromBuildName(date);
  dt.day = atoi(date + 4);
  dt.year = atoi(date + 7);
  dt.hour = atoi(time);
  dt.minute = atoi(time + 3);
  dt.second = atoi(time + 6);
  addMinutes(dt, 1);
  return dt;
}

/** Return a compact timestamp for detecting a newly uploaded firmware build. */
uint32_t buildStamp() {
  RtcDateTime dt = buildDateTime();
  return toEpoch(dt);
}

/** Return the current RTC time, or a fixed fallback if RTC read fails. */
RtcDateTime nowDateTime() {
  RtcDateTime dt = {2026, 5, 29, 12, 0, 0};
  if (readRtc(dt)) {
    rtcAvailable = true;
    return dt;
  }
  rtcAvailable = false;
  return dt;
}

/** Calculate the SHTC3 CRC-8 used to validate sensor response bytes. */
uint8_t crc8Shtc3(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
    }
  }
  return crc;
}

/** Wake, read, validate, and sleep the SHTC3 temperature/humidity sensor. */
#include "buddy_shtc3_acquisition.inc"

/** Read the battery voltage through the board's switched voltage divider. */
#include "buddy_battery_acquisition.inc"

/** Convert measured LiPo voltage into a simple percentage estimate. */
uint8_t batteryPercent(float voltage) {
  if (voltage <= 3.30f) {
    return 0;
  }
  if (voltage >= 4.20f) {
    return 100;
  }
  return (uint8_t)(((voltage - 3.30f) * 100.0f / 0.90f) + 0.5f);
}

/** Read the FT6336 touch controller when present. */
TouchPoint readTouch() {
  TouchPoint point = {false, 0, 0};
  if (!touchAvailable) {
    return point;
  }
  uint8_t buf[5];
  if (!i2cReadRegister(I2C_FT6336_ADDR, 0x02, buf, sizeof(buf))) {
    return point;
  }
  if ((buf[0] & 0x0F) == 0) {
    return point;
  }
  point.x = ((buf[1] & 0x0F) << 8) | buf[2];
  point.y = ((buf[3] & 0x0F) << 8) | buf[4];
  point.ok = point.x < EPD_WIDTH && point.y < EPD_HEIGHT;
  return point;
}

/** Probe the optional FT6336 touch controller after a reset pulse. */
void detectTouch() {
  pinMode(EPD_TP_RST_PIN, OUTPUT);
  digitalWrite(EPD_TP_RST_PIN, LOW);
  delay(10);
  digitalWrite(EPD_TP_RST_PIN, HIGH);
  delay(80);
  Wire.beginTransmission(I2C_FT6336_ADDR);
  touchAvailable = Wire.endTransmission() == 0;
}
