"""Rebuild the byte-bus I/O patch after `make -C vcv/bytebus/tools`."""
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def patch():
    backend=ROOT/'tools'/('compile_vastish_backend.exe' if os.name=='nt' else 'compile_vastish_backend')
    metadata=json.loads(subprocess.check_output([str(backend),'--describe'],text=True))
    def vast_params(values):
        params=metadata['defaults'][:]
        for name,value in values.items():
            params[metadata['names'].index(name)]=value
        return params
    def module(ident,plugin,model,version,pos,params,data=None):
        result=dict(id=ident,plugin=plugin,model=model,version=version,pos=pos,
                    params=[dict(id=i,value=float(v)) for i,v in enumerate(params)])
        if data is not None: result['data']=data
        return result
    otis=json.loads((ROOT.parent/'otislab/examples/OTIS-byte-frames.vcv').read_text())['modules'][2]
    source=vast_params({'pitch-adj':-20,'pitch-kt':0})
    processor=vast_params({'pitch-wave':6,'pitch-kt':0,'f1-type':7,'f1-adj':2})
    modules=[
        module(1,'VastishLab','Vastish','2.5.0',[0,0],source,{'program':0}),
        module(2,'ByteBusLab','Converter','2.2.0',[56,0],[1,0,0,1,0,0,0,0]),
        module(3,'ByteBusLab','ClockedRegister','2.2.0',[70,0],[3,2,128,0],{'word':128}),
        module(4,'VastishLab','Vastish','2.5.0',[0,1],processor,{'program':0}),
        module(5,'ByteBusLab','Lookup','2.2.0',[82,0],[4,11,0,17,0,0,0]),
        {**otis,'id':6,'pos':[56,1]},
        module(7,'Core','AudioInterface2','2.6.4',[92,1],[]),
    ]
    connections=[(1,11,2,2),(2,2,2,0),(2,0,3,0),(3,0,4,8),(4,11,5,0),(5,0,6,16),(6,0,7,0),(6,1,7,1)]
    cables=[dict(id=i+1,outputModuleId=src,outputId=out,inputModuleId=dest,inputId=port,color='#80ddc7')
            for i,(src,out,dest,port) in enumerate(connections)]
    return dict(version='2.6.4',modules=modules,cables=cables)

if __name__=='__main__':
    (ROOT/'examples/byte-bus-io.vcv').write_text(json.dumps(patch(),indent=2)+'\n')
    print('Created original byte-bus I/O patch')
