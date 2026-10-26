// SPDX-License-Identifier: AGPL-3.0-only
// Exercise the actual DSP recompiler on a worker, using original test code.
// No Elektron firmware is needed to check native code allocation/ABI/execution.
#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include <cassert>
#include <future>
#include <iostream>

static dsp56k::TWord run(bool jit) {
    using namespace dsp56k;
    DefaultMemoryValidator validator;
    // Use the voice core's supported, allocation-granularity-aligned map.
    Memory memory(validator, 0x200000, 0x800000, 0x20000);
    PeripheralsNop x, y;
    DSP dsp(memory, &x, &y);
    auto config = dsp.getJit().getConfig();
    config.linkJitBlocks = false;
    config.interruptRegionIsCode = true;
    dsp.getJit().setConfig(config);
    if (!jit) dsp.setInterpreterEnabled(true);
    Assembler assembler;
    TWord pc = 0x100;
    for (const char* source : {"move #$123456,x0", "move x0,x:$20", "jmp $100"}) {
        const auto op = assembler.assemble(source);
        assert(op.success());
        for (unsigned i = 0; i < op.wordCount; ++i) dsp.memWriteP(pc++, op.word[i]);
    }
    dsp.setPC(0x100);
    for (int steps = 0; steps < 16 && memory.get(MemArea_X, 0x20) != 0x123456; ++steps) {
        if (jit) dsp.execJit();
        else dsp.execInterpreter();
    }
    return memory.get(MemArea_X, 0x20);
}

int main() {
    static_assert(dsp56k::g_useJIT, "This platform needs a native DSP JIT backend");
    const auto reference = run(false);
    assert(reference == 0x123456);
    auto worker = std::async(std::launch::async, [] { return run(true); });
    assert(worker.get() == reference);
    std::cout << "DSP JIT: worker execution matches interpreter, without firmware\n";
}
