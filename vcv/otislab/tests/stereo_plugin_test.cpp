// Stereo through the compiled plugin and Rack's module API: patch data, outputs, the Vastish share and
// stereo recording from AUDIO R. Build like plugin_test.cpp (see README.md).
#include <rack.hpp>
#include "../src/engine/Wav.hpp"
#include "../../shared/SampleShare.hpp"
#include <dlfcn.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>

using namespace rack;
enum { PLAY = 8, LOOP = 10 };                          // OtisSampler params
enum { AUDIO_IN = 0, RECORD_IN = 3, AUDIO_R_IN = 15 }; // inputs (AUDIO_R_IN appended in 2.6)
enum { LEFT_OUT = 0, RIGHT_OUT = 1 };

static json_t* patchData(const std::vector<int16_t>& left, const std::vector<int16_t>* right, double rate) {
    json_t* d = json_object();
    json_object_set_new(d, "pcm16le", json_string(otis::encodePcm(left.data(), left.size()).c_str()));
    if (right) json_object_set_new(d, "pcm16le_r", json_string(otis::encodePcm(right->data(), right->size()).c_str()));
    json_object_set_new(d, "rate", json_real(rate));
    return d;
}

int main() {
    void* handle = dlopen("./plugin.so", RTLD_NOW | RTLD_LOCAL);
    if (!handle) { std::cerr << dlerror() << "\n"; return 1; }
    auto initialize = reinterpret_cast<void (*)(Plugin*)>(dlsym(handle, "init")); assert(initialize);
    Plugin plugin; plugin.slug = "OtisLab"; plugin.version = "2.6.0"; initialize(&plugin);
    auto model = plugin.models.front();
    auto make = [&] { auto m = std::unique_ptr<engine::Module>(model->createModule()); m->model = model; return m; };
    auto st = make(), ml = make(), mr = make(), old = make();
    assert(st->inputs.size() == 16 && st->outputs.size() == 4);   // AUDIO R appended; earlier cable ids unchanged

    std::vector<int16_t> L(9000), R(9000);
    uint32_t rng = 3;
    for (size_t i = 0; i < L.size(); ++i) {
        rng = rng * 1664525u + 1013904223u;
        L[i] = int16_t(12000 * std::sin(i * .05) + int16_t(rng >> 20)); R[i] = int16_t(9000 * std::sin(i * .013 + 2));
    }
    json_t* d = patchData(L, &R, 29761.9); st->dataFromJson(d); json_decref(d);
    d = patchData(L, nullptr, 29761.9); ml->dataFromJson(d); old->dataFromJson(d); json_decref(d);
    d = patchData(R, nullptr, 29761.9); mr->dataFromJson(d); json_decref(d);

    // Patch round trip keeps both channels; a later load reproduces the same data.
    json_t* saved = st->dataToJson();
    assert(json_string_value(json_object_get(saved, "pcm16le_r")));
    assert(otis::decodePcm(json_string_value(json_object_get(saved, "pcm16le"))) == L);
    assert(otis::decodePcm(json_string_value(json_object_get(saved, "pcm16le_r"))) == R);
    auto again = make(); again->dataFromJson(saved); json_t* saved2 = again->dataToJson();
    assert(json_equal(saved, saved2)); json_decref(saved); json_decref(saved2);
    json_t* monoSaved = old->dataToJson(); assert(!json_object_get(monoSaved, "pcm16le_r")); json_decref(monoSaved);

    // Playback: the stereo module's L and R equal mono modules holding each channel.
    engine::Module::SampleRateChangeEvent rate; rate.sampleRate = 48000; rate.sampleTime = 1.f / 48000;
    engine::Module::ProcessArgs args; args.sampleRate = 48000; args.sampleTime = 1.f / 48000;
    std::vector<engine::Module*> all = {st.get(), ml.get(), mr.get(), old.get()};
    for (auto* m : all) { m->onSampleRateChange(rate); m->process(args); m->params[LOOP].setValue(1); m->params[PLAY].setValue(1); }
    double difference = 0, energy = 0;
    for (int i = 0; i < 48000; ++i) {
        args.frame = i;
        for (auto* m : all) m->process(args);
        float l = st->outputs[LEFT_OUT].getVoltage(), r = st->outputs[RIGHT_OUT].getVoltage();
        assert(std::isfinite(l) && std::isfinite(r));
        assert(l == ml->outputs[LEFT_OUT].getVoltage() && r == mr->outputs[LEFT_OUT].getVoltage());
        assert(old->outputs[LEFT_OUT].getVoltage() == old->outputs[RIGHT_OUT].getVoltage()); // mono: same on both
        difference += (l - r) * (l - r); energy += l * l + r * r;
        if (i == 0) for (auto* m : all) m->params[PLAY].setValue(0);
    }
    assert(energy > 1 && difference > .1 * energy);

    // Vastish reads a mono mix through SampleShareV1.
    {
        auto* share = static_cast<const dspemu::SampleShareV1*>(st->rightExpander.producerMessage);
        assert(dspemu::validShare(share));
        double r = 0; uint32_t got = 0; size_t n = share->copy(share->context, nullptr, 0, &r, &got);
        std::vector<int16_t> mix(n); share->copy(share->context, mix.data(), n, &r, &got);
        assert(n == L.size() && r == 29761.9);
        for (size_t i = 0; i < n; ++i) assert(mix[i] == int16_t((int32_t(L[i]) + R[i]) >> 1));
    }

    // Recording: AUDIO R patched when the take starts makes it stereo; unpatched, it stays mono.
    for (bool stereo : {true, false}) {
        auto rec = make(); rec->onSampleRateChange(rate);
        rec->inputs[AUDIO_IN].channels = 1; rec->inputs[AUDIO_IN].setVoltage(2);
        rec->inputs[AUDIO_R_IN].channels = stereo ? 1 : 0; rec->inputs[AUDIO_R_IN].setVoltage(-3);
        rec->inputs[RECORD_IN].channels = 1; rec->inputs[RECORD_IN].setVoltage(10);
        for (int i = 0; i < 9600; ++i) rec->process(args);
        rec->inputs[RECORD_IN].setVoltage(0); rec->process(args);
        json_t* taken = rec->dataToJson();
        auto left = otis::decodePcm(json_string_value(json_object_get(taken, "pcm16le")));
        auto rightText = json_string_value(json_object_get(taken, "pcm16le_r"));
        assert(std::abs(double(left.size()) - otis::captureRate(5) * .2) < 2);
        assert(std::abs(left.back() / 32768. - .4) < .01);             // 2 V of the ±5 V range
        if (stereo) {
            auto right = otis::decodePcm(rightText);
            assert(right.size() == left.size() && std::abs(right.back() / 32768. + .6) < .01);   // -3 V
        } else assert(!rightText);
        json_decref(taken);
    }
    st.reset(); ml.reset(); mr.reset(); old.reset();
    std::cout << "PASS: stereo patch data round trip, L/R equal per-channel mono modules for 1 s, old patches stay mono, "
                 "mono mix for Vastish, stereo take from AUDIO R\n";
}
