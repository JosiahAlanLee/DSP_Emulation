// SPDX-License-Identifier: AGPL-3.0-only
// Exercise the actual DSP recompiler on a worker, using original test code.
// No Elektron firmware is needed to check native code allocation/ABI/execution.
//
// Two directions cross the native ABI:
// - C++ calls generated code: every execJit() block entry.
// - Generated code calls C++: every peripheral access without a direct pointer,
//   which firmware does constantly (ESSI, host port, timers). Wrong argument
//   registers there deliver the wrong DSP, area, address or value. Missing
//   Win64 shadow space only corrupts the JIT's stack if the callee happens to
//   spill into its home area, so that part of the ABI stays probabilistic.
#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include <cassert>
#include <future>
#include <iostream>
#include <memory>

using dsp56k::TWord;

// A 56303 X I/O address with no device behind it: reads and writes are plain
// storage, and readAsPtr offers no pointer, so generated code must call out.
constexpr TWord SCRATCH = 0xffff9e;

struct Result { TWord x20 = 0, x21 = 0, x22 = 0, periph = 0; };

template <typename PeriphX>
static Result run(bool jit, std::initializer_list<const char*> program, TWord doneAddr, TWord doneValue) {
    using namespace dsp56k;
    DefaultMemoryValidator validator;
    // Use the voice core's supported, allocation-granularity-aligned map.
    Memory memory(validator, 0x200000, 0x800000, 0x20000);
    auto x = std::make_unique<PeriphX>();   // Peripherals56303 is large
    PeripheralsNop y;
    DSP dsp(memory, x.get(), &y);
    auto config = dsp.getJit().getConfig();
    config.linkJitBlocks = false;
    config.interruptRegionIsCode = true;
    dsp.getJit().setConfig(config);
    if (!jit) dsp.setInterpreterEnabled(true);
    Assembler assembler;
    TWord pc = 0x100;
    for (const char* source : program) {
        const auto op = assembler.assemble(source);
        if (!op.success()) std::cerr << "cannot assemble: " << source << std::endl;
        assert(op.success());
        for (unsigned i = 0; i < op.wordCount; ++i) dsp.memWriteP(pc++, op.word[i]);
    }
    dsp.setPC(0x100);
    for (int steps = 0; steps < 32 && memory.get(MemArea_X, doneAddr) != doneValue; ++steps) {
        if (jit) dsp.execJit();
        else dsp.execInterpreter();
    }
    Result r;
    r.x20 = memory.get(MemArea_X, 0x20);
    r.x21 = memory.get(MemArea_X, 0x21);
    r.x22 = memory.get(MemArea_X, 0x22);
    r.periph = static_cast<IPeripherals&>(*x).read(SCRATCH, Instruction{});
    return r;
}

template <typename PeriphX>
static Result onWorker(std::initializer_list<const char*> program, TWord doneAddr, TWord doneValue) {
    return std::async(std::launch::async, [=] { return run<PeriphX>(true, program, doneAddr, doneValue); }).get();
}

int main() {
    static_assert(dsp56k::g_useJIT, "This platform needs a native DSP JIT backend");

    // C++ -> generated code.
    {
        const auto program = {"move #$123456,x0", "move x0,x:$20", "jmp $100"};
        const auto reference = run<dsp56k::PeripheralsNop>(false, program, 0x20, 0x123456);
        assert(reference.x20 == 0x123456);
        assert(onWorker<dsp56k::PeripheralsNop>(program, 0x20, 0x123456).x20 == reference.x20);
        std::cout << "DSP JIT: worker execution matches interpreter, without firmware" << std::endl;
    }

    // Generated code -> C++. The write's address and value, and the read's address
    // and return value, all cross as call arguments. x1 holds a stale value the read
    // must replace; y0 is set before both calls and stored after them.
    {
        dsp56k::Peripherals56303 probe;
        auto& p = static_cast<dsp56k::IPeripherals&>(probe);
        p.write(SCRATCH, 0x13579b);
        assert(p.read(SCRATCH, dsp56k::Instruction{}) == 0x13579b);
        assert(p.readAsPtr(SCRATCH, dsp56k::Instruction{}) == nullptr);

        const auto program = {
            "move #$123456,x0", "move #$654321,y0", "move #$abcdef,x1",
            "move x0,x:>$ffff9e",     // absolute long address: the generic move path
            "movep x:<<$ffff9e,x1",   // short I/O address: the movep path
            "move x1,x:$22", "move y0,x:$21", "jmp $100"};
        const auto reference = run<dsp56k::Peripherals56303>(false, program, 0x21, 0x654321);
        assert(reference.periph == 0x123456 && reference.x22 == 0x123456 && reference.x21 == 0x654321);
        const auto jit = onWorker<dsp56k::Peripherals56303>(program, 0x21, 0x654321);
        if (jit.periph != reference.periph || jit.x22 != reference.x22 || jit.x21 != reference.x21)
            std::cerr << std::hex << "JIT peripheral path: stored " << jit.periph << ", read back " << jit.x22
                      << ", y0 after the calls " << jit.x21 << std::endl;
        assert(jit.periph == reference.periph);   // write: right address and value
        assert(jit.x22 == reference.x22);         // read: right address and return value
        assert(jit.x21 == reference.x21);         // a live DSP register survived both calls
        std::cout << "DSP JIT: peripheral calls out of generated code match interpreter" << std::endl;
    }
}
