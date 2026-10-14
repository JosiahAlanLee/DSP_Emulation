// SPDX-License-Identifier: BSD-3-Clause
// Versioned, C-compatible link that lets one module read another module's sample
// memory across separate plugin binaries (e.g. Vastish placed to the right of OTIS).
//
// The providing module points both of its rightExpander message pointers at one
// long-lived SampleShareV1 and never requests a flip, so the pointer stays fixed.
// A reader verifies the neighbour's plugin/model slugs, then `magic` and `version`,
// before calling anything. Only plain C types cross the boundary; the reader owns
// every buffer, so no memory is allocated in one plugin and freed in another.
//
// Threading: call these from a non-audio thread (the Rack UI thread). The provider
// may take its own locks; it must never block its audio thread to serve a copy.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dspemu {

constexpr uint32_t SAMPLE_SHARE_MAGIC = 0x53484152u;  // "SHAR"
constexpr uint32_t SAMPLE_SHARE_VERSION = 1;

struct SampleShareV1 {
    uint32_t magic;
    uint32_t version;
    void* context;
    // Changes whenever the shareable sample is replaced, reworked or re-recorded.
    // An odd value means a change is in progress; wait for it to become even.
    uint32_t (*revision)(void* context);
    // Copies up to `capacity` mono 16-bit frames into `dst` (which may be null to
    // query the size) and returns the total number of frames available. Also
    // reports the sample rate and the revision the copy belongs to.
    size_t (*copy)(void* context, int16_t* dst, size_t capacity, double* rate, uint32_t* revision);
    // Writes the sample's display name (UTF-8, always NUL-terminated) into `buffer`.
    void (*name)(void* context, char* buffer, size_t size);
};

inline bool validShare(const void* p) {
    auto* s = static_cast<const SampleShareV1*>(p);
    return s && s->magic == SAMPLE_SHARE_MAGIC && s->version == SAMPLE_SHARE_VERSION && s->revision && s->copy && s->name;
}

} // namespace dspemu
