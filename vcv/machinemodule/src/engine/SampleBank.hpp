// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace md_rack {
struct SampleRecord {uint32_t address=0;std::vector<uint32_t> words;};
// Machinemodule's per-user MDS1 export, not an arbitrary flash image or WAV.
// Explicit little-endian reads work on every host; reject partial/out-of-range records.
inline std::vector<SampleRecord> readSampleBank(const std::string& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Cannot open ROM sample bank");
    auto size=file.tellg();if(size<12||size>40*1024*1024)throw std::runtime_error("Invalid ROM sample bank size");
    file.seekg(0);char magic[4];file.read(magic,4);
    if(std::string(magic,4)!="MDS1")throw std::runtime_error("Expected a Machinemodule MDS1 sample bank");
    auto u32=[&](){unsigned char b[4];if(!file.read(reinterpret_cast<char*>(b),4))throw std::runtime_error("Truncated ROM sample bank");return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);};
    std::vector<SampleRecord> result;
    uint32_t previousEnd=0x140000;
    while(true){
        uint32_t address=u32(),count=u32();
        if(count==0){if(address!=0||file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Invalid sample bank terminator");break;}
        if(address<previousEnd||address>=0x800000||count>0x800000-address)throw std::runtime_error("ROM sample record outside sample memory or overlapping");
        if(uint64_t(count)*4>uint64_t(size-file.tellg()))throw std::runtime_error("Truncated ROM sample record");
        SampleRecord record;record.address=address;record.words.resize(count);
        for(auto& word:record.words){word=u32();if(word>0xffffff)throw std::runtime_error("ROM sample word exceeds 24 bits");}
        previousEnd=address+count;result.push_back(std::move(record));
    }
    if(result.empty())throw std::runtime_error("ROM sample bank is empty");
    return result;
}
} // namespace md_rack
