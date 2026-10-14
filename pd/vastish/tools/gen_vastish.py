#!/usr/bin/env python3
"""Generates the vastish Pd patch set (vanilla Pd >= 0.52)."""
import os, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
os.makedirs(OUT, exist_ok=True)


def esc(t):
    return t.replace("$", "\\$").replace(";", " \\;").replace(",", " \\,")


class Patch:
    def __init__(self, name=None, w=900, h=700):
        self.name, self.w, self.h = name, w, h
        self.items = []      # (kind, text-or-patch, x, y)
        self.names = {}
        self.conns = []
        self._col, self._row = 0, 0

    def _pos(self, x, y):
        if x is None:
            x = 20 + self._col * 230
            y = 120 + self._row * 28
            self._row += 1
            if self._row > 22:
                self._row = 0
                self._col += 1
        return x, y

    def _add(self, kind, payload, name, x, y):
        x, y = self._pos(x, y)
        idx = len(self.items)
        self.items.append((kind, payload, x, y))
        if name:
            assert name not in self.names, name
            self.names[name] = idx
        return name if name else idx

    def obj(self, name, text, x=None, y=None):
        return self._add("obj", text, name, x, y)

    def msg(self, name, text, x=None, y=None):
        return self._add("msg", text, name, x, y)

    def text(self, t, x, y):
        return self._add("text", t, None, x, y)

    def atom(self, name, x, y, label, snd, w=6):
        return self._add("atom", (w, label, snd), name, x, y)

    def sub(self, name, patch, x=None, y=None):
        return self._add("sub", patch, name, x, y)

    def c(self, a, ao, b, bi):
        self.conns.append((a, ao, b, bi))

    def chain(self, *names):
        for a, b in zip(names, names[1:]):
            self.c(a, 0, b, 0)

    def idx(self, k):
        return self.names[k] if isinstance(k, str) else k

    def render(self, top=True):
        out = []
        if top:
            out.append(f"#N canvas 40 40 {self.w} {self.h} 12;")
        else:
            out.append(f"#N canvas 80 80 {self.w} {self.h} {self.name} 0;")
        for kind, p, x, y in self.items:
            if kind == "obj":
                out.append(f"#X obj {x} {y} {esc(p)};")
            elif kind == "msg":
                out.append(f"#X msg {x} {y} {esc(p)};")
            elif kind == "text":
                out.append(f"#X text {x} {y} {esc(p.replace(chr(92)+',', ','))};")
            elif kind == "atom":
                w, label, snd = p
                out.append(f"#X floatatom {x} {y} {w} 0 0 1 {label} - {snd} 0;")
            elif kind == "sub":
                out.append(p.render(top=False))
                out.append(f"#X restore {x} {y} pd {p.name};")
        for a, ao, b, bi in self.conns:
            out.append(f"#X connect {self.idx(a)} {ao} {self.idx(b)} {bi};")
        return "\n".join(out)

    def save(self, fname):
        with open(os.path.join(OUT, fname), "w") as f:
            f.write(self.render() + "\n")


def gated_sum(p, prefix, sources, selrecv):
    """sources: list of node names; gate each by (selector == i); returns sum node."""
    p.obj(prefix + "sel", f"r {selrecv}")
    last = None
    for i, s in enumerate(sources):
        p.obj(f"{prefix}eq{i}", f"== {i}")
        p.obj(f"{prefix}g{i}", "*~ 0")
        p.c(prefix + "sel", 0, f"{prefix}eq{i}", 0)
        p.c(f"{prefix}eq{i}", 0, f"{prefix}g{i}", 1)
        p.c(s, 0, f"{prefix}g{i}", 0)
        if last is None:
            last = f"{prefix}g{i}"
        else:
            p.obj(f"{prefix}s{i}", "+~")
            p.c(last, 0, f"{prefix}s{i}", 0)
            p.c(f"{prefix}g{i}", 0, f"{prefix}s{i}", 1)
            last = f"{prefix}s{i}"
    return last


# ------------------------------------------------------------ vctl
# Control path: Adjust + KeyTrack*(key-60) + VelTrack*vel + Src1*Depth1 + Src2*Depth2
p = Patch()
p.text("vctl <param> <voice-id> : Adj + KeyTrk*(key-60) + VelTrk*vel + Src1*Dpt1 + Src2*Dpt2", 20, 10)
p.obj("key", "receive~ $2-key")
p.obj("kt", "r vast-$1-kt")
p.obj("ktm", "*~ 0")
p.c("key", 0, "ktm", 0); p.c("kt", 0, "ktm", 1)
p.obj("vel", "receive~ $2-vel")
p.obj("vt", "r vast-$1-vt")
p.obj("vtm", "*~ 0")
p.c("vel", 0, "vtm", 0); p.c("vt", 0, "vtm", 1)
for n in ("1", "2"):
    p.obj("src" + n, "receive~ $2-src0")
    p.obj("s" + n, f"r vast-$1-s{n}")
    p.obj("pk" + n, "pack 0 $2")
    p.msg("set" + n, "set $2-src$1")
    p.chain("s" + n, "pk" + n, "set" + n, "src" + n)
    p.obj("d" + n, f"r vast-$1-d{n}")
    p.obj("dm" + n, "*~ 0")
    p.c("src" + n, 0, "dm" + n, 0); p.c("d" + n, 0, "dm" + n, 1)
