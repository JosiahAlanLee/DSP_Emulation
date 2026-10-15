// Drives the real ESPish module through Rack's Module API (no GUI): I/O scaling,
// normalling, clock sync, freeze gate, ESP reconfiguration, capture -> share, JSON.
#include "../src/Espish.cpp"
#include <cassert>
#include <iostream>
Plugin* pluginInstance = nullptr;

static void run(Espish& m, int n, Module::ProcessArgs& a) { for (int i = 0; i < n; i++) { a.frame++; m.process(a); } }

int main() {
    std::cout << std::unitbuf;
    // ParamQuantity::getValue() reads through APP->engine, so a minimal host context is needed.
    Context context; context.engine = new engine::Engine; contextSet(&context);
    Module::ProcessArgs args; args.sampleRate = 48000; args.sampleTime = 1.f / 48000; args.frame = 0;
    {
        Espish m; for (auto& o : m.outputs) o.channels = 1;
        Module::SampleRateChangeEvent sr; sr.sampleRate = 48000; sr.sampleTime = 1.f / 48000; m.onSampleRateChange(sr);
        // Fully dry passes through unchanged; IN R normals to IN L.
        m.params[Espish::MIX].setValue(0);
        m.inputs[Espish::IN_L].channels = 1; m.inputs[Espish::IN_L].setVoltage(3.f);
        run(m, 10, args);
        assert(std::abs(m.outputs[Espish::OUT_L].getVoltage() - 3.f) < 1e-5f && std::abs(m.outputs[Espish::OUT_R].getVoltage() - 3.f) < 1e-5f);
        m.inputs[Espish::IN_R].channels = 1; m.inputs[Espish::IN_R].setVoltage(-2.f);
        run(m, 1, args);
        assert(std::abs(m.outputs[Espish::OUT_R].getVoltage() + 2.f) < 1e-5f);
        m.inputs[Espish::IN_R].channels = 0;

        // Echo only, clock synced: 1/2 x a 0.4 s clock = 0.2 s.
        m.params[Espish::MIX].setValue(1); m.params[Espish::ECHO_MIX].setValue(1); m.params[Espish::SPACE_MIX].setValue(0);
        m.params[Espish::FEEDBACK].setValue(0);
        m.inputs[Espish::CLOCK_IN].channels = 1;
        for (int beat = 0; beat < 4; beat++) { m.inputs[Espish::CLOCK_IN].setVoltage(10); run(m, 100, args); m.inputs[Espish::CLOCK_IN].setVoltage(0); run(m, 19100, args); }
        assert(std::abs(m.clockPeriod - .4) < 1e-3);
        m.params[Espish::TIME].setValue(.25f);  // index 2 of 10 -> 1/2
        m.inputs[Espish::IN_L].setVoltage(0); run(m, 48000, args);
        m.inputs[Espish::IN_L].setVoltage(5); run(m, 1, args); m.inputs[Espish::IN_L].setVoltage(0);
        int peakAt = 0; float peak = 0;
        for (int i = 1; i < 24000; i++) { run(m, 1, args); float v = std::abs(m.outputs[Espish::OUT_L].getVoltage()); if (v > peak) { peak = v; peakAt = i; } }
        assert(std::abs(peakAt - 9600) <= 50);  // 0.2 s (+ glide settled), within ~1 ms
        auto* tq = m.paramQuantities[Espish::TIME];
        assert(tq->getDisplayValueString() == "1/2 x clock");
        m.inputs[Espish::CLOCK_IN].channels = 0;

        // Shift display: centre is exactly zero; fine and wide ranges.
        m.params[Espish::SHIFT].setValue(0); assert(m.paramQuantities[Espish::SHIFT]->getDisplayValueString() == "+0.00");
        m.params[Espish::SHIFT].setValue(1); m.params[Espish::SHIFT_RANGE].setValue(1); assert(m.paramQuantities[Espish::SHIFT]->getDisplayValueString() == "+2000.00");
        m.params[Espish::SHIFT_RANGE].setValue(0); assert(m.paramQuantities[Espish::SHIFT]->getDisplayValueString() == "+20.00");
        m.params[Espish::SHIFT].setValue(-1); assert(m.paramQuantities[Espish::SHIFT]->getDisplayValueString() == "-20.00");
        m.params[Espish::SHIFT].setValue(0);

        // Freeze gate lights the freeze light.
        m.inputs[Espish::FREEZE_IN].channels = 1; m.inputs[Espish::FREEZE_IN].setVoltage(10); run(m, 1, args);
        assert(m.lights[Espish::FREEZE_LIGHT].getBrightness() == 1);
        m.inputs[Espish::FREEZE_IN].setVoltage(0); run(m, 1, args);
        assert(m.lights[Espish::FREEZE_LIGHT].getBrightness() == 0);

        // Capture: play a ramp, press CAPTURE, collect on the "UI", read through the share.
        m.params[Espish::MIX].setValue(0); m.params[Espish::CAPTURE_LEN].setValue(1);
        for (int i = 0; i < 96000; i++) { m.inputs[Espish::IN_L].setVoltage(5.f * float(i % 4800) / 4800); run(m, 1, args); }
        auto* share = static_cast<const dspemu::SampleShareV1*>(m.rightExpander.producerMessage);
        assert(dspemu::validShare(share) && share->revision(share->context) == 0);
        assert(share->copy(share->context, nullptr, 0, nullptr, nullptr) == 0);
        m.params[Espish::CAPTURE].setValue(1); run(m, 1, args); m.params[Espish::CAPTURE].setValue(0); run(m, 1, args);
        assert(m.lights[Espish::CAPTURE_LIGHT].getBrightness() == 1);  // waiting for the UI
        m.collectCapture();
        uint32_t rev = share->revision(share->context); assert(rev == 2);
        double rate = 0; uint32_t got = 0;
        size_t n = share->copy(share->context, nullptr, 0, &rate, &got);
        assert(n == 48000 && rate == 48000 && got == rev);
        std::vector<int16_t> pcm(n); share->copy(share->context, pcm.data(), n, &rate, &got);
        // The capture is the dry ramp (mono sum of L and normalled R), oldest first.
        int16_t last = pcm[n - 1]; assert(last > 16000 && last < 16400);  // 5 V * 4799/4800 -> 0.5 * 32768
        char name[64]; share->name(share->context, name, sizeof name); assert(std::string(name) == "ESPish capture 1");
        run(m, 10, args);
        assert(m.lights[Espish::CAPTURE_LIGHT].getBrightness() == 0);
        // A capture trigger input works too, and the revision advances by two.
        m.inputs[Espish::CAPTURE_IN].channels = 1; m.inputs[Espish::CAPTURE_IN].setVoltage(10); run(m, 1, args);
        m.collectCapture(); assert(share->revision(share->context) == 4);

        // ESP: reconfigure rate and budget off the audio path; output stays finite.
        m.params[Espish::ENGINE].setValue(1); m.params[Espish::MIX].setValue(1);
        for (int r = 0; r < 3; r++) {
            m.reconfigure(r, r != 1);
            assert(m.engine.preparedRate == r);
            for (int i = 0; i < 4800; i++) { m.inputs[Espish::IN_L].setVoltage(5.f * float(std::sin(i * .05))); run(m, 1, args); assert(std::isfinite(m.outputs[Espish::OUT_L].getVoltage())); }
        }
        // JSON round trip.
        json_t* j = m.dataToJson();
        Espish n2; n2.dataFromJson(j); json_decref(j);
        assert(n2.espRateIndex == 2 && n2.espBudget == true && n2.engine.preparedRate == 2);
    }
    std::cout << "PASS: ESPish module: dry path and normalling, clock-synced echo, shift display, freeze gate, capture to share, ESP reconfiguration, JSON\n";
}
