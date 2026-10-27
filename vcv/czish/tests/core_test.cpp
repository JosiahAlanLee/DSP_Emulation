// Meaningful parity tests against the pinned, original upstream code.
#include "../src/engine/Upd933.hpp"
#include "../../shared/DacStage.hpp"
#include "mame_ref_generated.hpp"
#include "reference/ymfm.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

using namespace dspemu;

static void yamaha() {
    for (int v = -32768; v <= 32767; ++v) {
        assert(dac::yamahaEncode(v) == uint16_t(ymfm::encode_fp(v)));
        assert(dac::yamahaRoundtrip(v) == ymfm::roundtrip_fp(v));
        assert(dac::yamahaDecode(dac::yamahaEncode(v)) == ymfm::roundtrip_fp(v));
    }
    for (int code = 0; code < 8192; ++code)
        assert(dac::yamahaDecode(code) == ymfm::decode_fp(int16_t(code)));
    for (int v : {-100000, -32769, 32768, 100000}) {
        assert(dac::yamahaEncode(v) == uint16_t(ymfm::encode_fp(v)));
        assert(dac::yamahaRoundtrip(v) == ymfm::roundtrip_fp(v));
    }
}

static void quantizers() {
    for (int bits : {4, 8, 12, 16}) {
        for (int v = -32768; v <= 32767; ++v) {
            auto w = dac::quantize(dac::Profile::Linear, v, bits);
            assert(w.value == dac::linearDecode(dac::linearEncode(v, bits), bits));
            assert(w.value <= v && v - int(w.value) < (1 << (16 - bits)));
        }
    }
    for (int v = -32768; v <= 32767; ++v) {
        auto w = dac::quantize(dac::Profile::Casio, v);
        assert(((w.code >> 12) & 3) <= 2);
        assert(w.value <= v && v - int(w.value) < 16);
    }
    for (auto profile : {dac::Profile::Yamaha, dac::Profile::Casio, dac::Profile::Linear})
        for (int bits : {4, 8, 12, 16}) for (int mask = 0; mask < 256; ++mask) {
            auto plain = dac::quantize(profile, -15001, bits);
            auto changed = dac::quantize(profile, -15001, bits, uint8_t(mask));
            uint8_t expected = plain.width >= 8 ? uint8_t(plain.high() ^ mask)
                                               : uint8_t(plain.high() ^ (mask & (0xff << (8 - plain.width))));
            assert(changed.high() == expected);
        }
}

static void clocks() {
    for (double host : {44100., 48000., 96000.}) for (double device : {1000., 16000., 40000., 55930., 96000., 192000.}) {
        dac::Stage stage;
        bool wasHigh = false;
        uint8_t previous = 0;
        int events = 0;
        for (int i = 0; i < int(host * 2); ++i) {
            float x = float(std::sin(2 * 3.141592653589793 * 437. * i / host));
            float y = stage.process(x, dac::Profile::Yamaha, 12, 0, 0, device, host);
            assert(std::isfinite(y) && std::abs(y) < 1.6f);
            assert(!(wasHigh && stage.tap.clk));
            if (stage.tap.clk) ++events;
            else assert(stage.tap.word == previous);
            wasHigh = stage.tap.clk; previous = stage.tap.word;
        }
        unsigned divider = unsigned(std::max(1., std::ceil(2 * device / host)));
        assert(stage.tap.divider == divider);
        assert(std::abs(events - 2. * device / divider) <= 2.);
    }
    dac::Stage stage;
    for (int i = 0; i < 100000; ++i) {
        double rate = i % 3 == 0 ? 120000 : (i % 3 == 1 ? 12000 : 55930);
        bool before = stage.tap.clk;
        stage.process(.3f, dac::Profile::Yamaha, 12, uint8_t(i), 0, rate, 48000);
        assert(!(before && stage.tap.clk));
    }
}

static double component(const std::vector<float>& samples, double frequency, double rate) {
    double re=0, im=0;
    constexpr double pi=3.1415926535897932384626433832795;
    for (size_t i=0;i<samples.size();++i) {
        double phase=2*pi*frequency*i/rate;
        re+=samples[i]*std::cos(phase); im-=samples[i]*std::sin(phase);
    }
    return 2*std::hypot(re,im)/samples.size();
}
static void resampling() {
    constexpr double host=48000, device=16000, pi=3.1415926535897932384626433832795;
    dac::Stage raw, antiAlias, clean;
    std::vector<float> unfilteredAlias, filteredAlias, heldImage, cleanImage;
    for(int i=0;i<48000;++i) {
        float high=float(.8*std::sin(2*pi*12000*i/host));
        float low=float(.8*std::sin(2*pi*1000*i/host));
        float a=raw.process(high,dac::Profile::Linear,16,0,0,device,host,false,false);
        float b=antiAlias.process(high,dac::Profile::Linear,16,0,0,device,host,true,false);
        float c=clean.process(low,dac::Profile::Linear,16,0,0,device,host,false,true);
        if(i>2000) { unfilteredAlias.push_back(a); filteredAlias.push_back(b); cleanImage.push_back(c); }
    }
    dac::Stage rawLow;
    for(int i=0;i<48000;++i) {
        float low=float(.8*std::sin(2*pi*1000*i/host));
        float a=rawLow.process(low,dac::Profile::Linear,16,0,0,device,host,false,false);
        if(i>2000) heldImage.push_back(a);
    }
    double alias=component(unfilteredAlias,4000,host), filtered=component(filteredAlias,4000,host);
    double image=component(heldImage,15000,host), cleaned=component(cleanImage,15000,host);
    assert(alias>.3 && filtered<alias*.02);
    assert(image>.01 && cleaned<image*.1);
    assert(component(cleanImage,1000,host)>.7);
}

static void chipParity() {
    cz::Upd933 tested;
    upd933_device reference;
    std::mt19937 random(0x933);
    for (int n = 0; n < 8; ++n) {
        for (auto p : {std::pair<int,int>{0x60, (53+n)*512}, {0x00, 0x7fff},
                       {0x20, 0x7fff}, {0x68, n << 13}, {0x98, 0}}) {
            tested.write(uint8_t(p.first+n), uint16_t(p.second));
            reference.registerWrite(uint8_t(p.first+n), uint16_t(p.second));
        }
    }
    const int bases[] = {0x00, 0x10, 0x20, 0x60, 0x68, 0x98, 0xb8};
    for (int i = 0; i < 240000; ++i) {
        if (i % 7 == 0) {
            int base = bases[random() % 7], n = random() % (base == 0xb8 ? 4 : 8);
            uint16_t value = uint16_t(random());
            tested.write(uint8_t(base+n), value);
            reference.registerWrite(uint8_t(base+n), value);
        }
        int a = tested.render(), b = reference.render();
        if (a != b) { std::cerr << "MAME mismatch at sample " << i << ": " << a << " vs " << b << "\n"; std::abort(); }
    }
    // The CV extension must stay inside the chip's defined transfer domain.
    for (int offset : {-10000, -127, 0, 127, 10000}) {
        tested.dcwOffset.fill(offset);
        for (int i = 0; i < 1000; ++i) tested.render();
    }
}

int main() {
    yamaha(); quantizers(); clocks(); resampling(); chipParity();
    std::cout << "PASS: exhaustive ymfm parity, quantizer masks, safe ByteBus clock, sinc alias/image rejection and 240000 samples against the pinned MAME chip sound path\n";
}