p.obj("adj", "r vast-$1-adj")
p.obj("adjs", "sig~ 0")
p.c("adj", 0, "adjs", 0)
p.obj("a1", "+~"); p.c("ktm", 0, "a1", 0); p.c("vtm", 0, "a1", 1)
p.obj("a2", "+~"); p.c("a1", 0, "a2", 0); p.c("dm1", 0, "a2", 1)
p.obj("a3", "+~"); p.c("a2", 0, "a3", 0); p.c("dm2", 0, "a3", 1)
p.obj("a4", "+~"); p.c("a3", 0, "a4", 0); p.c("adjs", 0, "a4", 1)
p.obj("out", "outlet~", 20, 800)
p.c("a4", 0, "out", 0)
p.save("vctl.pd")

# ------------------------------------------------------------ vadsr
p = Patch()
p.text("vadsr <name> : inlet 1=gate on 0=off. A/D/R in ms \\, S 0-1", 20, 10)
p.obj("in", "inlet", 20, 40)
p.obj("sel", "sel 0")
p.obj("rf", "f 300")
p.obj("rr", "r vast-$1-r")
p.msg("rel", "0 $1")
p.obj("tb", "t b")
p.obj("af", "f 5")
p.obj("ra", "r vast-$1-a")
p.obj("pk", "pack 0 200 0.8")
p.obj("rd", "r vast-$1-d")
p.obj("rs", "r vast-$1-s")
p.msg("atk", "1 $1, $3 $2 $1")
p.obj("vl", "vline~")
p.obj("out", "outlet~", 20, 800)
p.c("in", 0, "sel", 0)
p.c("sel", 0, "rf", 0); p.c("rr", 0, "rf", 1); p.c("rf", 0, "rel", 0); p.c("rel", 0, "vl", 0)
p.c("sel", 1, "tb", 0); p.c("tb", 0, "af", 0); p.c("ra", 0, "af", 1)
p.c("af", 0, "pk", 0); p.c("rd", 0, "pk", 1); p.c("rs", 0, "pk", 2)
p.c("pk", 0, "atk", 0); p.c("atk", 0, "vl", 0); p.c("vl", 0, "out", 0)
p.save("vadsr.pd")

# ------------------------------------------------------------ vlfo
p = Patch()
p.text("vlfo <name> : rate Hz \\, shape 0 sine 1 tri 2 saw 3 square 4 s&h. inlet bang = phase reset", 20, 10)
p.obj("in", "inlet", 20, 40)
p.msg("z", "0")
p.obj("rate", "r vast-$1-rate")
p.obj("ph", "phasor~ 1")
p.c("in", 0, "z", 0); p.c("z", 0, "ph", 1); p.c("rate", 0, "ph", 0)
p.obj("sine", "cos~"); p.c("ph", 0, "sine", 0)
p.obj("tri", "expr~ 1-abs($v1*4-2)"); p.c("ph", 0, "tri", 0)
p.obj("saw", "expr~ $v1*2-1"); p.c("ph", 0, "saw", 0)
p.obj("sq", "expr~ ($v1<0.5)*2-1"); p.c("ph", 0, "sq", 0)
p.obj("nz", "noise~")
p.obj("sh", "samphold~"); p.c("nz", 0, "sh", 0); p.c("ph", 0, "sh", 1)
last = gated_sum(p, "w", ["sine", "tri", "saw", "sq", "sh"], "vast-$1-shape")
p.obj("out", "outlet~", 20, 800); p.c(last, 0, "out", 0)
p.save("vlfo.pd")

# ------------------------------------------------------------ vfun
p = Patch()
p.text("vfun <name> <voice-id> : eq 0 a+b 1 a-b 2 a*b 3 (a+b)/2 4 max 5 min 6 |a-b| 7 a*(b+1)/2", 20, 10)
for n in ("a", "b"):
    p.obj(n, "receive~ $2-src0")
    p.obj("r" + n, f"r vast-$1-{n}")
    p.obj("pk" + n, "pack 0 $2")
    p.msg("set" + n, "set $2-src$1")
    p.chain("r" + n, "pk" + n, "set" + n, n)
specs = ["+~", "-~", "*~", "+~", "max~", "min~", "-~", "expr~ $v1*($v2+1)*0.5"]
srcs = []
for i, s in enumerate(specs):
    p.obj(f"e{i}", s)
    p.c("a", 0, f"e{i}", 0); p.c("b", 0, f"e{i}", 1)
    node = f"e{i}"
    if i == 3:
        p.obj("h3", "*~ 0.5"); p.c("e3", 0, "h3", 0); node = "h3"
    if i == 6:
        p.obj("ab6", "abs~"); p.c("e6", 0, "ab6", 0); node = "ab6"
    srcs.append(node)
last = gated_sum(p, "q", srcs, "vast-$1-eq")
p.obj("out", "outlet~", 20, 800); p.c(last, 0, "out", 0)
p.save("vfun.pd")

# ------------------------------------------------------------ vblock (DSP function block)
FUNCS = []  # (name, units-doc, builder)


def fn(name, doc, sw="switch~"):
    def deco(b):
        FUNCS.append((name, doc, b, sw))
        return b
    return deco


def note2hz(s, src="A", hi=135, maxhz=20000):
    s.obj("cl", f"clip~ -20 {hi}"); s.c(src, 0, "cl", 0)
    s.obj("hz", "mtof~"); s.c("cl", 0, "hz", 0)
    s.obj("hzc", f"clip~ 5 {maxhz}"); s.c("hz", 0, "hzc", 0)
    return "hzc"


@fn("LOPASS", "1-pole lowpass. A=cutoff (note#)")
def _(s):
    f = note2hz(s); s.obj("lp", "lop~"); s.c("IN", 0, "lp", 0); s.c(f, 0, "lp", 1); return "lp"


