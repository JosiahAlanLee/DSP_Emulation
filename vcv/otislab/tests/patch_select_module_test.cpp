// Patch select (2.9) through the real Rack module: each hit takes its patch from PATCH on the same
// sample, a stored patch plays exactly as the panel it was stored from, and unpatched nothing changes.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/OtisSampler.cpp"
#include <cassert>
#include <iostream>
Plugin* pluginInstance=nullptr;
static Module::ProcessArgs args(float sr=48000) { Module::ProcessArgs a{}; a.sampleRate=sr; a.sampleTime=1/sr; return a; }
static void bus(Input& p,uint8_t word,bool clocked=false,bool clk=false) {
    p.channels=clocked ? 9 : 8; dspemu::bus::Writer{}.write(p,word,clocked,clk);
}
static std::unique_ptr<OtisSampler> makeOtis() {
    auto m=std::make_unique<OtisSampler>(); m->model=modelOtisSampler;
    for(auto& p:m->outputs) p.channels=1;
    // A ramp that also steps along the sample, so start, direction and loop all change what is heard.
    std::vector<int16_t> pcm(24000);
    for(size_t i=0;i<pcm.size();++i) pcm[i]=int16_t((int(i*37%2000)-1000)*12+int(i/2000)*600-3600);
    m->engine.install(pcm,otis::captureRate(5));
    return m;
}
struct Panel { float pitch,start,end,k1,k2,position,size,interp; int loop; bool reverse; };
static const Panel A{7,.25f,.75f,.6f,.9f,.3f,9,3,1,true};
static const Panel B{0,0,1,1,1,0,8,9,0,false};
static void panel(OtisSampler& m,const Panel& p) {
    m.params[OtisSampler::PITCH].setValue(p.pitch); m.params[OtisSampler::START].setValue(p.start); m.params[OtisSampler::END].setValue(p.end);
    m.params[OtisSampler::K1].setValue(p.k1); m.params[OtisSampler::K2].setValue(p.k2); m.params[OtisSampler::POSITION].setValue(p.position);
    m.params[OtisSampler::SIZE].setValue(p.size); m.params[OtisSampler::INTERP].setValue(p.interp);
    m.params[OtisSampler::LOOP].setValue(float(p.loop)); m.params[OtisSampler::REVERSE].setValue(p.reverse ? 1.f : 0.f);
}
static void gate(OtisSampler& m,int voice,bool high,int channels=1) {
    auto& g=m.inputs[OtisSampler::GATE_IN]; g.channels=channels; g.setVoltage(high ? 10.f : 0.f,voice);
}
// Plays both modules through the same gates, the first with `word` on PATCH at each hit and 0 after it,
// and requires identical audio.
static void same(OtisSampler& x,OtisSampler& y,int word,int samples=30000) {
    auto a=args();
    for(int t=0;t<samples;++t) {
        bool hit=t%7000<40;
        gate(x,0,hit); gate(y,0,hit);
        if(word>=0) bus(x.inputs[OtisSampler::PATCH_IN],uint8_t(t%7000==0 ? word : 0));
        x.process(a); y.process(a);
        assert(x.outputs[OtisSampler::LEFT_OUT].getVoltage()==y.outputs[OtisSampler::LEFT_OUT].getVoltage());
    }
    assert(x.engine.playing() && y.engine.playing());
}
int main() {
    Context context; context.engine=new engine::Engine; contextSet(&context);
    Plugin plugin; plugin.slug="OtisLab"; plugin.version="2.11.2"; plugin.addModel(modelOtisSampler);
    rack::plugin::plugins.push_back(&plugin);
    auto a=args();
    {   // Unpatched, or patched at 0, stored patches change nothing: the panel plays.
        auto x=makeOtis(),y=makeOtis(); panel(*x,A); x->storePatch(1,x->panelPatch()); x->storePatch(3,x->panelPatch());
        panel(*x,B); panel(*y,B);
        same(*x,*y,-1); same(*x,*y,0);
    }
    {   // A hit on patch 1 plays exactly what a panel set to patch 1 plays, though PATCH falls right after it.
        auto x=makeOtis(),z=makeOtis(); panel(*x,A); x->storePatch(1,x->panelPatch()); panel(*x,B); panel(*z,A);
        same(*x,*z,1);
        // A patch that was never stored plays the panel.
        auto u=makeOtis(),b=makeOtis(); panel(*u,A); u->storePatch(1,u->panelPatch()); panel(*u,B); panel(*b,B);
        same(*u,*b,2);
    }
    {   // Per voice, held from the hit: panel moves and PATCH changes don't reach a voice on a stored patch.
        auto m=makeOtis(); panel(*m,A); m->storePatch(1,m->panelPatch()); panel(*m,B);
        auto& c=m->engine.controls;
        gate(*m,0,false,2); m->process(a);   // Rack's Schmitt triggers start high: a first high sample is no hit
        bus(m->inputs[OtisSampler::PATCH_IN],1); gate(*m,0,true,2); m->process(a);
        bus(m->inputs[OtisSampler::PATCH_IN],0); gate(*m,1,true,2); m->process(a); m->process(a);
        assert(c.startv[0]==.25f && c.endv[0]==.75f && c.pitch[0]==7.f/12 && c.k1v[0]==.6f && c.position[0]==.3f && c.sizeOctaves[0]==9);
        assert(c.startv[1]==0 && c.endv[1]==1 && c.pitch[1]==0 && c.k1v[1]==1 && c.sizeOctaves[1]==8);
        assert(m->engine.voices[0].reverse && !m->engine.voices[1].reverse);
        assert(m->engine.voices[0].loop==1 && m->engine.voices[1].loop==0);
        assert(m->engine.voices[0].interpMask!=m->engine.voices[1].interpMask);
        assert(m->lastPatch==0);
        // The panel moves; voice 0 stays on patch 1. REV turns only the panel's voice.
        m->params[OtisSampler::START].setValue(.9f); m->params[OtisSampler::REVERSE].setValue(1); m->process(a);
        assert(c.startv[0]==.25f && c.startv[1]==.9f);
        assert(m->engine.voices[0].reverse && m->engine.voices[1].reverse);
        m->params[OtisSampler::REVERSE].setValue(0); m->process(a);
        assert(m->engine.voices[0].reverse && !m->engine.voices[1].reverse);
        // CV adds to the stored patch as to the panel.
        auto& start=m->inputs[OtisSampler::START_IN]; start.channels=2; start.setVoltage(1,0); start.setVoltage(1,1); m->process(a);
        assert(std::abs(c.startv[0]-.35f)<1e-6f && c.startv[1]==1);
        start.channels=0; start.setVoltage(0,0); start.setVoltage(0,1);   // as Rack leaves an unplugged input
        // A voice holds its patch's number: storing patch 1 again reaches it, as a knob reaches the panel's voices.
        m->params[OtisSampler::START].setValue(.5f); m->storePatch(1,m->panelPatch()); m->process(a);
        assert(c.startv[0]==.5f && c.startv[1]==.5f);
        m->params[OtisSampler::START].setValue(.1f); m->process(a);
        assert(c.startv[0]==.5f && c.startv[1]==.1f);
    }
    {   // Re-storing REV turns only that patch's voices; clearing takes the panel's REV.
        auto m=makeOtis(); panel(*m,B);
        m->params[OtisSampler::LOOP].setValue(1); // avoid ping-pong direction changes
        m->storePatch(1,m->panelPatch()); m->storePatch(2,m->panelPatch());
        gate(*m,0,false,3); m->process(a);
        for(int i=0;i<3;++i) {
            bus(m->inputs[OtisSampler::PATCH_IN],uint8_t(i));
            gate(*m,i,true,3); m->process(a);
        }
        m->params[OtisSampler::REVERSE].setValue(1); m->process(a);
        assert(m->engine.voices[0].reverse && !m->engine.voices[1].reverse && !m->engine.voices[2].reverse);
        m->storePatch(1,m->panelPatch()); m->process(a);
        assert(m->engine.voices[0].reverse && m->engine.voices[1].reverse && !m->engine.voices[2].reverse);
        m->params[OtisSampler::REVERSE].setValue(0); m->storePatch(1,m->panelPatch()); m->process(a);
        assert(!m->engine.voices[0].reverse && !m->engine.voices[1].reverse && !m->engine.voices[2].reverse);
        m->params[OtisSampler::REVERSE].setValue(1); m->process(a);
        m->storePatch(1,std::nullopt); m->process(a);
        assert(m->engine.voices[0].reverse && m->engine.voices[1].reverse && !m->engine.voices[2].reverse);
        m->params[OtisSampler::REVERSE].setValue(0); m->process(a);
        assert(!m->engine.voices[0].reverse && !m->engine.voices[1].reverse && !m->engine.voices[2].reverse);
        // A hit changing patches leaves the fading old note's direction alone.
        m->params[OtisSampler::REVERSE].setValue(1); m->storePatch(2,m->panelPatch());
        m->params[OtisSampler::REVERSE].setValue(0);
        gate(*m,1,false,3); m->process(a);
        bus(m->inputs[OtisSampler::PATCH_IN],2); gate(*m,1,true,3); m->process(a);
        assert(m->engine.voices[1].restartAfterFade && !m->engine.voices[1].reverse);
        for(int i=0;i<200;++i) m->process(a);
        assert(!m->engine.voices[1].restartAfterFade && m->engine.voices[1].reverse);
    }
    {   // Clocked: a hit takes the word latched at the last CLK edge, not the bit lines' current state.
        auto m=makeOtis(); panel(*m,A); m->storePatch(2,m->panelPatch()); panel(*m,B);
        auto& c=m->engine.controls;
        bus(m->inputs[OtisSampler::PATCH_IN],2,true,true); m->process(a);
        bus(m->inputs[OtisSampler::PATCH_IN],0,true,false); gate(*m,0,true); m->process(a);
        assert(c.startv[0]==.25f && m->lastPatch==2);
        // The edge and the hit on the same sample: the hit gets the new word.
        gate(*m,0,false); m->process(a);
        bus(m->inputs[OtisSampler::PATCH_IN],0,true,false); m->process(a);
        bus(m->inputs[OtisSampler::PATCH_IN],0,true,true); gate(*m,0,true); m->process(a);
        assert(c.startv[0]==0 && m->lastPatch==0);
        // Unpatching shows no patch; the voice keeps the one it has until its next hit.
        m->inputs[OtisSampler::PATCH_IN].channels=0; m->process(a); assert(m->lastPatch==-1);
    }
    {   // Saved with the patch: stored ones restore exactly, never-stored ones stay empty, bad values clamp.
        auto m=makeOtis(); panel(*m,A); m->storePatch(2,m->panelPatch());
        json_t* saved=m->dataToJson(); json_t* list=json_object_get(saved,"patches");
        assert(json_array_size(list)==3 && json_is_null(json_array_get(list,0)) && json_is_object(json_array_get(list,1)));
        auto r=makeOtis(); r->dataFromJson(saved); json_decref(saved);
        auto p=r->storedPatch(2); assert(p && !r->storedPatch(1) && !r->storedPatch(3));
        assert(p->pitch==7 && p->start==.25f && p->end==.75f && p->k1==.6f && p->k2==.9f && p->position==.3f && p->size==9 && p->interp==3 && p->loop==1 && p->reverse);
        json_t* bad=json_loads(R"({"patches":[{"pitch":99,"start":-1,"end":"x","loop":7,"size":0},5]})",0,nullptr);
        r->dataFromJson(bad); json_decref(bad);
        p=r->storedPatch(1); assert(p && p->pitch==48 && p->start==0 && p->end==1 && p->loop==2 && p->size==1 && !r->storedPatch(2));
        json_t* old=json_object(); r->dataFromJson(old); json_decref(old); assert(!r->storedPatch(1));
        // Recall copies a patch to the panel; clear and Initialize forget them.
        auto q=makeOtis(); panel(*q,A); q->storePatch(3,q->panelPatch()); panel(*q,B); q->recallPatch(3);
        assert(q->params[OtisSampler::START].getValue()==.25f && q->params[OtisSampler::PITCH].getValue()==7 && q->params[OtisSampler::LOOP].getValue()==1);
        q->storePatch(3,std::nullopt); assert(!q->storedPatch(3));
        q->storePatch(1,q->panelPatch()); Module::ResetEvent e; q->onReset(e); assert(!q->storedPatch(1));
    }
    rack::plugin::plugins.clear();
    std::cout<<"PASS: patch select per hit (same sample, clocked, held per voice), stored patch = its panel sample for sample, unpatched unchanged, CV on top, REV on panel voices only, saved/clamped/recalled/cleared, live stored REV and clear, retrigger direction\n";
}
