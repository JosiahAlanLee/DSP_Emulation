// Cross-plugin link: the real OTIS module code importing captures from a Spoon Bender
// module created from SpoonBenderLab's compiled binary, as Rack would place them.
#include "../src/OtisSampler.cpp"
#include <dlfcn.h>
#include <cassert>
#include <iostream>
#include <memory>
Plugin* pluginInstance = nullptr;
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    const char* path = argc > 1 ? argv[1] : "../../spoonbender/plugin.so";
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) { std::cerr << dlerror() << "\n"; return 1; }
    auto init = reinterpret_cast<void (*)(rack::plugin::Plugin*)>(dlsym(handle, "init")); assert(init);
    rack::plugin::Plugin fxPlugin; fxPlugin.slug = "SpoonBenderLab"; init(&fxPlugin);
    auto* model = fxPlugin.models.front(); model->plugin = &fxPlugin;
    assert(model->slug == "SpoonBender");
    auto fx = std::unique_ptr<engine::Module>(model->createModule()); fx->model = model;
    // Spoon Bender parameter/port indices (see vcv/spoonbender/src/SpoonBender.cpp).
    const int MIX = 16, CAPTURE = 18, CAPTURE_LEN = 19, IN_L = 0;
    for (auto& o : fx->outputs) o.channels = 1;
    fx->inputs[IN_L].channels = 1;
    fx->params[MIX].setValue(0); fx->params[CAPTURE_LEN].setValue(1);
    engine::Module::ProcessArgs args; args.sampleRate = 48000; args.sampleTime = 1.f / 48000; args.frame = 0;
    for (int i = 0; i < 60000; i++) { fx->inputs[IN_L].setVoltage(5.f * float(std::sin(i * .01))); fx->process(args); args.frame++; }

    OtisSampler otis; otis.followFx = true;
    assert(!OtisSampler::findFxShare(nullptr));
    fxPlugin.slug = "SomethingElse"; assert(!OtisSampler::findFxShare(fx.get())); fxPlugin.slug = "SpoonBenderLab";
    auto* share = OtisSampler::findFxShare(fx.get()); assert(share);
    // Nothing captured yet: following does nothing.
    otis.followFxShare(share);
    { std::lock_guard<std::mutex> g(otis.nameMutex); assert(otis.sampleName.rfind("NO SAMPLE", 0) == 0); }
    // Press CAPTURE on the Spoon Bender; OTIS (following) picks it up on its next UI frame.
    fx->params[CAPTURE].setValue(1); fx->process(args); fx->params[CAPTURE].setValue(0); fx->process(args);
    otis.followFxShare(share);
    std::vector<int16_t> otisPcm; double otisRate = 0; uint32_t seq = 0; otis.engine.snapshot(otisPcm, otisRate, seq);
    double rate = 0; uint32_t rev = 0; size_t n = share->copy(share->context, nullptr, 0, &rate, &rev);
    std::vector<int16_t> fxPcm(n); share->copy(share->context, fxPcm.data(), n, &rate, &rev);
    assert(n == 48000 && otisPcm == fxPcm && otisRate == 48000);
    { std::lock_guard<std::mutex> g(otis.nameMutex); assert(otis.sampleName == "Spoon Bender capture 1"); }
    // No re-import while the revision is unchanged; a new capture is picked up.
    uint32_t before = otis.engine.sequence();
    otis.followFxShare(share); assert(otis.engine.sequence() == before);
    for (int i = 0; i < 4800; i++) { fx->inputs[IN_L].setVoltage(-2.5f); fx->process(args); }
    fx->params[CAPTURE].setValue(1); fx->process(args); fx->params[CAPTURE].setValue(0); fx->process(args);
    otis.followFxShare(share);
    { std::lock_guard<std::mutex> g(otis.nameMutex); assert(otis.sampleName == "Spoon Bender capture 2"); }
    otis.engine.snapshot(otisPcm, otisRate, seq);
    assert(otisPcm.size() == 4802 && otisPcm.back() == -8192);  // only what Spoon Bender recorded since the last capture
    // One-shot import works without following; a missing neighbour is ignored.
    OtisSampler manual; assert(!manual.importFrom(nullptr)); assert(manual.importFrom(share));
    manual.engine.snapshot(otisPcm, otisRate, seq); assert(otisPcm.size() == 4802);
    // Saved patch keeps the follow setting.
    json_t* j = otis.dataToJson(); OtisSampler restored; restored.dataFromJson(j); json_decref(j);
    assert(restored.followFx && !restored.fxMono);

    // Stereo (Spoon Bender 2.2.0+): distinct L and R arrive as a stereo sample. The version above was
    // left empty, so everything so far used the mono link; that is how an older Spoon Bender behaves.
    const int IN_R = 1;
    assert(!OtisSampler::findFxStereo(fx.get()));
    fxPlugin.version = "2.2.0";
    auto* stereo = OtisSampler::findFxStereo(fx.get()); assert(stereo);
    fx->inputs[IN_R].channels = 1;
    for (int i = 0; i < 9600; i++) {
        fx->inputs[IN_L].setVoltage(3.f * float(std::sin(i * .02))); fx->inputs[IN_R].setVoltage(-2.f * float(std::sin(i * .031)));
        fx->process(args);
    }
    fx->params[CAPTURE].setValue(1); fx->process(args); fx->params[CAPTURE].setValue(0); fx->process(args);
    otis.followFxShare(share, stereo);
    uint32_t channels = 0; n = stereo->copyStereo(share->context, nullptr, nullptr, 0, &rate, &rev, &channels);
    assert(channels == 2);
    std::vector<int16_t> fxL(n), fxR(n), mix(n);
    stereo->copyStereo(share->context, fxL.data(), fxR.data(), n, &rate, &rev, &channels);
    share->copy(share->context, mix.data(), n, &rate, &rev);
    std::vector<int16_t> l, r;
    assert(otis.engine.snapshotStereo(l, r, otisRate, seq) && l == fxL && r == fxR && l != r && otisRate == 48000);
    { std::lock_guard<std::mutex> g(otis.nameMutex); assert(otis.sampleName == "Spoon Bender capture 3"); }
    // "Import Spoon Bender captures as mono" takes the mix instead, at the full mono length.
    OtisSampler folded; folded.fxMono = true; assert(folded.importFrom(share, stereo));
    assert(!folded.engine.snapshotStereo(l, r, otisRate, seq) && l == mix);
    json_t* k = folded.dataToJson(); OtisSampler again; again.dataFromJson(k); json_decref(k); assert(again.fxMono);
    // A stereo capture longer than half of OTIS's memory is truncated, and marked.
    fx->params[CAPTURE_LEN].setValue(20);
    for (int i = 0; i < 48000 * 12; i++) { fx->inputs[IN_L].setVoltage(1); fx->inputs[IN_R].setVoltage(-1); fx->process(args); }
    fx->params[CAPTURE].setValue(1); fx->process(args); fx->params[CAPTURE].setValue(0); fx->process(args);
    OtisSampler longTake; assert(longTake.importFrom(share, stereo));
    assert(longTake.engine.sample.stereo && longTake.engine.frames == otis::stereoFrames);
    { std::lock_guard<std::mutex> g(longTake.nameMutex); assert(longTake.sampleName.find("[TRUNCATED]") != std::string::npos); }
    fx.reset();
    std::cout << "PASS: OTIS imports and follows captures from a real Spoon Bender module (compiled binary): mono from older versions, stereo from 2.2.0, mono on request, truncation\n";
}
