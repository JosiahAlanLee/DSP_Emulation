// SPDX-License-Identifier: BSD-3-Clause
// 16-bit sample loading (mono, or stereo on request) and patch serialization shared by
// the repo's Rack plugins. No Rack headers; usable from engine tests.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dspemu {

struct PcmSample {
    std::vector<int16_t> pcm;    // mono, or a stereo sample's left channel
    std::vector<int16_t> right;  // a stereo sample's right channel; empty for mono
    double rate = 0;
    std::string name;
    bool truncated = false;
    bool stereo() const { return !right.empty(); }
};
using MonoSample = PcmSample;  // the name before stereo

inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

inline int16_t toPcm16(double x) {
    if (!std::isfinite(x)) return 0;
    return int16_t(std::lround(std::max(-32768.0, std::min(32767.0, x * 32768.0))));
}

inline std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return path.substr(slash == std::string::npos ? 0 : slash + 1);
}

// Reads PCM 8/16/24/32-bit or float 32/64 WAV (including WAVE_FORMAT_EXTENSIBLE) and keeps
// at most maxFrames frames. With keepStereo a two-channel file keeps both channels; otherwise
// channels are averaged to mono.
inline PcmSample loadWav(const std::string& path, size_t maxFrames, bool keepStereo) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open WAV");
    uint8_t header[12];
    f.read(reinterpret_cast<char*>(header), 12);
    if (!f || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4))
        throw std::runtime_error("Expected little-endian RIFF WAV");
    uint16_t format = 0, channels = 0, bits = 0, align = 0;
    uint32_t rate = 0, dataBytes = 0;
    std::streampos dataPosition = 0;
    uint8_t h[8];
    for (int n = 0; n < 10000 && f.read(reinterpret_cast<char*>(h), 8); n++) {
        uint32_t bytes = le32(h + 4);
        auto next = f.tellg() + std::streamoff(bytes + (bytes & 1));
        if (!std::memcmp(h, "fmt ", 4)) {
            if (bytes < 16 || bytes > 65536) throw std::runtime_error("Invalid WAV format chunk");
            std::vector<uint8_t> fmt(bytes);
            f.read(reinterpret_cast<char*>(fmt.data()), bytes);
            if (!f) throw std::runtime_error("Truncated WAV format");
            format = le16(fmt.data()); channels = le16(fmt.data() + 2); rate = le32(fmt.data() + 4);
            align = le16(fmt.data() + 12); bits = le16(fmt.data() + 14);
            if (format == 0xfffe && bytes >= 40) {
                const uint8_t guidTail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
                if (std::memcmp(fmt.data() + 26, guidTail, 14)) throw std::runtime_error("Unsupported WAV subtype");
                format = le16(fmt.data() + 24);
            }
        } else if (!std::memcmp(h, "data", 4)) {
            dataBytes = bytes; dataPosition = f.tellg();
        }
        f.seekg(next);
        if (format && dataPosition != std::streampos(0)) break;
    }
    if (!format || dataPosition == std::streampos(0) || channels < 1 || channels > 32 || rate < 1000 || rate > 384000)
        throw std::runtime_error("Invalid or incomplete WAV");
    if ((format != 1 && format != 3) || (format == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
        (format == 3 && bits != 32 && bits != 64))
        throw std::runtime_error("Use PCM 8/16/24/32 or float 32/64 WAV");
    size_t word = bits / 8;
    if (align != channels * word || dataBytes % align) throw std::runtime_error("Invalid WAV frame alignment");
    size_t allFrames = dataBytes / align, frames = std::min(allFrames, maxFrames);
    if (frames < 2) throw std::runtime_error("WAV needs at least two samples");
    const bool stereo = keepStereo && channels == 2;
    PcmSample out;
    out.rate = rate; out.truncated = frames < allFrames; out.name = baseName(path); out.pcm.resize(frames);
    if (stereo) out.right.resize(frames);
    f.clear(); f.seekg(dataPosition);
    std::vector<uint8_t> frame(align);
    for (size_t i = 0; i < frames; i++) {
        f.read(reinterpret_cast<char*>(frame.data()), align);
        if (!f) throw std::runtime_error("Truncated WAV sample data");
        double sum = 0;
        for (int c = 0; c < channels; c++) {
            const uint8_t* p = frame.data() + c * word;
            double x = 0;
            if (format == 3 && bits == 32) { uint32_t b = le32(p); float v; std::memcpy(&v, &b, 4); x = v; }
            else if (format == 3) { uint64_t b = uint64_t(le32(p)) | (uint64_t(le32(p + 4)) << 32); double v; std::memcpy(&v, &b, 8); x = v; }
            else if (bits == 8) x = (int(p[0]) - 128) / 128.0;
            else if (bits == 16) x = int16_t(le16(p)) / 32768.0;
            else if (bits == 24) { int32_t v = p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16); if (v & 0x800000) v -= 0x1000000; x = v / 8388608.0; }
            else x = int32_t(le32(p)) / 2147483648.0;
            if (stereo) (c ? out.right : out.pcm)[i] = toPcm16(std::isfinite(x) ? x : 0.0);
            else if (std::isfinite(x)) sum += x;
        }
        if (!stereo) out.pcm[i] = toPcm16(sum / channels);
    }
    return out;
}
inline PcmSample loadWavMono(const std::string& path, size_t maxFrames) { return loadWav(path, maxFrames, false); }

