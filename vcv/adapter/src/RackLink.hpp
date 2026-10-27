// SPDX-License-Identifier: AGPL-3.0-only
// The Rack side shared by the emulated instruments: the sound-link packet from a Gorgon on the left,
// and a track's CV from a mono or polyphonic cable. Include after rack.hpp.
#pragma once
#include "Scheduling.hpp"

namespace emu_rack {
// The live sound-link packet in `consumer` (the receiver's buffer), or null. Only a GorgonBridge 2.x
// Gorgon writes it: an older Gorgon has no protocol, and a future major would need negotiation.
inline const gorgon_sound::Packet* soundPacket(const rack::engine::Module* provider, const void* consumer, int64_t now) {
    if(!provider||!provider->model||!provider->model->plugin||provider->model->plugin->slug!="GorgonBridge"||provider->model->slug!="Gorgon")return nullptr;
    const auto& version=provider->model->plugin->version;
    if(version.rfind("2.",0)!=0)return nullptr;
    const auto* packet=static_cast<const gorgon_sound::Packet*>(consumer);
    return packet&&gorgon_sound::valid(*packet,now)?packet:nullptr;
}
// Track `track`'s volts from `input`: a mono cable reaches every track, a polyphonic one gives track n
// its channel n and nothing past its width.
inline float trackCv(rack::engine::Input& input, int track) {
    int n=input.getChannels();if(!n||(n>1&&track>=n))return 0;
    return finite(input.getVoltage(n==1?0:track),-100,100);
}
} // namespace emu_rack
