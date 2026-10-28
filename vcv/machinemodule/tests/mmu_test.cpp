// SPDX-License-Identifier: AGPL-3.0-only
// dsp56300's MmuArray gives each JIT cache block private memory on first use by remapping it in place. The remap
// must never leave a hole in the address space: another thread's mmap (asmjit code pages, malloc, a second
// emulator in the same process) can land in the hole, and the MAP_FIXED that follows silently replaces that
// thread's memory with the cache block. Both then use the same pages, and whichever unmaps first pulls them from
// under the other. Rack crashed this way when Machinemodule and Monomodule loaded together.
//
// One thread fills MMU arrays block by block while others keep mapping pages of the same size. Nothing is
// freed, so the arrays' holes (if any) are the highest free gaps and the kernel's top-down placement picks them.
// A neighbour page inside an array's range, or a stamp or fill that changed, means a remap opened a hole.
#include "dsp56kBase/mmuarray.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#if !defined(_WIN32) && !defined(__APPLE__)
#include <sys/mman.h>
#endif

int main() {
#if defined(_WIN32) || defined(__APPLE__)
    std::puts("mmu: Linux only (Windows remaps through placeholders; macOS uses no MMU arrays)");
    return 0;
#else
    constexpr size_t blockSize = 512, blocks = 1024, maxArrays = 16, pageBytes = blockSize * sizeof(uint64_t);
    constexpr uint64_t fill = 0x5a5a5a5a5a5a5a5aull;
    constexpr int neighbourCount = 3;
    constexpr size_t pagesEach = 8000;
    struct Page { volatile uint64_t* p; uint64_t stamp; };
    std::vector<Page> pages[neighbourCount];
    std::atomic<bool> stop{false};
    auto neighbour = [&](int t) {
        pages[t].reserve(pagesEach);
        uint64_t stamp = uint64_t(t + 1) << 56;
        while (!stop && pages[t].size() < pagesEach) {
            void* p = mmap(nullptr, pageBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) break;
            auto* w = static_cast<volatile uint64_t*>(p);
            w[0] = ++stamp; w[blockSize - 1] = ~stamp;
            pages[t].push_back({w, stamp});
        }
    };
    std::vector<std::unique_ptr<dsp56k::MmuArray<uint64_t>>> arrays;
    std::thread threads[neighbourCount];
    for (int t = 0; t < neighbourCount; ++t) threads[t] = std::thread(neighbour, t);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < until && arrays.size() < maxArrays) {
        auto& array = *arrays.emplace_back(std::make_unique<dsp56k::MmuArray<uint64_t>>());
        if (!array.init(blocks * blockSize, fill, blockSize)) {
            std::puts("mmu: MMU mapping unavailable here; nothing to check");
            stop = true;
            for (auto& t : threads) t.join();
            return 0;
        }
        for (size_t b = 0; b < blocks; ++b) array.ensureBlockForIndex(b * blockSize);
    }
    stop = true;
    for (auto& t : threads) t.join();
    long inside = 0, stamps = 0, fills = 0, checked = 0;
    for (const auto& list : pages)
        for (const auto& page : list) {
            ++checked;
            const auto at = uintptr_t(page.p);
            for (const auto& a : arrays)
                if (at >= uintptr_t(a->data()) && at < uintptr_t(a->data()) + blocks * pageBytes) ++inside;
            if (page.p[0] != page.stamp || page.p[blockSize - 1] != ~page.stamp) ++stamps;
        }
    for (const auto& a : arrays)
        for (size_t b = 0; b < blocks; ++b)
            if ((*a)[b * blockSize] != fill || (*a)[b * blockSize + blockSize - 1] != fill) ++fills;
    std::printf("mmu: %zu arrays of %zu blocks, %ld neighbour pages: %ld inside an array, %ld stamps lost, "
        "%ld array blocks overwritten\n", arrays.size(), blocks, checked, inside, stamps, fills);
    // A clobbered neighbour page now belongs to an array; leave everything mapped and just exit.
    if (inside || stamps || fills) return 1;
    // Released arrays give back their whole range, reservation included: nothing of it may stay mapped.
    std::vector<std::pair<uintptr_t, size_t>> ranges;
    for (const auto& a : arrays) ranges.emplace_back(uintptr_t(a->data()), blocks * pageBytes);
    arrays.clear();
    long kept = 0;
    std::vector<unsigned char> resident(blocks);
    for (const auto& [at, bytes] : ranges)
        for (size_t b = 0; b < blocks; ++b)
            if (mincore(reinterpret_cast<void*>(at + b * pageBytes), pageBytes, resident.data()) == 0) ++kept;
    std::printf("mmu: %ld array blocks still mapped after release\n", kept);
    return kept == 0 ? 0 : 1;
#endif
}
