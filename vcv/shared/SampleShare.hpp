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
#include <cstring>
#include <type_traits>

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

// ---- Stereo extension ----------------------------------------------------------------------
// A provider that supports stereo exposes a SampleShareWithStereo. Its first member is the
// SampleShareV1 that rightExpander points to, so readers that predate the extension still see a
// valid share and get mono through copy(). The V1 struct cannot say whether anything follows it,
// so a reader looks at the extension only when the provider's plugin version guarantees it
// (providerHasStereo), then checks the extension's own magic and version.
constexpr uint32_t SAMPLE_SHARE_STEREO_MAGIC = 0x53544552u;  // "STER"
constexpr uint32_t SAMPLE_SHARE_STEREO_VERSION = 1;

struct SampleShareStereoV1 {
    uint32_t magic;
    uint32_t version;
    // Like SampleShareV1::copy for both channels: up to `capacity` frames into each of `left` and
    // `right` (either may be null to query), oldest first. A mono sample is copied into both.
    // Reports whether the sample has one or two channels.
    size_t (*copyStereo)(void* context, int16_t* left, int16_t* right, size_t capacity,
                         double* rate, uint32_t* revision, uint32_t* channels);
};

struct SampleShareWithStereo {
    SampleShareV1 v1;  // first: what the expander pointer points to
    SampleShareStereoV1 stereo;
};
static_assert(std::is_standard_layout<SampleShareWithStereo>::value && offsetof(SampleShareWithStereo, v1) == 0,
              "a pointer to v1 must also point to the whole share");

// True if `version` ("2.7.0", "2.10") is at least major.minor.patch; unparsable counts as older.
inline bool versionAtLeast(const char* version, int major, int minor, int patch) {
    if (!version) return false;
    int part[3] = {0, 0, 0}, n = 0;
    const char* p = version;
    while (n < 3) {
        if (*p < '0' || *p > '9') return false;
        int v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > 100000) return false; p++; }
        part[n++] = v;
        if (*p != '.') break;
        p++;
    }
    const int want[3] = {major, minor, patch};
    for (int i = 0; i < 3; i++) if (part[i] != want[i]) return part[i] > want[i];
    return true;
}

// First releases whose share is a SampleShareWithStereo.
inline bool providerHasStereo(const char* pluginSlug, const char* pluginVersion) {
    if (!pluginSlug) return false;
    if (!std::strcmp(pluginSlug, "SpoonBenderLab")) return versionAtLeast(pluginVersion, 2, 2, 0);
    if (!std::strcmp(pluginSlug, "OtisLab")) return versionAtLeast(pluginVersion, 2, 7, 0);
    return false;
}

// The provider's stereo extension, or null (then use the mono V1 copy).
inline const SampleShareStereoV1* stereoExtension(const SampleShareV1* s, bool providerHasStereoShare) {
    if (!providerHasStereoShare || !validShare(s)) return nullptr;
    // s points to the first member of a standard-layout SampleShareWithStereo.
    const SampleShareStereoV1* x = &reinterpret_cast<const SampleShareWithStereo*>(s)->stereo;
    return x->magic == SAMPLE_SHARE_STEREO_MAGIC && x->version == SAMPLE_SHARE_STEREO_VERSION && x->copyStereo ? x : nullptr;
}

} // namespace dspemu
