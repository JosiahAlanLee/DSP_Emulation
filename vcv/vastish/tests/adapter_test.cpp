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
        assert(s.sampleShortLabel()=="three");
        assert(s.pendingSample.load()==nullptr && s.retiredSample.load()==nullptr);
        s.publishSample(new SampleSlot(one)); s.process(args);
        assert(s.liveSample->data.name=="one.wav" && s.retiredSample.load()!=nullptr);
        // Until the UI collects, a newer sample waits rather than being freed on the audio thread.
        // (publishSample collects first, so stage the pending sample directly here.)
        auto* waiting=new SampleSlot(two); s.pendingSample.store(waiting); s.process(args);
        assert(s.liveSample->data.name=="one.wav" && s.pendingSample.load()==waiting);
        s.collectSamples(); s.process(args); assert(s.liveSample==waiting);
        s.uiSample=waiting; waiting->generation=++s.publishedGeneration; s.collectSamples(); // what publishSample records
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
    // Saved PCM always matches the current sample, even when a retired slot's
    // address is reused by the next allocation (review finding 1). Sequence: save A,
    // publish B (A retires and is freed), publish C (often lands at A's address), save.
    {
        auto make=[](int value,const char* name) { dspemu::MonoSample s; s.pcm.assign(128,int16_t(value)); s.rate=48000; s.name=name; return new SampleSlot(std::move(s)); };
        int reused=0;
        for(int trial=0;trial<50;++trial) {
            Vastish t; t.publishSample(make(111,"A")); t.process(args); json_decref(t.dataToJson());
            const void* a=t.uiSample;
            t.publishSample(make(222,"B")); t.process(args); t.collectSamples();
            auto* c=make(333,"C"); reused+=(c==a);
            t.publishSample(c); t.process(args); t.collectSamples();
            json_t* saved=t.dataToJson();
            auto pcm=dspemu::decodePcm16(json_string_value(json_object_get(json_object_get(saved,"sample"),"pcm16le")),Vastish::MAX_SAMPLE_FRAMES);
            assert(pcm==t.uiSample->data.pcm && pcm[0]==333); json_decref(saved);
            t.collectSamples();
        }
        std::cout<<"save-cache check: address reuse observed in "<<reused<<"/50 trials\n";
    }
    {
        Vastish m;
        dspemu::MonoSample old; old.pcm.assign(4800,12000); old.rate=48000; old.name="old sample";
        m.publishSample(new SampleSlot(std::move(old))); m.process(args); m.collectSamples();
        assert(m.sampleLabel()=="old sample");
        // Restoring a state that has no sample into a populated instance clears it (finding 3).
        Vastish empty; json_t* none=empty.dataToJson(); assert(!json_object_get(none,"sample"));
        m.dataFromJson(none); json_decref(none);
        assert(m.sampleLabel()=="NO SAMPLE");
        m.process(args); m.collectSamples();
        assert(m.liveSample && !m.liveSample->view.valid()); // the audio thread has the empty sample
        // INIT routes only the oscillator, so no sample means silence (Ladder Bass's F-blocks can self-oscillate).
        for(int i=0;i<PARAM_COUNT;++i) m.params[i].setValue(programs()[0][i]);
        m.params[PITCH_WAVE].setValue(SAMPLE_WAVE); for(auto& o:m.outputs) o.channels=1;
        m.inputs[Vastish::PITCH_IN].channels=m.inputs[Vastish::GATE_IN].channels=1; m.inputs[Vastish::GATE_IN].setVoltage(10);
        for(int i=0;i<2000;++i) { m.process(args); assert(m.outputs[0].getVoltage()==0); }
        m.collectSamples();
    }
    // OTIS link, driven through a stand-in provider using the same C ABI.
    {
        struct Fake { std::vector<int16_t> pcm; double rate=29762; uint32_t rev=2; int copies=0; bool changeDuringCopy=false; std::string name="bright-demo.wav"; } fake;
        fake.pcm.assign(3000,0); for(size_t i=0;i<fake.pcm.size();++i) fake.pcm[i]=int16_t(i*7);
        dspemu::SampleShareV1 share{dspemu::SAMPLE_SHARE_MAGIC,dspemu::SAMPLE_SHARE_VERSION,&fake,
            [](void* c)->uint32_t { return static_cast<Fake*>(c)->rev; },
            [](void* c,int16_t* dst,size_t cap,double* rate,uint32_t* rev)->size_t {
                auto* f=static_cast<Fake*>(c); f->copies++;
                if(dst) { std::copy(f->pcm.begin(),f->pcm.begin()+std::min(cap,f->pcm.size()),dst); if(f->changeDuringCopy) { f->rev+=2; f->changeDuringCopy=false; } }
                *rate=f->rate; *rev=f->rev; return f->pcm.size(); },
            [](void* c,char* b,size_t n) { std::snprintf(b,n,"%s",static_cast<Fake*>(c)->name.c_str()); }};
        assert(dspemu::validShare(&share));
        dspemu::SampleShareV1 wrong=share; wrong.version=99; assert(!dspemu::validShare(&wrong));
        Vastish v;
        v.followShare(&share); assert(v.linkState==Vastish::LINK_OFF && !v.uiSample); // off by default
        v.setFollowOtis(true);
        v.followShare(nullptr); assert(v.linkState==Vastish::LINK_NOT_FOUND && !v.uiSample);
        v.followShare(&share);
        assert(v.linkState==Vastish::LINK_CONNECTED && v.uiSample && v.uiSample->data.pcm==fake.pcm);
        assert(v.uiSample->data.rate==fake.rate && v.sampleLabel()=="OTIS: bright-demo.wav");
        assert(v.sampleShortLabel()=="\xe2\x97\x80 bright-demo");
        int copies=fake.copies; v.followShare(&share); assert(fake.copies==copies); // unchanged: no copy
        fake.rev=3; fake.pcm[0]=1234; v.followShare(&share); assert(fake.copies==copies && v.uiSample->data.pcm[0]==0); // mid-change: wait
        fake.rev=4; v.followShare(&share); assert(v.uiSample->data.pcm[0]==1234); // REWORK/record lands
        fake.pcm[0]=99; fake.rev=6; fake.changeDuringCopy=true; v.followShare(&share);
        assert(v.uiSample->data.pcm[0]==1234); // torn copy discarded...
        v.followShare(&share); assert(v.uiSample->data.pcm[0]==99); // ...and retried
        fake.pcm.assign(1,0); fake.rev=10; v.followShare(&share); assert(v.sampleLabel()=="NO SAMPLE"); // OTIS emptied
        // Saved and restored: link setting persists; follows again on the next frame.
        json_t* saved=v.dataToJson(); Vastish r; r.dataFromJson(saved); json_decref(saved);
        assert(r.followOtis); fake.pcm.assign(500,7); fake.rev=12; r.followShare(&share); assert(r.uiSample->data.pcm.size()==500);
        v.setFollowOtis(false); fake.rev=14; v.followShare(&share); assert(v.linkState==Vastish::LINK_OFF && v.sampleLabel()=="NO SAMPLE");
        v.collectSamples(); r.collectSamples();
    }
    std::cout<<"PASS: audio-thread program loads, held-gate Panic, retrigger, editor endpoints, sample handoff and patch round-trip, OTIS link\n";
}
