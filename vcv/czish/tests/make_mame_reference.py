#!/usr/bin/env python3
"""Compile the original pinned MAME sound functions with a minimal device stub.

The tested code is extracted unchanged from reference/mame_upd933.cpp. MAME's
host scheduler/stream are not required for register and sample comparisons.
"""
from pathlib import Path

root = Path(__file__).resolve().parent
source = (root / 'reference/mame_upd933.cpp').read_text()
markers = [
    'void upd933_device::write(u8 data)',
    'u32 upd933_device::env_rate(u8 data) const',
    's16 upd933_device::update(int vnum)',
    'void upd933_device::env_t::update()',
    'void upd933_device::update_pitch_step(int vnum)',
]
prefix = r'''// Generated from the BSD-3-Clause MAME source in reference/.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
using s16=int16_t; using s32=int32_t;
template<class T> unsigned BIT(T value, int start, int length = 1) {
    return (uint32_t(value) >> start) & ((1u << length) - 1);
}
template<class... Args> void logerror(const char*, Args...) {}
struct StubStream { void update() {} };
struct StubMachine {
    uint64_t state = 0x933;
    const char* describe_context() { return "reference"; }
    uint64_t rand() {
        uint64_t z=(state+=0x9e3779b97f4a7c15ULL);
        z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
        z=(z^(z>>27))*0x94d049bb133111ebULL;
        return z^(z>>31);
    }
};
class upd933_device {
    static constexpr unsigned NOTE_SHIFT=9, PITCH_SHIFT=20, PITCH_FINE_SHIFT=12;
    static constexpr unsigned VOLUME_SHIFT=12, ENV_DCA_SHIFT=16, ENV_DCW_SHIFT=16, ENV_DCO_SHIFT=11;
    struct env_t {
        u8 m_direction=0,m_sustain=1,m_irq=0;
        u32 m_rate=0,m_target=0,m_current=0;
        void update();
    };
    struct voice_t {
        u8 m_wave[2]={0},m_window=0,m_ring_mod=0,m_pitch_mod=0,m_mute_other=0;
        u16 m_pitch=0; u32 m_position=0,m_pitch_step=0; u16 m_dcw_limit=0; s16 m_pm_level=0;
    };
    StubStream stream_; StubMachine machine_; StubStream* m_stream=&stream_;
    u8 m_cs=0,m_sound_data[2]={0},m_sound_data_pos=0;
    std::array<u16,256> m_sound_regs{};
    u16 m_cosine[0x800]{},m_pitch_fine[0x200]{},m_volume[0x200]{};
    u32 m_pitch[0x80]{},m_sample_count=0; s16 m_last_sample=0;
    std::array<voice_t,8> m_voice{};
    std::array<env_t,8> m_dca{},m_dco{},m_dcw{};
    StubMachine& machine() { return machine_; }
    u32 env_rate(u8 data) const; void update_pitch_step(int vnum); s16 update(int vnum);
public:
    upd933_device() {
        constexpr double pi=3.1415926535897932384626433832795;
        for(int i=0;i<0x800;i++) m_cosine[i]=0xfff*(1-std::cos(2.*pi*i/0x7ff))/2;
        for(int i=0;i<0x80;i++) {
            const double freq=442.0*std::pow(2,(i-62)/12.0);
            m_pitch[i]=(1<<PITCH_SHIFT)*(freq*0x800/40000);
        }
        for(int i=0;i<0x200;i++) m_pitch_fine[i]=(1<<PITCH_FINE_SHIFT)*(std::pow(2,(double)i/(12.0*0x200))-1);
        for(int i=1;i<0x200;i++) m_volume[i]=std::pow(2<<VOLUME_SHIFT,(double)i/0x1ff);
        m_volume[0]=0;
    }
    void write(u8 data);
    void registerWrite(u8 reg,u16 value) { write(reg); write(value>>8); write(value&255); }
    int16_t render() {
        s32 sum=0; for(int i:{5,0,7,2,1,4,3,6}) sum+=update(i);
        ++m_sample_count; return int16_t(std::clamp(sum,-32768,32767));
    }
};
'''
out = [prefix]
for marker in markers:
    start = source.index(marker)
    end = source.find('/**************************************************************************/', start)
    out.append(source[start:end if end != -1 else len(source)])
(root / 'mame_ref_generated.hpp').write_text('\n'.join(out))