@fn("HIPASS", "1-pole highpass. A=cutoff (note#)")
def _(s):
    f = note2hz(s); s.obj("lp", "lop~"); s.c("IN", 0, "lp", 0); s.c(f, 0, "lp", 1)
    s.obj("d", "-~"); s.c("IN", 0, "d", 0); s.c("lp", 0, "d", 1); return "d"


def vcf(s):
    f = note2hz(s, maxhz=16000)
    s.obj("q", "clip 0.5 40"); s.c("B", 0, "q", 0)
    s.obj("v", "vcf~ 1"); s.c("IN", 0, "v", 0); s.c(f, 0, "v", 1); s.c("q", 0, "v", 2)
    return "v"


@fn("LP2RES", "2-pole resonant lowpass. A=cutoff (note#) B=Q 0.5-40")
def _(s):
    v = vcf(s); s.obj("o", "*~ 1"); s.c(v, 1, "o", 0); return "o"


@fn("BANDPASS", "2-pole bandpass. A=center (note#) B=Q 0.5-40")
def _(s):
    v = vcf(s); s.obj("o", "*~ 1"); s.c(v, 0, "o", 0); return "o"


@fn("NOTCH", "notch. A=center (note#) B=Q 0.5-40")
def _(s):
    v = vcf(s); s.obj("d", "-~"); s.c("IN", 0, "d", 0); s.c(v, 0, "d", 1); return "d"


@fn("LP4", "4-pole ladder (bob~). A=cutoff (note#) B=resonance 0-1")
def _(s):
    f = note2hz(s, maxhz=12000)
    s.obj("rc", "clip 0 1"); s.c("B", 0, "rc", 0)
    s.obj("rm", "* 3.9"); s.c("rc", 0, "rm", 0)
    s.obj("rs", "sig~ 0"); s.c("rm", 0, "rs", 0)
    s.obj("bob", "bob~"); s.c("IN", 0, "bob", 0); s.c(f, 0, "bob", 1); s.c("rs", 0, "bob", 2)
    return "bob"


@fn("SHAPER", "sine wavefolder. A=amount 0.05-16 (1 = gentle)")
def _(s):
    s.obj("ac", "clip~ 0.05 16"); s.c("A", 0, "ac", 0)
    s.obj("m", "*~"); s.c("IN", 0, "m", 0); s.c("ac", 0, "m", 1)
    s.obj("m2", "*~ 0.25"); s.c("m", 0, "m2", 0)
    s.obj("ph", "-~ 0.25"); s.c("m2", 0, "ph", 0)
    s.obj("cs", "cos~"); s.c("ph", 0, "cs", 0)
    return "cs"


@fn("DIST", "tanh saturation. A=drive dB")
def _(s):
    s.obj("db", "+~ 100"); s.c("A", 0, "db", 0)
    s.obj("g", "dbtorms~"); s.c("db", 0, "g", 0)
    s.obj("m", "*~"); s.c("IN", 0, "m", 0); s.c("g", 0, "m", 1)
    s.obj("t", "expr~ tanh($v1)*0.8"); s.c("m", 0, "t", 0)
    return "t"


@fn("SINE", "sine osc (ignores input). A=pitch (note#)")
def _(s):
    s.obj("cl", "clip~ -20 135"); s.c("A", 0, "cl", 0)
    s.obj("hz", "mtof~"); s.c("cl", 0, "hz", 0)
    s.obj("o", "osc~"); s.c("hz", 0, "o", 0); return "o"


@fn("RINGMOD", "input x sine. A=sine pitch (note#)")
def _(s):
    s.obj("cl", "clip~ -20 135"); s.c("A", 0, "cl", 0)
    s.obj("hz", "mtof~"); s.c("cl", 0, "hz", 0)
    s.obj("o", "osc~"); s.c("hz", 0, "o", 0)
    s.obj("m", "*~"); s.c("IN", 0, "m", 0); s.c("o", 0, "m", 1); return "m"


@fn("GAIN", "gain. A=dB (-100 = silent)")
def _(s):
    s.obj("db", "+~ 100"); s.c("A", 0, "db", 0)
    s.obj("g", "dbtorms~"); s.c("db", 0, "g", 0)
    s.obj("m", "*~"); s.c("IN", 0, "m", 0); s.c("g", 0, "m", 1); return "m"


@fn("CRUSH", "bit reduction. A=bits 1-16")
def _(s):
    s.obj("bc", "clip~ 1 16"); s.c("A", 0, "bc", 0)
    s.obj("q", "expr~ floor($v1*pow(2, $v2-1)+0.5)/pow(2, $v2-1)")
    s.c("IN", 0, "q", 0); s.c("bc", 0, "q", 1); return "q"


@fn("DECIMATE", "sample-rate reduction. A=rate (note#)")
def _(s):
    f = note2hz(s, maxhz=44100)
    s.obj("ph", "phasor~"); s.c(f, 0, "ph", 0)
    s.obj("sh", "samphold~"); s.c("IN", 0, "sh", 0); s.c("ph", 0, "sh", 1); return "sh"


