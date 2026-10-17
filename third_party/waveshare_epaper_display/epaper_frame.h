#pragma once

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

// Monochrome frame storage is row-major, most-significant bit first.
inline bool epaperFrameLayout(int width, int height, int bufferLength,
                              size_t *strideOut = nullptr) {
    if (width <= 0 || height <= 0 || (width & 7) != 0 || bufferLength <= 0)
        return false;
    const size_t stride = static_cast<size_t>(width / 8);
    if (static_cast<size_t>(height) > SIZE_MAX / stride) return false;
    const size_t required = stride * static_cast<size_t>(height);
    if (required > static_cast<size_t>(INT_MAX) || required != static_cast<size_t>(bufferLength))
        return false;
    if (strideOut) *strideOut = stride;
    return true;
}

inline bool epaperWritePixel(uint8_t *frame, size_t frameLength, size_t stride,
                             int width, int height, int x, int y, bool black) {
    if (!frame || width <= 0 || (width & 7) != 0 || height <= 0 || x < 0 || y < 0 ||
        x >= width || y >= height || stride != static_cast<size_t>(width / 8))
        return false;
    if (static_cast<size_t>(height) > SIZE_MAX / stride ||
        frameLength < stride * static_cast<size_t>(height)) return false;
    const size_t index = static_cast<size_t>(y) * stride + static_cast<size_t>(x / 8);
    if (index >= frameLength) return false;
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
    frame[index] = black ? static_cast<uint8_t>(frame[index] & ~mask)
                         : static_cast<uint8_t>(frame[index] | mask);
    return true;
}
