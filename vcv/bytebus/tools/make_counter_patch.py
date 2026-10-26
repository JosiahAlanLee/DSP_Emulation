"""Rebuild examples/counter-divider.vcv: Counter as a divider chain and as a rhythm address."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def patch():
    version=json.loads((ROOT/'plugin.json').read_text())['version']
    def module(ident,plugin,model,version,pos,params):
        return dict(id=ident,plugin=plugin,model=model,version=version,pos=pos,
                    params=[dict(id=i,value=float(v)) for i,v in enumerate(params)])
    modules=[
        # Counter: Auto (internal 8 Hz), INC 1, MOD 256, SEED 0.
        module(1,'ByteBusLab','Counter',version,[0,0],[0,3,1,256,0,0]),
        # Split 1: outputs 1-8 are squares at RATE/2 ... RATE/256.
        module(2,'Fundamental','Split','2.6.4',[12,0],[]),
        # Lookup: Rhythm family, pattern seed 0; its CLK follows the Counter's.
        module(3,'ByteBusLab','Lookup',version,[16,0],[5,0,0,0,0,0,0]),
        # Split 2: eight trigger lanes stepping through the rhythm.
        module(4,'Fundamental','Split','2.6.4',[32,0],[]),
    ]
    connections=[(1,0,2,0),(1,0,3,0),(3,0,4,0)]
    cables=[dict(id=i+1,outputModuleId=src,outputId=out,inputModuleId=dest,inputId=port,color='#80ddc7')
            for i,(src,out,dest,port) in enumerate(connections)]
    return dict(version='2.6.4',modules=modules,cables=cables)

if __name__=='__main__':
    (ROOT/'examples/counter-divider.vcv').write_text(json.dumps(patch(),indent=2)+'\n')
    print('Created Counter divider/rhythm patch')