// Base64 of little-endian int16 PCM, for storing a sample inside a Rack patch.
inline std::string encodePcm16(const int16_t* pcm, size_t frames) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    s.reserve((frames * 2 + 2) / 3 * 4);
    size_t bytes = frames * 2;
    auto byte = [&](size_t i) { return uint8_t(uint16_t(pcm[i / 2]) >> ((i % 2) * 8)); };
    for (size_t i = 0; i < bytes; i += 3) {
        uint32_t v = uint32_t(byte(i)) << 16;
        if (i + 1 < bytes) v |= uint32_t(byte(i + 1)) << 8;
        if (i + 2 < bytes) v |= byte(i + 2);
        s.push_back(alphabet[(v >> 18) & 63]);
        s.push_back(alphabet[(v >> 12) & 63]);
        s.push_back(i + 1 < bytes ? alphabet[(v >> 6) & 63] : '=');
        s.push_back(i + 2 < bytes ? alphabet[v & 63] : '=');
    }
    return s;
}

inline std::vector<int16_t> decodePcm16(const std::string& s, size_t maxFrames) {
    if (s.size() % 4 || s.size() > (maxFrames * 2 + 2) / 3 * 4) throw std::runtime_error("Invalid saved sample length");
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> bytes;
    bytes.reserve(s.size() / 4 * 3);
    for (size_t i = 0; i < s.size(); i += 4) {
        int a = val(s[i]), b = val(s[i + 1]), c = val(s[i + 2]), d = val(s[i + 3]);
        bool last = i + 4 == s.size();
        if (a < 0 || b < 0 || (c < 0 && !(last && s[i + 2] == '=' && s[i + 3] == '=')) || (d < 0 && !(last && s[i + 3] == '=')))
            throw std::runtime_error("Invalid saved sample encoding");
        uint32_t v = (uint32_t(a) << 18) | (uint32_t(b) << 12) | (uint32_t(std::max(c, 0)) << 6) | uint32_t(std::max(d, 0));
        bytes.push_back(uint8_t(v >> 16));
        if (c >= 0) bytes.push_back(uint8_t(v >> 8));
        if (d >= 0) bytes.push_back(uint8_t(v));
    }
    if (bytes.size() % 2 || bytes.size() / 2 > maxFrames) throw std::runtime_error("Invalid saved PCM");
    std::vector<int16_t> pcm(bytes.size() / 2);
    for (size_t i = 0; i < pcm.size(); i++) pcm[i] = int16_t(le16(bytes.data() + i * 2));
    return pcm;
}

} // namespace dspemu
