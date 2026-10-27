// SPDX-License-Identifier: AGPL-3.0-only
// The Monomachine's 22 machines in the hardware's menu order, for the MACHINE knob and the menu. A
// machine's ID is its model number (shnolk's Monomodule host::Machine, = the kit sysex value), which is
// also what a Gorgon machine lock carries.
#pragma once
#include "host/Machines.h"
#include <array>

namespace mnm_rack {
using MnmMachine=mnm::host::Machine;
constexpr std::array<MnmMachine,22> machineOrder{
    MnmMachine::GND, MnmMachine::SIN, MnmMachine::NOIS, MnmMachine::SID, MnmMachine::SAW, MnmMachine::PULS,
    MnmMachine::ENS, MnmMachine::WAVE, MnmMachine::BBOX, MnmMachine::DDRW, MnmMachine::DENS, MnmMachine::FM_STAT,
    MnmMachine::FM_PAR, MnmMachine::FM_DYN, MnmMachine::VO6, MnmMachine::THRU, MnmMachine::REVERB, MnmMachine::CHORUS,
    MnmMachine::DYNAMIX, MnmMachine::RINGMOD, MnmMachine::PHASER, MnmMachine::FLANGER};
inline int machineIdAt(int index) {return int(machineOrder[size_t(std::clamp(index,0,int(machineOrder.size())-1))]);}
inline const mnm::host::MachineDef* machineDef(int id) {
    return id<0||id>255?nullptr:mnm::host::machineDef(MnmMachine(uint8_t(id)));
}
} // namespace mnm_rack
