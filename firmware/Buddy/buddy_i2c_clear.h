#pragma once

// With the controller detached, release both lines, clock a slave holding
// SDA low at most nine times, then issue STOP. Drives are open-drain only.
template<typename Lines> bool buddyClearI2cBus(Lines &lines) {
  lines.releaseSda();
  lines.releaseScl();
  lines.pause();
  if (!lines.sclHigh()) return false;
  for (unsigned pulse = 0; pulse < 9 && !lines.sdaHigh(); ++pulse) {
    lines.driveSclLow();
    lines.pause();
    lines.releaseScl();
    lines.pause();
    if (!lines.sclHigh()) return false;
  }
  if (!lines.sdaHigh()) return false;
  lines.driveSclLow();
  lines.driveSdaLow();
  lines.pause();
  lines.releaseScl();
  lines.pause();
  if (!lines.sclHigh()) { lines.releaseSda(); return false; }
  lines.releaseSda();
  lines.pause();
  return lines.sdaHigh();
}
