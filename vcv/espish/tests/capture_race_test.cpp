// Capture threading, run under ThreadSanitizer in CI. Three threads use the engine the
// way the module does:
//   audio:  try_lock the engine mutex, tick (writes the capture ring), sometimes trigger
//   reconfig: lock the engine mutex and re-prepare at another sample rate (rebuilds the rings)
//   UI:     collect captures without the engine mutex (as collectCapture() does)
// Every collected capture must carry the rate it was recorded at and fit that rate's ring.
#include "../src/engine/Fx.hpp"
#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
using namespace espish;

int main() {
    std::cout << std::unitbuf;
    const double rates[2] = {48000, 96000};
    const double captureSeconds = .02;
    auto engine = std::make_unique<Engine>();
    engine->prepare(rates[0], 0, captureSeconds);
    std::mutex engineMutex;
    std::atomic<bool> done{false};
    std::atomic<int> collected{0}, triggered{0}, prepared{0};

    std::thread audio([&] {
        Params p; p.mix = .5f;
        for (int ticks = 0; ticks < 200000;) {
            std::unique_lock<std::mutex> lock(engineMutex, std::try_to_lock);
            if (!lock.owns_lock()) { std::this_thread::yield(); continue; }  // the module outputs silence here
            engine->tick(.25f, -.25f, p);
            if (++ticks % 487 == 0 && engine->capture.trigger(size_t(captureSeconds * engine->hostRate))) triggered++;
        }
        done = true;
    });
    std::thread reconfig([&] {
        for (int k = 1; !done; k++) {
            { std::lock_guard<std::mutex> lock(engineMutex); engine->prepare(rates[k % 2], 0, captureSeconds); }
            prepared++;
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    });
    std::thread ui([&] {
        std::vector<int16_t> pcm; double rate = 0;
        while (!done) {
            if (engine->capture.collect(pcm, rate)) {
                assert(rate == rates[0] || rate == rates[1]);
                assert(pcm.size() >= 2 && pcm.size() <= size_t(captureSeconds * rate));
                collected++;
            }
            std::this_thread::yield();
        }
    });
    audio.join(); reconfig.join(); ui.join();
    std::cout << "capture threads: " << triggered << " triggered, " << collected << " collected, "
              << prepared << " rate changes, no torn copies\n";
    assert(collected > 0 && prepared > 0);
}
