// Direct adapter test for audio-thread program requests and held-gate Panic.
#include "../src/Vastish.cpp"
#include <cassert>
#include <iostream>
Plugin* pluginInstance=nullptr;
int main() {
    Vastish m; Module::ProcessArgs args; args.sampleRate=48000; args.sampleTime=1.f/48000;
    m.inputs[Vastish::PITCH_IN].channels=m.inputs[Vastish::GATE_IN].channels=1;
    for(auto& o:m.outputs) o.channels=1;
    m.process(args); m.inputs[Vastish::GATE_IN].setVoltage(10);
    for(int i=0;i<1000;++i) m.process(args);
    assert(m.engine.voices[0].sources[11]>.1f);
    m.requestedPanic=true; m.process(args);
    for(int i=0;i<1000;++i) { m.process(args); assert(m.outputs[0].getVoltage()==0); }
    m.inputs[Vastish::GATE_IN].setVoltage(0); m.process(args);
    m.inputs[Vastish::GATE_IN].setVoltage(10); for(int i=0;i<1000;++i) m.process(args);
    assert(m.engine.voices[0].sources[11]>.1f);
    for(int pg=0;pg<6;++pg) { m.requestedProgram=pg; m.process(args); for(int i=0;i<PARAM_COUNT;++i) assert(m.params[i].getValue()==programs()[pg][i]); assert(m.displayedProgram==pg); }
    // Exercise broad editor endpoints across functions with maximal modulation.
    for(int type=0;type<=14;++type) {
        for(int i=0;i<PARAM_COUNT;++i) if(metadata()[i].options.empty()) m.params[i].setValue(metadata()[i].hi);
        m.params[F1_TYPE].setValue(type); m.params[F2_TYPE].setValue(type); m.params[F3_TYPE].setValue(type);
        for(int i=0;i<100;++i) { m.process(args); assert(std::isfinite(m.outputs[0].getVoltage())); }
    }
    // SAMPLE wave: UI-thread publish, audio-thread adoption, retirement, and patch round-trip.
    {
        Vastish s; for(auto& o:s.outputs) o.channels=1;
        s.inputs[Vastish::PITCH_IN].channels=s.inputs[Vastish::GATE_IN].channels=1;
        std::vector<int16_t> pcm(9600); for(size_t i=0;i<pcm.size();++i) pcm[i]=int16_t(16000*std::sin(i*.03));
        dspemu::MonoSample one; one.pcm=pcm; one.rate=48000; one.name="one.wav";
        s.publishSample(new SampleSlot(one));
        // A second and third publish before the audio thread runs: only the last survives.
        dspemu::MonoSample two=one; two.name="two.wav"; s.publishSample(new SampleSlot(two));
        dspemu::MonoSample three=one; three.name="three.wav"; for(auto& x:three.pcm) x=int16_t(x/2);
        s.publishSample(new SampleSlot(three));
        s.params[PITCH_WAVE].setValue(SAMPLE_WAVE); s.inputs[Vastish::GATE_IN].setVoltage(10);
        double energy=0; for(int i=0;i<4800;++i) { s.process(args); energy+=std::pow(s.outputs[0].getVoltage(),2); }
        assert(energy>1e-3 && s.liveSample && s.liveSample->data.name=="three.wav" && s.sampleLabel()=="three.wav");
        assert(s.pendingSample.load()==nullptr && s.retiredSample.load()==nullptr);
        s.publishSample(new SampleSlot(one)); s.process(args);
        assert(s.liveSample->data.name=="one.wav" && s.retiredSample.load()!=nullptr);
        // Until the UI collects, a newer sample waits rather than being freed on the audio thread.
        // (publishSample collects first, so stage the pending sample directly here.)
        auto* waiting=new SampleSlot(two); s.pendingSample.store(waiting); s.process(args);
        assert(s.liveSample->data.name=="one.wav" && s.pendingSample.load()==waiting);
        s.collectSamples(); s.process(args); assert(s.liveSample==waiting);
        s.uiSample=waiting; s.collectSamples();
        // Saved patch restores the sample and the loop setting, and plays identically.
        s.sampleLoop=true; json_t* saved=s.dataToJson();
        Vastish r; r.dataFromJson(saved); json_decref(saved);
        assert(r.sampleLoop && r.sampleLabel()=="two.wav");
        assert(r.uiSample->data.pcm==s.uiSample->data.pcm && r.uiSample->data.rate==s.uiSample->data.rate);
        // A restored module plays exactly like a fresh one given the same sample.
        Vastish f; f.publishSample(new SampleSlot(s.uiSample->data)); f.sampleLoop=true;
        for(auto* m:{&f,&r}) {
            for(auto& o:m->outputs) o.channels=1; m->inputs[Vastish::PITCH_IN].channels=m->inputs[Vastish::GATE_IN].channels=1;
            m->params[PITCH_WAVE].setValue(SAMPLE_WAVE); m->inputs[Vastish::GATE_IN].setVoltage(10);
        }
        double played=0;
        for(int i=0;i<48000;++i) { f.process(args); r.process(args); assert(f.outputs[0].getVoltage()==r.outputs[0].getVoltage()); played+=std::abs(r.outputs[0].getVoltage()); }
        assert(played>1); // loop keeps it sounding past the 0.2 s sample
        // Clearing publishes an empty sample: silence, label says so.
        s.publishSample(new SampleSlot()); s.collectSamples(); s.process(args); s.collectSamples();
        for(int i=0;i<2400;++i) s.process(args);
        assert(s.sampleLabel()=="NO SAMPLE");
    }
    std::cout<<"PASS: audio-thread program loads, held-gate Panic, retrigger, editor endpoints, sample handoff and patch round-trip\n";
}
