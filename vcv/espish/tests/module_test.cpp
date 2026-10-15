// Drives the real ESPish module through Rack's Module API (no GUI): I/O scaling,
// normalling, clock sync, freeze gate, ESP reconfiguration, capture -> share, JSON.
// Host internals first (before rack.hpp marks them private) to provide a Rack context.
#include <engine/Engine.hpp>
#include <context.hpp>
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
        // Typed entry in clock mode is a ratio (Rack evaluates "3/2"); the nearest ratio is chosen.
        tq->setDisplayValueString("3/2"); assert(tq->getDisplayValueString() == "3/2 x clock");
        tq->setDisplayValueString("0.3"); assert(tq->getDisplayValueString() == "1/3 x clock");
        tq->setDisplayValueString("4"); assert(tq->getDisplayValueString() == "4 x clock");
        tq->setDisplayValueString(".5"); assert(tq->getDisplayValueString() == "1/2 x clock");
        m.inputs[Espish::CLOCK_IN].channels = 0;
        // Unclocked, typed entry is in seconds.
        tq->setDisplayValueString("0.5"); assert(std::abs(tq->getDisplayValue() - .5f) < 1e-4f);
        tq->setDisplayValueString("2/3"); assert(std::abs(tq->getDisplayValue() - 2.f / 3) < 1e-4f);

        // Shift display: centre is exactly zero; fine and wide ranges.
        m.params[Espish::SHIFT].setValue(0); assert(m.paramQuantities[Espish::SHIFT]->getDisplayValueString() == "+0.00");
        m.params[Espish::SHIFT].setValue(1); m.params[Espish::SHIFT_RANGE].setValue(1); assert(m.paramQuantities[Espish::SHIFT]->getDisplayValueString() == "+2000.00");
        m.params[Espish::SHIFT_RANGE].setValue(0); assert(m.paramQuantities[Espish::SHIFT]->getDisplayValueString() == "+20.00");
        m.params[Espish::SHIFT].setValue(-1); assert(m.paramQuantities[Espish::SHIFT]->getDisplayValueString() == "-20.00");
        // Typed entry is in Hz for the current range; out-of-range values clamp, tiny ones centre.
        auto* sq = m.paramQuantities[Espish::SHIFT];
        auto typed = [&](const char* text, const char* shown) { sq->setDisplayValueString(text); return sq->getDisplayValueString() == shown; };
        m.params[Espish::SHIFT_RANGE].setValue(1);
        assert(typed("1", "+1.00") && typed("-250", "-250.00") && typed("0.1", "+0.10") && typed("5000", "+2000.00") && typed("-1e6", "-2000.00"));
        assert(typed("0", "+0.00") && m.params[Espish::SHIFT].getValue() == 0);
        assert(typed("0.03", "+0.00") && m.params[Espish::SHIFT].getValue() == 0);
        m.params[Espish::SHIFT_RANGE].setValue(0);
        assert(typed("12.5", "+12.50") && typed("-0.25", "-0.25") && typed("500", "+20.00") && typed("-3", "-3.00"));
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
        // A sample-rate change before collection discards the pending capture (the rings are
        // rebuilt); the next capture is labelled with the rate it was recorded at.
        m.inputs[Espish::CAPTURE_IN].setVoltage(0); run(m, 1, args);
        m.inputs[Espish::CAPTURE_IN].setVoltage(10); run(m, 1, args);  // pending, not collected
        Module::SampleRateChangeEvent sr96; sr96.sampleRate = 96000; sr96.sampleTime = 1.f / 96000; m.onSampleRateChange(sr96);
        m.collectCapture(); assert(share->revision(share->context) == 4);
        Module::ProcessArgs a96 = args; a96.sampleRate = 96000; a96.sampleTime = 1.f / 96000;
        m.inputs[Espish::CAPTURE_IN].setVoltage(0); run(m, 120000, a96);
        m.inputs[Espish::CAPTURE_IN].setVoltage(10); run(m, 1, a96);
        m.collectCapture(); assert(share->revision(share->context) == 6);
        n = share->copy(share->context, nullptr, 0, &rate, &got); assert(n == 96000 && rate == 96000 && got == 6);
        m.inputs[Espish::CAPTURE_IN].setVoltage(0); m.onSampleRateChange(sr);

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
