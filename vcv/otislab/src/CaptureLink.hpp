// SPDX-License-Identifier: BSD-3-Clause
// The link between Otis Kit and Otis Capture directly to its right (docs/CAPTURE.md), through Rack's
// double-buffered expander messages: one sample per hop, whatever order Rack processes them in.
#pragma once
#include <cstdint>

namespace otis::kit {
// Capture to Kit, in the kit's right-hand expander buffers.
struct CaptureRequest {
    static constexpr uint32_t MAGIC = 0x4f4b4352; // "OKCR"
    uint32_t magic = 0;
    bool recording = false;       // high for the whole take
    int slot = 0;                 // 0-7
    int source = 0;               // 0 Capture's IN, 1 the kit's own output, 2 both
    float gain = 1, input = 0;    // input at ±1 full scale (±5 V)
};
// Kit to Capture, in Capture's left-hand expander buffers.
struct CaptureStatus {
    static constexpr uint32_t MAGIC = 0x4f4b4353; // "OKCS"
    uint32_t magic = 0;
    bool recording = false, truncated = false, noRoom = false;
    int slot = -1;                // the slot of the take being recorded, or of the last one
    int32_t frames = 0;           // its length so far
    int32_t room = 0;             // frames left for the take in progress, or the longest take that fits
    double rate = 48000;          // frames per second of takes (the host rate)
    uint32_t takes = 0;           // finished takes
};
} // namespace otis::kit
