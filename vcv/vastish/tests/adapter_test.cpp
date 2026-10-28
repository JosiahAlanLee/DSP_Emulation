// Direct adapter test for audio-thread program requests and held-gate Panic.
#include "../src/Vastish.cpp"
#include <cassert>
#include <iostream>
#include <random>
#include <vector>
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
    // AUX through the module: a poly cable gives each voice its own value, a mono
    // cable reaches every voice; FUN 3/4 outputs carry per-voice results.
    {
        Vastish a; for(auto& o:a.outputs) o.channels=1;
        a.inputs[Vastish::PITCH_IN].channels=a.inputs[Vastish::GATE_IN].channels=2;
        a.params[FUN3_A].setValue(SRC_AUX1); a.params[FUN3_B].setValue(0); a.params[FUN3_EQ].setValue(0);   // FUN3 = AUX1
        a.params[FUN4_A].setValue(SRC_AUX1+1); a.params[FUN4_B].setValue(0); a.params[FUN4_EQ].setValue(0); // FUN4 = AUX2
        a.inputs[Vastish::AUX1_IN].channels=2; a.inputs[Vastish::AUX1_IN].setVoltage(5,0); a.inputs[Vastish::AUX1_IN].setVoltage(-2,1);
        a.inputs[Vastish::AUX2_IN].channels=1; a.inputs[Vastish::AUX2_IN].setVoltage(3);
        a.process(args);
        assert(a.outputs[Vastish::FUN3_OUT].getChannels()==2 && a.outputs[Vastish::FUN4_OUT].getChannels()==2);
        assert(std::abs(a.outputs[Vastish::FUN3_OUT].getVoltage(0)-5)<1e-5f && std::abs(a.outputs[Vastish::FUN3_OUT].getVoltage(1)+2)<1e-5f);
        assert(std::abs(a.outputs[Vastish::FUN4_OUT].getVoltage(0)-3)<1e-5f && std::abs(a.outputs[Vastish::FUN4_OUT].getVoltage(1)-3)<1e-5f);
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
    // AUDIO IN is appended as input 10; every earlier input and output keeps its ID.
    static_assert(Vastish::PITCH_IN==0 && Vastish::BUS_A_IN==8 && Vastish::BUS_B_IN==9 && Vastish::AUDIO_IN==10 && Vastish::INPUTS==11);
    static_assert(Vastish::LEFT_OUT==0 && Vastish::BUS_OUT==11 && Vastish::OUTPUTS==12);
    {
        auto live=[&](Vastish& v) {
            for(int i=0;i<PARAM_COUNT;++i) v.params[i].setValue(programs()[0][i]);
            v.params[PITCH_WAVE].setValue(AUDIO_WAVE); v.params[MASTER].setValue(0);
            v.params[AMP_ADJ].setValue(0); v.params[PAN_ADJ].setValue(-1);
            for(auto& o:v.outputs) o.channels=1;
            v.inputs[Vastish::AUDIO_IN].channels=1;
            for(int i=0;i<4800;++i) v.process(args); // MASTER's 50 ms ramp
        };
        // A ±5 V saw into AUDIO IN with GATE unpatched leaves L at the same level; the
        // engine's 5 Hz DC blocker is the only difference. The ±10 V range halves it.
        for(bool ten:{false,true}) {
            Vastish v; v.audioIn10V=ten; live(v); Highpass dc;
            for(int n=0;n<48000;++n) {
                float saw=5*(2*std::fmod(n*110.f/48000,1.f)-1); v.inputs[Vastish::AUDIO_IN].setVoltage(saw); v.process(args);
                assert(v.outputs[Vastish::LEFT_OUT].getVoltage()==dc.tick(saw*(ten ? .1f : .2f),5,48000)*5);
            }
        }
        // Patching GATE brings back the amp envelope: silent while low, sounding while high.
        Vastish g; live(g); g.inputs[Vastish::GATE_IN].channels=1; g.inputs[Vastish::AUDIO_IN].setVoltage(5);
        for(int i=0;i<480;++i) { g.process(args); assert(g.outputs[Vastish::LEFT_OUT].getVoltage()==0); }
        g.inputs[Vastish::GATE_IN].setVoltage(10); double sounded=0;
        for(int i=0;i<4800;++i) { g.process(args); sounded+=std::abs(g.outputs[Vastish::LEFT_OUT].getVoltage()); }
        assert(sounded>100);
        // Without a gate, the AUDIO voice follows PITCH continuously so F blocks key-track.
        Vastish k; live(k); k.inputs[Vastish::PITCH_IN].channels=1;
        k.inputs[Vastish::PITCH_IN].setVoltage(1); k.process(args); assert(k.in[0].note==72);
        k.inputs[Vastish::PITCH_IN].setVoltage(-2); k.process(args); assert(k.in[0].note==36);
        // With GATE patched, pitch is again held between notes, as for every wave.
        k.inputs[Vastish::GATE_IN].channels=1; k.inputs[Vastish::PITCH_IN].setVoltage(2); k.process(args); assert(k.in[0].note==36);
        // Other waves never follow PITCH without a gate.
        k.inputs[Vastish::GATE_IN].channels=0; k.params[PITCH_WAVE].setValue(0); k.process(args); assert(k.in[0].note==36);
        // Unpatched, AUDIO IN reads nothing: silence with the gate-free amp off.
        Vastish u; live(u); u.inputs[Vastish::AUDIO_IN].channels=0; u.inputs[Vastish::AUDIO_IN].setVoltage(5);
        for(int i=0;i<480;++i) { u.process(args); assert(u.outputs[Vastish::LEFT_OUT].getVoltage()==0); }
        // Non-finite input is silence, not a fault.
        Vastish bad; live(bad); bad.inputs[Vastish::AUDIO_IN].setVoltage(NAN);
        for(int i=0;i<480;++i) { bad.process(args); assert(bad.outputs[Vastish::LEFT_OUT].getVoltage()==0); }
        assert(bad.engine.faults==0);
    }
    // Polyphonic AUDIO IN: its channels establish voices, voice v reads channel v, a mono cable
    // reaches every voice, and a voice without a channel is a silent input under its envelope.
    {
        auto live=[&](Vastish& v,int pitchChannels,int audioChannels) {
            for(int i=0;i<PARAM_COUNT;++i) v.params[i].setValue(programs()[0][i]);
            v.params[PITCH_WAVE].setValue(AUDIO_WAVE); v.params[MASTER].setValue(0);
            v.params[AMP_ADJ].setValue(0); v.params[PAN_ADJ].setValue(-1);
            for(auto& o:v.outputs) o.channels=1;
            v.inputs[Vastish::PITCH_IN].channels=pitchChannels; v.inputs[Vastish::AUDIO_IN].channels=audioChannels;
        };
        // Four AUDIO channels and nothing else patched: four gate-free voices, each its own channel.
        Vastish v; live(v,0,4);
        for(int c=0;c<4;++c) v.inputs[Vastish::AUDIO_IN].setVoltage(1.f+c,c);
        v.process(args);
        assert(v.outputs[Vastish::AMP_OUT].getChannels()==4);
        for(int c=0;c<8;++c) assert(v.engine.voices[c].tap==(c<4 ? (1.f+c)*.2f : 0.f));
        // L is the four voices' sum through the DC blocker, at unity.
        Vastish u; live(u,0,4);
        for(int i=0;i<4800;++i) u.process(args); // silence while MASTER's 50 ms ramp settles
        Highpass dc;
        for(int n=0;n<4800;++n) {
            float sum=0;
            for(int c=0;c<4;++c) { float x=5*std::sin(.01f*n*(c+1)); u.inputs[Vastish::AUDIO_IN].setVoltage(x,c); sum+=x*.2f; }
            u.process(args); assert(u.outputs[Vastish::LEFT_OUT].getVoltage()==dc.tick(sum,5,48000)*5);
        }
        // Each voice keeps its own key: PITCH channel c tracks voice c without a gate.
        Vastish k; live(k,4,4);
        for(int c=0;c<4;++c) k.inputs[Vastish::PITCH_IN].setVoltage(float(c)-1.f,c);
        k.process(args);
        for(int c=0;c<4;++c) assert(k.in[c].note==60+12*(c-1));
        // A mono AUDIO cable reaches every voice PITCH establishes.
        Vastish m; live(m,3,1); m.inputs[Vastish::AUDIO_IN].setVoltage(2.5f); m.process(args);
        assert(m.outputs[Vastish::AMP_OUT].getChannels()==3);
        for(int c=0;c<8;++c) assert(m.engine.voices[c].tap==(c<3 ? .5f : 0.f));
        // Two AUDIO channels under six PITCH channels: voices 3-6 read silence and stay under
        // their (closed) envelopes, so they are silent rather than gate-free.
        Vastish s; live(s,6,2); s.inputs[Vastish::AUDIO_IN].setVoltage(5,0); s.inputs[Vastish::AUDIO_IN].setVoltage(5,1);
        s.process(args);
        assert(s.outputs[Vastish::AMP_OUT].getChannels()==6);
        for(int c=0;c<6;++c) assert(s.in[c].audioConnected==(c<2));
        // With another wave, AUDIO IN is unused and does not establish voices.
        Vastish w; live(w,0,4); w.params[PITCH_WAVE].setValue(0); w.process(args);
        assert(w.outputs[Vastish::AMP_OUT].getChannels()==1);
    }
    // The range is saved in the patch. A 2.5 patch, which has no range, opens at ±5 V,
    // and a full restore sets it even on an instance that was at ±10 V.
    {
        Vastish v; json_t* saved=v.dataToJson();
        assert(json_integer_value(json_object_get(saved,"audioInRange"))==5); json_decref(saved);
        v.audioIn10V=true; saved=v.dataToJson(); assert(json_integer_value(json_object_get(saved,"audioInRange"))==10);
        Vastish r; r.dataFromJson(saved); assert(r.audioIn10V);
        json_object_del(saved,"audioInRange"); r.dataFromJson(saved); json_decref(saved); assert(!r.audioIn10V);
    }
    // Feedback: L back into AUDIO IN through a one-sample cable delay. Loop gain above
    // unity limits at the blocks' own nonlinearities or AUDIO IN's ±1 clamp; nothing faults.
    for(int type:{6,8,11,14}) {
        Vastish f; for(auto& o:f.outputs) o.channels=1;
        f.params[PITCH_WAVE].setValue(AUDIO_WAVE); f.params[ALG].setValue(1); f.params[MASTER].setValue(24);
        f.params[F1_TYPE].setValue(type); f.params[F1_ADJ].setValue(type==11 ? 24 : 90); f.params[F1_B].setValue(type==14 ? .97f : 1);
        f.params[F2_TYPE].setValue(0); f.params[F3_TYPE].setValue(0); f.params[AMP_ADJ].setValue(12);
        f.inputs[Vastish::AUDIO_IN].channels=1; float cable=5; // a kick to start the loop
        double peak=0;
        for(int i=0;i<96000;++i) {
            f.inputs[Vastish::AUDIO_IN].setVoltage(cable); f.process(args);
            cable=f.outputs[Vastish::LEFT_OUT].getVoltage(); assert(std::isfinite(cable)); peak=std::max(peak,double(std::abs(cable)));
        }
        assert(f.engine.faults==0 && peak>1);
    }
    // Chain mode (docs/CHAIN.md), with Rack's expander wiring and its end-of-frame message flip
    // emulated, and the modules processed in a random order each frame.
    {
        std::mt19937 order(5);
        auto wire=[](std::vector<Vastish*> row) {
            for(size_t k=0;k<row.size();++k) {
                row[k]->model=modelVastish;
                static int64_t nextId=1; if(row[k]->id<0) row[k]->id=nextId++;   // Rack gives each module an id
                row[k]->leftExpander.module=k ? row[k-1] : nullptr;
                row[k]->leftExpander.moduleId=k ? row[k-1]->id : -1;
                row[k]->rightExpander.module=k+1<row.size() ? row[k+1] : nullptr;
                row[k]->rightExpander.moduleId=k+1<row.size() ? row[k+1]->id : -1;
            }
        };
        auto frame=[&](std::vector<Vastish*> row) {
            auto shuffled=row; std::shuffle(shuffled.begin(),shuffled.end(),order);
            for(auto* v:shuffled) v->process(args);
            for(auto* v:row) if(v->leftExpander.messageFlipRequested) {
                std::swap(v->leftExpander.producerMessage,v->leftExpander.consumerMessage); v->leftExpander.messageFlipRequested=false;
            }
        };
        auto passThrough=[&](Vastish& v) {
            for(int i=0;i<PARAM_COUNT;++i) v.params[i].setValue(programs()[0][i]);
            v.params[ALG].setValue(1); v.params[MASTER].setValue(0); v.params[AMP_ADJ].setValue(0); v.params[PAN_ADJ].setValue(-1);
            for(int f:{F1_TYPE,F2_TYPE,F3_TYPE}) v.params[f].setValue(0);
            for(auto& o:v.outputs) o.channels=1;
        };
        // Three in a row, NONE blocks: the first takes AUDIO IN gate-free; the second and third
        // continue it, each one sample later, still gate-free; the outputs are the DC-blocked input.
        Vastish a, b, c; for(auto* v:{&a,&b,&c}) passThrough(*v);
        a.params[PITCH_WAVE].setValue(AUDIO_WAVE); a.inputs[Vastish::AUDIO_IN].channels=1;
        b.chainFromLeft=c.chainFromLeft=true; wire({&a,&b,&c});
        for(int n=0;n<4800;++n) frame({&a,&b,&c});                                   // MASTER ramps settle
        assert(b.chainState==Vastish::CHAIN_CONNECTED && c.chainState==Vastish::CHAIN_CONNECTED);
        Highpass dcB, dcC; std::vector<float> sent;
        for(int n=0;n<4800;++n) {
            a.inputs[Vastish::AUDIO_IN].setVoltage(float(std::sin(.003*n))*4); frame({&a,&b,&c});
            sent.push_back(a.in[0].audio);                                             // as AUDIO IN scaled it
            if(n>=2) {
                assert(b.outputs[Vastish::LEFT_OUT].getVoltage()==dcB.tick(sent[n-1],5,48000)*5);
                assert(c.outputs[Vastish::LEFT_OUT].getVoltage()==dcC.tick(sent[n-2],5,48000)*5);
            } else { dcB.tick(n ? sent[0] : 0.f,5,48000); dcC.tick(0,5,48000); }
        }
        // Notes, velocities and gates follow the left one's voices; this one's own PITCH and GATE
        // are ignored, and its own amp envelope runs on those gates.
        Vastish keys, follower; for(auto* v:{&keys,&follower}) passThrough(*v);
        keys.params[PITCH_WAVE].setValue(0); follower.params[AMP_A].setValue(0); follower.params[AMP_S].setValue(1);
        keys.inputs[Vastish::PITCH_IN].channels=keys.inputs[Vastish::GATE_IN].channels=3;
        keys.inputs[Vastish::VEL_IN].channels=1; keys.inputs[Vastish::VEL_IN].setVoltage(7);
        follower.inputs[Vastish::GATE_IN].channels=1; follower.inputs[Vastish::GATE_IN].setVoltage(0);   // its own, low
        follower.chainFromLeft=true; wire({&keys,&follower});
        for(int v=0;v<3;++v) { keys.inputs[Vastish::PITCH_IN].setVoltage(v-1.f,v); keys.inputs[Vastish::GATE_IN].setVoltage(v==1 ? 0 : 10,v); }
        for(int n=0;n<2400;++n) frame({&keys,&follower});
        for(int v=0;v<3;++v) {
            assert(follower.in[v].note==keys.in[v].note && follower.in[v].gate==(v!=1) && follower.engine.voices[v].gate==(v!=1));
            if(v!=1) assert(std::abs(follower.engine.voices[v].velocity-.7f)<1e-6f && follower.engine.voices[v].sources[11]>.9f);
        }
        assert(follower.outputs[Vastish::AMP_OUT].getChannels()==3);
        // Off, or with nothing (or something else) on the left, it is an ordinary Vastish again.
        follower.chainFromLeft=false; follower.inputs[Vastish::GATE_IN].setVoltage(10); frame({&keys,&follower});
        assert(follower.chainState==Vastish::CHAIN_OFF && !follower.in[0].chained && follower.in[0].gate && !follower.in[2].gate); // its own GATE
        follower.chainFromLeft=true; follower.leftExpander.module=nullptr; follower.process(args);
        assert(follower.chainState==Vastish::CHAIN_NO_VASTISH && !follower.in[0].chained);
        Module stranger; follower.leftExpander.module=&stranger; follower.process(args);
        assert(follower.chainState==Vastish::CHAIN_NO_VASTISH);
        // Placed back on the left after a note ended while apart, the left Vastish's last message
        // from before is never played: no one-sample gate, so no blip (review #15).
        follower.inputs[Vastish::GATE_IN].channels=0; wire({&keys,&follower});
        for(int n=0;n<2400;++n) frame({&keys,&follower});
        assert(follower.chainState==Vastish::CHAIN_CONNECTED && follower.in[0].gate && follower.engine.voices[0].gate);
        keys.rightExpander.module=nullptr; keys.rightExpander.moduleId=-1;
        follower.leftExpander.module=nullptr; follower.leftExpander.moduleId=-1;
        keys.inputs[Vastish::GATE_IN].setVoltage(0,0);                              // the note ends while apart
        for(int n=0;n<2400;++n) { keys.process(args); follower.process(args); }
        assert(!follower.engine.voices[0].gate);
        wire({&keys,&follower});
        for(int n=0;n<4;++n) {
            frame({&keys,&follower});
            assert(!follower.in[0].gate && !follower.engine.voices[0].gate);
        }
        assert(follower.chainState==Vastish::CHAIN_CONNECTED && follower.in[2].gate);   // and then it follows again
        // Saved with the patch; absent before 2.8; Initialize turns it off.
        json_t* saved=follower.dataToJson(); assert(json_is_true(json_object_get(saved,"chainFromLeft")));
        Vastish r; r.dataFromJson(saved); assert(r.chainFromLeft);
        json_object_del(saved,"chainFromLeft"); r.dataFromJson(saved); json_decref(saved); assert(!r.chainFromLeft);
        follower.leftExpander.module=nullptr;                                        // plugin_test covers Initialize
    }
    std::cout<<"PASS: audio-thread program loads, held-gate Panic, retrigger, editor endpoints, sample handoff and patch round-trip, poly AUX and FUN 3/4 outputs, OTIS link, AUDIO IN level, range, gating, key tracking, polyphonic voices and mono broadcast, saved range and feedback, three-module chain one sample per hop in random order, chained notes/gates/velocities, chain off and absent, no stale message when put back, saved chain\n";
}