@fn("COMB", "feedback comb. A=tuning (note#) B=feedback -0.98..0.98", sw="switch~ 16")
def _(s):
    s.obj("cl", "clip~ -20 100"); s.c("A", 0, "cl", 0)
    s.obj("hz", "mtof~"); s.c("cl", 0, "hz", 0)
    s.obj("ms", "expr~ 1000/max($v1, 5)"); s.c("hz", 0, "ms", 0)
    s.obj("rd", "delread4~ $0-comb"); s.c("ms", 0, "rd", 0)
    s.obj("fbc", "clip -0.98 0.98"); s.c("B", 0, "fbc", 0)
    s.obj("fb", "*~ 0"); s.c("rd", 0, "fb", 0); s.c("fbc", 0, "fb", 1)
    s.obj("sum", "+~"); s.c("IN", 0, "sum", 0); s.c("fb", 0, "sum", 1)
    s.obj("wr", "delwrite~ $0-comb 300"); s.c("sum", 0, "wr", 0)
    s.obj("o", "*~ 0.5"); s.c("sum", 0, "o", 0); return "o"


p = Patch(w=1100)
p.text("vblock <name> <voice-id> : one VAST-style DSP function slot. type 0 = NONE (bypass)", 20, 10)
p.obj("IN", "inlet~", 20, 40)
p.obj("ctl", "vctl $1 $2", 200, 40)
p.obj("B", "r vast-$1-b", 320, 40)
p.obj("type", "r vast-$1-type", 450, 40)
p.obj("byeq", "== 0", 450, 70)
p.obj("byp", "*~ 1", 20, 100)
p.c("IN", 0, "byp", 0); p.c("type", 0, "byeq", 0); p.c("byeq", 0, "byp", 1)
last = "byp"
for i, (name, doc, build, sw) in enumerate(FUNCS, start=1):
    s = Patch(name=name, w=500, h=500)
    s.text(doc, 20, 10)
    s.obj("IN", "inlet~", 20, 40)
    s.obj("A", "inlet~", 120, 40)
    s.obj("B", "inlet", 220, 40)
    s.obj("on", "inlet", 320, 40)
    s.obj("sw", sw, 320, 70)
    s.c("on", 0, "sw", 0)
    o = build(s)
    s.obj("out", "outlet~", 20, 460)
    s.c(o, 0, "out", 0)
    sx, sy = 20 + ((i - 1) % 5) * 200, 160 + ((i - 1) // 5) * 90
    p.sub(f"f{i}", s, sx, sy)
    p.obj(f"eq{i}", f"== {i}", sx + 120, sy - 25)
    p.c("type", 0, f"eq{i}", 0); p.c(f"eq{i}", 0, f"f{i}", 3)
    p.c("IN", 0, f"f{i}", 0); p.c("ctl", 0, f"f{i}", 1); p.c("B", 0, f"f{i}", 2)
    p.obj(f"s{i}", "+~", sx, sy + 30)
    p.c(last, 0, f"s{i}", 0); p.c(f"f{i}", 0, f"s{i}", 1)
    last = f"s{i}"
p.obj("out", "outlet~", 20, 520)
p.c(last, 0, "out", 0)
p.save("vblock.pd")

# ------------------------------------------------------------ vvoice
SOURCES = ["OFF", "ENV2", "ENV3", "LFO1", "LFO2", "FUN1", "FUN2", "MWHEEL", "VEL", "KEYNUM", "RAND", "AMPENV"]
# algorithms: coefficients a1 a2 | b1 b2 b3 b4 | c1 c2 c3
#   F1in = P ; F2in = a1*P + a2*F1 ; F3in = b1*P + b2*F1 + b3*F2 + b4*F1*F2 ; OUT = c1*F1 + c2*F2 + c3*F3
ALGS = {
    1: ("P > F1 > F2 > F3", [0, 1, 0, 0, 1, 0, 0, 0, 1]),
    2: ("(P>F1 + P>F2) > F3", [1, 0, 0, 0.5, 0.5, 0, 0, 0, 1]),
    3: ("P > F1 > (F2 + F3)", [0, 1, 0, 1, 0, 0, 0, 0.5, 0.5]),
    4: ("(P>F1 x P>F2) > F3  [ring]", [1, 0, 0, 0, 0, 1, 0, 0, 1]),
    5: ("P>F1 + P>F2 + P>F3", [1, 0, 1, 0, 0, 0, 0.4, 0.4, 0.4]),
    6: ("P>F1>F2 + P>F3", [0, 1, 1, 0, 0, 0, 0, 0.5, 0.5]),
}

p = Patch(w=1100, h=800)
p.text("vvoice : one voice. inlet: note vel (vel 0 = release)", 20, 10)
p.obj("in", "inlet", 20, 40)
p.obj("tll", "t l l", 20, 65)
# first (right): key / vel / rand
p.obj("up1", "unpack f f", 200, 90)
p.obj("km", "- 60", 200, 115); p.obj("ks", "sig~", 200, 140); p.obj("ksend", "send~ $0-key", 200, 165)
p.obj("kn", "/ 127", 300, 115); p.obj("kns", "sig~", 300, 140); p.obj("knsend", "send~ $0-src9", 300, 165)
p.obj("vsel", "sel 0", 420, 115); p.obj("vn", "/ 127", 420, 140); p.obj("vs", "sig~", 420, 165)
p.obj("vsend", "send~ $0-vel", 420, 190); p.obj("vsend2", "send~ $0-src8", 520, 190)
p.obj("rnd", "random 10001", 620, 140); p.obj("rn1", "/ 5000", 620, 165); p.obj("rn2", "- 1", 620, 190)
p.obj("rs", "sig~", 620, 215); p.obj("rsend", "send~ $0-src10", 620, 240)
p.c("in", 0, "tll", 0); p.c("tll", 1, "up1", 0)
p.chain("km", "ks", "ksend"); p.c("up1", 0, "km", 0)
p.chain("kn", "kns", "knsend"); p.c("up1", 0, "kn", 0)
p.c("up1", 1, "vsel", 0); p.c("vsel", 1, "vn", 0); p.chain("vn", "vs", "vsend"); p.c("vs", 0, "vsend2", 0)
p.obj("rtb", "t b", 620, 115); p.c("vsel", 1, "rtb", 0); p.chain("rtb", "rnd", "rn1", "rn2", "rs", "rsend")
# second (left): gate
p.obj("up2", "unpack f f", 20, 90)
p.obj("gate", "!= 0", 20, 115)
p.obj("gsel", "sel 1", 120, 140)
p.c("tll", 0, "up2", 0); p.c("up2", 1, "gate", 0); p.c("gate", 0, "gsel", 0)
# sources
p.obj("zero", "sig~ 0", 20, 280); p.obj("zsend", "send~ $0-src0", 20, 305); p.c("zero", 0, "zsend", 0)
p.obj("aenv", "vadsr amp", 120, 280)
p.obj("env2", "vadsr env2", 220, 280); p.obj("e2s", "send~ $0-src1", 220, 305)
p.obj("env3", "vadsr env3", 340, 280); p.obj("e3s", "send~ $0-src2", 340, 305)
p.obj("lfo1", "vlfo lfo1", 460, 280); p.obj("l1s", "send~ $0-src3", 460, 305)
p.obj("lfo2", "vlfo lfo2", 580, 280); p.obj("l2s", "send~ $0-src4", 580, 305)
p.obj("fun1", "vfun fun1 $0", 700, 280); p.obj("f1s", "send~ $0-src5", 700, 305)
p.obj("fun2", "vfun fun2 $0", 820, 280); p.obj("f2s", "send~ $0-src6", 820, 305)
p.obj("mw", "r vast-mw", 940, 230); p.msg("mwm", "$1 20", 940, 255); p.obj("mwl", "line~", 940, 280)
p.obj("mws", "send~ $0-src7", 940, 305)
p.chain("mw", "mwm", "mwl", "mws")
p.obj("aes", "send~ $0-src11", 120, 330)
for e in ("aenv", "env2", "env3"):
    p.c("gate", 0, e, 0)
p.c("aenv", 0, "aes", 0); p.c("env2", 0, "e2s", 0); p.c("env3", 0, "e3s", 0)
p.c("gsel", 0, "lfo1", 0); p.c("gsel", 0, "lfo2", 0)
p.c("lfo1", 0, "l1s", 0); p.c("lfo2", 0, "l2s", 0)
p.c("fun1", 0, "f1s", 0); p.c("fun2", 0, "f2s", 0)
# pitch block
p.obj("pctl", "vctl pitch $0", 20, 380)
p.obj("pcl", "clip~ -20 135", 20, 405); p.obj("phz", "mtof~", 20, 430); p.obj("ph", "phasor~", 20, 455)
p.chain("pctl", "pcl", "phz", "ph")
p.obj("w_saw", "expr~ $v1*2-1", 20, 490)
p.obj("pw", "r vast-pitch-pw", 160, 465)
p.obj("w_sq", "expr~ ($v1>$f2)*2-1", 160, 490); p.c("pw", 0, "w_sq", 1)
p.obj("w_tri", "expr~ 1-abs($v1*4-2)", 320, 490)
p.obj("w_sin", "cos~", 480, 490)
p.obj("w_nz", "noise~", 560, 490)
for w in ("w_saw", "w_sq", "w_tri", "w_sin"):
    p.c("ph", 0, w, 0)
P = gated_sum(p, "pw_", ["w_saw", "w_sq", "w_tri", "w_sin", "w_nz"], "vast-pitch-wave")
# algorithm routing
p.obj("alg", "r vast-alg", 700, 380)
p.obj("asel", "sel " + " ".join(str(k) for k in ALGS), 700, 405)
p.obj("aup", "unpack " + " ".join(["f"] * 9), 700, 480)
for j, (k, (desc, co)) in enumerate(ALGS.items()):
    p.msg(f"am{k}", " ".join(str(c) for c in co), 700 + j * 0, 430 + 0)  # positions overlap; fine below
# re-position algorithm msgs neatly
for j, k in enumerate(ALGS):
    i = p.names[f"am{k}"]
    kind, t, x, y = p.items[i]
    p.items[i] = (kind, t, 700 + (j % 3) * 130, 430 + (j // 3) * 22)
    p.c("alg", 0, "asel", 0)
    p.c("asel", j, f"am{k}", 0)
    p.c(f"am{k}", 0, "aup", 0)
p.conns = list(dict.fromkeys(p.conns))
coef = ["a1", "a2", "b1", "b2", "b3", "b4", "c1", "c2", "c3"]
p.obj("F1", "vblock f1 $0", 20, 560)
p.c(P, 0, "F1", 0)
def gain(name, src, ci, x, y):
    p.obj(name, "*~ 0", x, y); p.c(src, 0, name, 0); p.c("aup", ci, name, 1)
gain("ga1", P, 0, 200, 590); gain("ga2", "F1", 1, 280, 590)
p.obj("f2in", "+~", 200, 615); p.c("ga1", 0, "f2in", 0); p.c("ga2", 0, "f2in", 1)
p.obj("F2", "vblock f2 $0", 200, 640); p.c("f2in", 0, "F2", 0)
gain("gb1", P, 2, 400, 670); gain("gb2", "F1", 3, 480, 670); gain("gb3", "F2", 4, 560, 670)
p.obj("ring", "*~", 640, 645); p.c("F1", 0, "ring", 0); p.c("F2", 0, "ring", 1)
gain("gb4", "ring", 5, 640, 670)
p.obj("f3a", "+~", 400, 695); p.c("gb1", 0, "f3a", 0); p.c("gb2", 0, "f3a", 1)
p.obj("f3b", "+~", 400, 720); p.c("f3a", 0, "f3b", 0); p.c("gb3", 0, "f3b", 1)
p.obj("f3c", "+~", 400, 745); p.c("f3b", 0, "f3c", 0); p.c("gb4", 0, "f3c", 1)
p.obj("F3", "vblock f3 $0", 400, 770); p.c("f3c", 0, "F3", 0)
gain("gc1", "F1", 6, 700, 800); gain("gc2", "F2", 7, 780, 800); gain("gc3", "F3", 8, 860, 800)
p.obj("oa", "+~", 700, 825); p.c("gc1", 0, "oa", 0); p.c("gc2", 0, "oa", 1)
p.obj("ob", "+~", 700, 850); p.c("oa", 0, "ob", 0); p.c("gc3", 0, "ob", 1)
# amp stage: ampenv^2 * dbtorms(ampctl)
p.obj("actl", "vctl amp $0", 900, 700)
p.obj("adb", "+~ 100", 900, 725); p.obj("arms", "dbtorms~", 900, 750)
p.chain("actl", "adb", "arms")
p.obj("asq", "*~", 120, 355); p.c("aenv", 0, "asq", 0); p.c("aenv", 0, "asq", 1)
p.obj("m1", "*~", 700, 875); p.c("ob", 0, "m1", 0); p.c("asq", 0, "m1", 1)
p.obj("m2", "*~", 700, 900); p.c("m1", 0, "m2", 0); p.c("arms", 0, "m2", 1)
# pan
p.obj("pnctl", "vctl pan $0", 1000, 760)
p.obj("pnc", "clip~ -1 1", 1000, 785); p.obj("pn1", "+~ 1", 1000, 810); p.obj("pn2", "*~ 0.125", 1000, 835)
p.chain("pnctl", "pnc", "pn1", "pn2")
p.obj("cl", "cos~", 1000, 860); p.c("pn2", 0, "cl", 0)
p.obj("pr", "-~ 0.25", 1080, 860); p.c("pn2", 0, "pr", 0)
p.obj("cr", "cos~", 1080, 885); p.c("pr", 0, "cr", 0)
p.obj("L", "*~", 700, 930); p.c("m2", 0, "L", 0); p.c("cl", 0, "L", 1)
p.obj("R", "*~", 780, 930); p.c("m2", 0, "R", 0); p.c("cr", 0, "R", 1)
p.obj("oL", "outlet~", 700, 960); p.obj("oR", "outlet~", 780, 960)
p.c("L", 0, "oL", 0); p.c("R", 0, "oR", 0)
if os.environ.get("VDEBUG"):
    for k in os.environ["VDEBUG"].split(","):
        p.obj("dbe" + k, "env~ 16384"); p.obj("dbp" + k, "print " + k)
        p.c(k, 0, "dbe" + k, 0); p.c("dbe" + k, 0, "dbp" + k, 0)
p.save("vvoice.pd")

# ------------------------------------------------------------ parameters & programs
CTLS = ["pitch", "f1", "f2", "f3", "amp", "pan"]
CTL_SUFFIX = ["adj", "kt", "vt", "s1", "d1", "s2", "d2"]


def defaults():
    d = {"alg": 1, "master": 6, "pitch-wave": 0, "pitch-pw": 0.5}
    for c in CTLS:
        for s in CTL_SUFFIX:
            d[f"{c}-{s}"] = 0
    d["pitch-adj"] = 60; d["pitch-kt"] = 1; d["amp-adj"] = -12
    for f in ("f1", "f2", "f3"):
        d[f"{f}-type"] = 0; d[f"{f}-b"] = 0
    for e, v in (("amp", (5, 200, 0.8, 300)), ("env2", (5, 300, 0.5, 300)), ("env3", (5, 300, 0.5, 300))):
        for k, x in zip("adsr", v):
            d[f"{e}-{k}"] = x
    d.update({"lfo1-rate": 5, "lfo1-shape": 0, "lfo2-rate": 0.3, "lfo2-shape": 0})
    for f in ("fun1", "fun2"):
        d.update({f"{f}-a": 0, f"{f}-b": 0, f"{f}-eq": 0})
    return d


F = {n: i for i, (n, *_r) in enumerate(FUNCS, start=1)}
S = {n: i for i, n in enumerate(SOURCES)}

PROGRAMS = [
    ("INIT", {}),
    ("Ladder Bass", {
        "alg": 1, "pitch-wave": 0,
        "f1-type": F["LP4"], "f1-adj": 52, "f1-kt": 0.5, "f1-vt": 24, "f1-b": 0.55,
        "f1-s1": S["ENV2"], "f1-d1": 55,
        "env2-a": 1, "env2-d": 260, "env2-s": 0.05, "env2-r": 150,
        "f2-type": F["DIST"], "f2-adj": 8,
        "f3-type": F["GAIN"], "f3-adj": -4,
        "amp-adj": -14, "amp-vt": 12, "amp-a": 2, "amp-d": 500, "amp-s": 0.6, "amp-r": 120}),
    ("Ring Bell", {
        "alg": 4,
        "f1-type": F["SINE"], "f1-adj": 60, "f1-kt": 1,
        "f2-type": F["SINE"], "f2-adj": 77.4, "f2-kt": 1, "f2-s1": S["ENV3"], "f2-d1": 0.6,
        "f3-type": F["LOPASS"], "f3-adj": 125, "f3-kt": 0.5, "f3-s1": S["ENV2"], "f3-d1": 0,
        "f3-s2": S["AMPENV"], "f3-d2": 10,
        "env3-a": 0, "env3-d": 1500, "env3-s": 0, "env3-r": 1500,
        "amp-adj": -24, "amp-vt": 18, "amp-a": 1, "amp-d": 3000, "amp-s": 0, "amp-r": 2500,
        "pan-s1": S["KEYNUM"], "pan-d1": 1.2, "pan-adj": -0.6}),
    ("Fold Pad", {
        "alg": 1, "pitch-wave": 2, "pitch-s1": S["LFO2"], "pitch-d1": 0.08,
        "lfo2-rate": 5.2, "lfo1-rate": 0.35, "lfo1-shape": 1,
        "f1-type": F["SHAPER"], "f1-adj": 1.6, "f1-s1": S["LFO1"], "f1-d1": 1.3,
        "f1-s2": S["MWHEEL"], "f1-d2": 5,
        "f2-type": F["LP2RES"], "f2-adj": 78, "f2-kt": 0.5, "f2-b": 2.5, "f2-s1": S["ENV2"], "f2-d1": 22,
        "env2-a": 1500, "env2-d": 2500, "env2-s": 0.5, "env2-r": 2000,
        "f3-type": F["GAIN"], "f3-adj": -3,
        "amp-adj": -18, "amp-a": 900, "amp-d": 1000, "amp-s": 0.85, "amp-r": 1800,
        "pan-s1": S["RAND"], "pan-d1": 0.7}),
    ("Crush Lead", {
        "alg": 6, "pitch-wave": 1, "pitch-pw": 0.3,
        "lfo1-rate": 6, "lfo1-shape": 1, "lfo2-rate": 0.25,
        "fun1-a": S["LFO1"], "fun1-b": S["ENV2"], "fun1-eq": 2,
        "f1-type": F["CRUSH"], "f1-adj": 5, "f1-s1": S["FUN1"], "f1-d1": 3, "f1-s2": S["MWHEEL"], "f1-d2": -3,
        "f2-type": F["BANDPASS"], "f2-adj": 74, "f2-kt": 1, "f2-b": 2.5, "f2-s1": S["ENV2"], "f2-d1": 18,
        "env2-a": 400, "env2-d": 800, "env2-s": 0.7, "env2-r": 300,
        "f3-type": F["DECIMATE"], "f3-adj": 100, "f3-kt": 1, "f3-s1": S["LFO2"], "f3-d1": -10,
        "amp-adj": -10, "amp-a": 8, "amp-d": 300, "amp-s": 0.75, "amp-r": 200}),
    ("Comb Pluck", {
        "alg": 1, "pitch-wave": 4,
        "f1-type": F["GAIN"], "f1-adj": -100, "f1-s1": S["ENV2"], "f1-d1": 100,
        "env2-a": 0, "env2-d": 40, "env2-s": 0, "env2-r": 25,
        "f2-type": F["COMB"], "f2-adj": 60, "f2-kt": 1, "f2-b": 0.985,
        "f3-type": F["LOPASS"], "f3-adj": 88, "f3-kt": 0.5, "f3-vt": 18,
        "amp-adj": 0, "amp-a": 1, "amp-d": 3500, "amp-s": 0, "amp-r": 500,
        "pan-s1": S["KEYNUM"], "pan-d1": 1.4, "pan-adj": -0.7}),
]


def prog_msg(over):
    d = defaults(); d.update(over)
    return "; " + "; ".join(f"vast-{k} {v}" for k, v in d.items())


# ------------------------------------------------------------ edit panel (subpatch)
ed = Patch(name="edit", w=1250, h=780)
ed.text("Edit panel. Values update when a program loads. Drag or shift-drag numbers \\, or click+type.", 20, 8)
ed.text("SOURCES: " + " ".join(f"{i}={n}" for i, n in enumerate(SOURCES)), 20, 30)
ed.text("FUNCTIONS: 0=NONE " + " ".join(f"{i}={n}" for i, (n, *_r) in enumerate(FUNCS, start=1)), 20, 50)
ed.text("ALGS: " + " ".join(f"{k}={d.replace(' ', '')}" for k, (d, _c) in ALGS.items()), 20, 70)
ed.text("Control path per param: adj + kt*(key-60) + vt*vel(0-1) + src1*d1 + src2*d2. Freq params are note numbers (60 = middle C)", 20, 90)
HELPER_X = 1400


def ed_param(param, x, y, label=None):
    n = f"a_{param}"
    ed.atom(n, x, y, label or param, f"vast-{param}")
    ed.obj(f"r_{param}", f"r vast-{param}", HELPER_X, y)
    ed.msg(f"m_{param}", "set $1", HELPER_X + 150, y)
    ed.c(f"r_{param}", 0, f"m_{param}", 0)
    ed.c(f"m_{param}", 0, n, 0)


cols = [("pitch", ["wave", "pw"]), ("f1", ["type", "b"]), ("f2", ["type", "b"]),
        ("f3", ["type", "b"]), ("amp", []), ("pan", [])]
for ci, (c, extra) in enumerate(cols):
    x = 20 + ci * 160
    ed.text(c.upper(), x, 120)
    y = 145
    for e in extra:
        ed_param(f"{c}-{e}", x, y, e); y += 24
    for s in CTL_SUFFIX:
        ed_param(f"{c}-{s}", x, y, s); y += 24
ed.text("ENVELOPES (ms / sustain 0-1)", 20, 420)
for ei, e in enumerate(("amp", "env2", "env3")):
    x = 20 + ei * 160
    ed.text(e.upper(), x, 445)
    for k, s in enumerate("adsr"):
        ed_param(f"{e}-{s}", x, 470 + k * 24, s)
ed.text("LFOs (shape 0 sin 1 tri 2 saw 3 sq 4 s&h)", 500, 420)
for li, l in enumerate(("lfo1", "lfo2")):
    x = 500 + li * 160
    ed.text(l.upper(), x, 445)
    ed_param(f"{l}-rate", x, 470, "rate"); ed_param(f"{l}-shape", x, 494, "shape")
ed.text("FUNs: a \\, b = source# eq: 0 a+b 1 a-b 2 a*b 3 avg 4 max 5 min 6 |a-b| 7 a*(b+1)/2", 820, 420)
for fi, f in enumerate(("fun1", "fun2")):
    x = 820 + fi * 160
    ed.text(f.upper(), x, 445)
    for k, s in enumerate("ab"):
        ed_param(f"{f}-{s}", x, 470 + k * 24, s)
    ed_param(f"{f}-eq", x, 518, "eq")
ed.text("GLOBAL", 20, 600)
ed_param("alg", 20, 625, "algorithm")
ed_param("master", 20, 649, "master-dB")
ed_param("mw", 20, 673, "modwheel-0-1")

# ------------------------------------------------------------ programs (subpatch)
pr = Patch(name="programs", w=600, h=400)
pr.obj("r", "r vast-prog", 20, 20)
pr.obj("sel", "sel " + " ".join(str(i) for i in range(len(PROGRAMS))), 20, 50)
pr.c("r", 0, "sel", 0)
for i, (name, over) in enumerate(PROGRAMS):
    pr.msg(f"p{i}", prog_msg(over), 20 + i * 40, 90 + i * 40)
    pr.c("sel", i, f"p{i}", 0)

# ------------------------------------------------------------ main
m = Patch(w=900, h=620)
m.text("vastish : a VAST-flavoured 8-voice synth in vanilla Pd (0.52+). Pitch block > F1 > F2 > F3 > Amp \\, wired by algorithm", 20, 10)
m.text("Click a program \\, then play MIDI. Open [pd edit] to change parameters. Turn DSP on first.", 20, 32)
m.msg("dspon", "; pd dsp 1", 20, 60); m.msg("dspoff", "; pd dsp 0", 110, 60)
m.obj("lb", "loadbang", 220, 60); m.msg("lb1", "1", 220, 85); m.obj("sp", "s vast-prog", 220, 110)
m.c("lb", 0, "lb1", 0); m.c("lb1", 0, "sp", 0)
m.text("PROGRAMS", 20, 150)
for i, (name, _o) in enumerate(PROGRAMS):
    m.msg(f"pm{i}", str(i), 20, 175 + i * 26)
    m.c(f"pm{i}", 0, "sp", 0)
    m.text(name, 60, 175 + i * 26)
m.sub("edit", ed, 220, 175)
m.sub("programs", pr, 220, 205)
m.obj("nin", "notein", 420, 60)
m.obj("rnote", "r vast-note", 520, 60); m.obj("unote", "unpack f f", 520, 85)
m.c("rnote", 0, "unote", 0)
m.obj("poly", "poly 8 1", 420, 120)
m.c("nin", 0, "poly", 0); m.c("nin", 1, "poly", 1)
m.c("unote", 0, "poly", 0); m.c("unote", 1, "poly", 1)
m.obj("pk", "pack f f f", 420, 150); m.c("poly", 0, "pk", 0); m.c("poly", 1, "pk", 1); m.c("poly", 2, "pk", 2)
m.obj("clone", "clone -s 1 vvoice 8", 420, 180); m.c("pk", 0, "clone", 0)
m.obj("cin", "ctlin 1", 640, 60); m.obj("cdiv", "/ 127", 640, 85); m.obj("smw", "s vast-mw", 640, 110)
m.chain("cin", "cdiv", "smw")
m.obj("rm", "r vast-master", 640, 160); m.obj("rm1", "+ 100", 640, 185); m.obj("rm2", "dbtorms", 640, 210)
m.msg("rm3", "$1 50", 640, 235); m.obj("rml", "line~", 640, 260)
m.chain("rm", "rm1", "rm2", "rm3", "rml")
m.obj("gL", "*~", 420, 290); m.obj("gR", "*~", 520, 290)
m.c("clone", 0, "gL", 0); m.c("clone", 1, "gR", 0); m.c("rml", 0, "gL", 1); m.c("rml", 0, "gR", 1)
m.obj("hL", "hip~ 5", 420, 320); m.obj("hR", "hip~ 5", 520, 320)
m.c("gL", 0, "hL", 0); m.c("gR", 0, "hR", 0)
m.obj("dac", "dac~", 420, 380); m.c("hL", 0, "dac", 0); m.c("hR", 0, "dac", 1)
m.obj("wr", "writesf~ 2", 520, 380); m.c("hL", 0, "wr", 0); m.c("hR", 0, "wr", 1)
m.obj("rrec", "r vast-rec", 620, 350); m.c("rrec", 0, "wr", 0)
m.text("record: send vast-rec 'open file.wav' then 'start' / 'stop'", 420, 420)
m.msg("recopen", "; vast-rec open vastish-take.wav; vast-rec start", 420, 450)
m.msg("recstop", "; vast-rec stop", 420, 500)
m.save("vastish.pd")

print("ok", len(FUNCS), "functions")
