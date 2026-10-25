// SPDX-License-Identifier: BSD-3-Clause
// Original periodic frame resampling for the ByteBusLab.LookupTable v1 format.
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace otis::frame_table {
using Table = std::array<uint8_t,256>;
enum class Channel { Left, Right, Mix };
inline std::string fingerprint(const Table& values) {
    uint64_t hash=14695981039346656037ull;
    for(uint8_t v:values) { hash^=v; hash*=1099511628211ull; }
    std::ostringstream text;
    text << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}
inline Table resample(const std::vector<int16_t>& left,const std::vector<int16_t>& right,
                      size_t begin,size_t end,Channel channel) {
    if(begin>=end || end>left.size() || end-begin<2 || end-begin>65536)
        throw std::runtime_error("Select a complete OTIS frame (2–65536 samples)");
    if((!right.empty() && right.size()!=left.size()) || (channel==Channel::Right && right.empty()))
        throw std::runtime_error("Right-channel export needs a stereo sample");
    auto at=[&](size_t i) -> double {
        if(channel==Channel::Right) return right[i];
        if(channel==Channel::Mix && !right.empty()) return (double(left[i])+right[i])*.5;
        return left[i];
    };
    Table values{}; const size_t length=end-begin;
    for(size_t i=0;i<values.size();++i) {
        // Integer numerator avoids accumulated phase error; the neighbor is periodic.
        size_t numerator=i*length, index=numerator/256;
        double a=at(begin+index), b=at(begin+(index+1)%length);
        double pcm=a+(b-a)*double(numerator%256)/256;
        values[i]=uint8_t(std::max(0.,std::min(255.,std::floor(128+pcm/256+.5))));
    }
    return values;
}
} // namespace otis::frame_table
