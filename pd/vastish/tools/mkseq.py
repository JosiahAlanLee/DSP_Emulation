import sys
out=[]
def ev(dt, line): out.append(f"{dt} {line};")
progs = {
 1: [(36,500),(36,250),(43,250),(48,500),(46,250),(36,750)],
 2: [(72,1500),(79,1500),(67,2500)],
 3: "chord",
 4: [(72,400),(75,400),(77,400),(79,800),(82,400),(79,1200)],
 5: [(48,300),(55,300),(60,300),(64,300),(67,300),(72,300),(76,1500)],
}
for p, seq in progs.items():
    ev(0, f"vast-prog {p}"); ev(0, "vast-mw 0")
    ev(50, f"vast-rec open {sys.argv[1]}/prog{p}.wav"); ev(0, "vast-rec start")
    if seq == "chord":
        for n in (60,64,67,71): ev(0, f"vast-note {n} 90")
        for k in range(1,21): ev(200, f"vast-mw {k/20}")
        for n in (60,64,67,71): ev(0, f"vast-note {n} 0")
        ev(2500, "vast-rec stop")
    else:
        for n,d in seq:
            ev(0, f"vast-note {n} 100"); ev(int(d*0.8), f"vast-note {n} 0"); ev(int(d*0.2), "dummy 0")
        if p == 4:
            for k in range(1,11): ev(100, f"vast-mw {k/10}")
            ev(0, "vast-note 79 100"); ev(1500,"vast-note 79 0")
        ev(2500, "vast-rec stop")
ev(100, "pd quit")
open(sys.argv[2],"w").write("\n".join(out)+"\n")
