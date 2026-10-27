"""Exercise generation and the real compiler CLI, including Rack archives."""
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT/"tools/compile_vastish.py"
import sys
sys.path.insert(0,str(ROOT/"tools"))
from compile_vastish import fnv

class Tools(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.recipe = json.loads((ROOT/"examples/vastish-sine.recipe.json").read_text())
    def run_compile(self,source,success=True,**kwargs):
        dest = self.work/"out.json"
        result = subprocess.run([sys.executable,str(SCRIPT),str(source),"--output",str(dest),*kwargs.get("extra",[])],capture_output=True,text=True)
        if success:
            self.assertEqual(result.returncode,0,result.stderr)
            table = json.loads(dest.read_text())
            raw = bytes(table["values"])
            self.assertEqual(len(raw),256)
            self.assertEqual(table["hash"],fnv(raw))
            self.assertEqual(table["sha256"],hashlib.sha256(raw).hexdigest())
            return table
        self.assertNotEqual(result.returncode,0)
        return result
    def write(self,name,value):
        path = self.work/name
        path.write_text(json.dumps(value))
        return path
    def test_generation_and_manifest(self):
        subprocess.run([sys.executable,str(ROOT/"tools/generate_lookup.py"),"--check"],check=True,capture_output=True)
        manifest = json.loads((ROOT/"res/lookup-v1.json").read_text())
        raw = (ROOT/"res/lookup-v1.bin").read_bytes()
        self.assertEqual(manifest["sha256"],hashlib.sha256(raw).hexdigest())
        self.assertEqual(manifest["sha256"],"f8339c89bd305abaa06a9773721301aa2e1d025b07adc355eddbaeadf03417ee")
        self.assertEqual(len(raw),12+6*64*256)
        for index,record in enumerate(manifest["tables"]):
            table = raw[12+index*256:12+(index+1)*256]
            self.assertEqual(record["sha256"],hashlib.sha256(table).hexdigest())
        self.assertEqual(len(manifest["tables"]),384)
    def test_library_v2(self):
        # v2 is its own identity; v1 above stays byte-identical.
        manifest = json.loads((ROOT/"res/lookup-v2.json").read_text())
        raw = (ROOT/"res/lookup-v2.bin").read_bytes()
        self.assertEqual(manifest["sha256"],hashlib.sha256(raw).hexdigest())
        self.assertEqual(manifest["sha256"],"6dca9a367a321379f3ceea33ab27c96a2c02b3d8b109c4bd6d7f89a8a9873b04")
        self.assertEqual(raw[:12],b"BBLOOK02"+bytes([3,64,0,2]))
        self.assertEqual(len(raw),12+3*64*256)
        self.assertEqual(manifest["families"],["Envelope","Sequence","Density"])
        tables = {}
        for index,record in enumerate(manifest["tables"]):
            table = raw[12+index*256:12+(index+1)*256]
            self.assertEqual(record["sha256"],hashlib.sha256(table).hexdigest())
            tables[record["family"],record["shape"]] = table
        self.assertEqual(len(tables),192)
        # Envelope: the decays (groups 0-2, 4-6) end at 00; the rises (group 3) end at FF;
        # the sustains (group 7) end at their sustain level, rising with the shape.
        for shape in range(64):
            env, group = tables["Envelope",shape], shape//8
            if group in (0,1,2,4,5,6): self.assertEqual(env[255],0,shape)
            if group == 3: self.assertEqual((env[0],env[255]),(0,255),shape)
            if group in (0,1,4,5): self.assertEqual(env[0],255,shape)
        sustains = [tables["Envelope",56+k][255] for k in range(8)]
        self.assertEqual(sustains,sorted(sustains)); self.assertEqual(sustains[0],0)
        # Sequence: piecewise constant; staircases have their step count of levels.
        for shape in range(64):
            runs = 1+sum(a!=b for a,b in zip(tables["Sequence",shape],tables["Sequence",shape][1:]))
            self.assertLessEqual(runs,32,shape)
        for k,count in enumerate([2,3,4,5,6,8,12,16]):
            self.assertEqual(len(set(tables["Sequence",k])),count)
            self.assertEqual(list(tables["Sequence",k]),sorted(tables["Sequence",k]))
            self.assertEqual(list(tables["Sequence",15-k]),sorted(tables["Sequence",15-k],reverse=True))
        # Density: Rhythm's layout (hits high for four of eight addresses), and each shape is the
        # previous one plus exactly one hit, so SHAPE is a fill control.
        previous = None
        for shape in range(64):
            table = tables["Density",shape]
            for n in range(256):
                if n%8 >= 4: self.assertEqual(table[n],0)
                else: self.assertEqual(table[n],table[n-n%8])
            hits = {(lane,n//8) for n in range(0,256,8) for lane in range(8) if table[n]>>lane&1}
            self.assertEqual(len(hits),4+shape)
            self.assertEqual(manifest["tables"][128+shape]["hits"],4+shape)
            if previous is not None:
                self.assertTrue(previous < hits and len(hits-previous) == 1, shape)
            previous = hits
        self.assertTrue({(0,0),(0,16),(1,8),(1,24)} <= previous)
    def test_library_v3_ratio(self):
        # v3 (Ratio): its own identity; every byte a ratio word from its grid, ascending through the
        # addresses in equal zones, centred on 1/1, over the shape's span; v1 and v2 unchanged.
        from fractions import Fraction
        import generate_lookup as g
        manifest = json.loads((ROOT/"res/lookup-v3.json").read_text())
        raw = (ROOT/"res/lookup-v3.bin").read_bytes()
        self.assertEqual(manifest["sha256"],hashlib.sha256(raw).hexdigest())
        self.assertEqual(manifest["sha256"],"3d269a068061e9a86b0eaaf4c9b4a854a863a3680b39a1814790848e158ab2fd")
        self.assertEqual(raw[:12],b"BBLOOK03"+bytes([1,64,0,3]))
        self.assertEqual(len(raw),12+64*256)
        grids = g.ratio_grids()
        self.assertEqual(list(grids),["Integers","Odds","Threes","Harmonics","Straight","Triplet","Dotted","All"])
        self.assertEqual([str(r) for r in grids["Harmonics"]],["1/4","1/3","1/2","2/3","3/4","1","4/3","3/2","2","3","4"])
        self.assertEqual([str(r) for r in grids["Straight"]],["1/16","1/8","1/4","1/2","1","2","4","8","16"])
        self.assertTrue(set(grids["Triplet"]) >= {Fraction(3,2),Fraction(3,8),Fraction(12)})
        self.assertTrue(set(grids["Dotted"]) >= {Fraction(2,3),Fraction(1,12),Fraction(16,3)})
        self.assertEqual(set(grids["All"]),set().union(*(set(v) for k,v in grids.items() if k!="All")))
        word = lambda w: Fraction((w>>4)+1,(w&15)+1)
        for shape in range(64):
            grid = grids[list(grids)[shape//8]]; span = g.RATIO_SPANS[shape%8]
            table = raw[12+shape*256:12+(shape+1)*256]
            self.assertEqual(manifest["tables"][shape]["sha256"],hashlib.sha256(table).hexdigest())
            values = [word(b) for b in table]
            self.assertTrue(all(v in grid for v in values),shape)
            self.assertEqual(values,sorted(values))
            self.assertIn(Fraction(1),values)
            distinct = list(dict.fromkeys(values))
            centre = grid.index(Fraction(1))
            want = grid if span is None else grid[max(0,centre-span):centre+span+1]
            self.assertEqual(distinct,want)
            zones = [values.count(v) for v in distinct]
            self.assertLessEqual(max(zones)-min(zones),1)                 # equal zones, to a byte
    def test_sine_recipe_and_frozen_provenance(self):
        table = self.run_compile(self.write("sine.json",self.recipe))
        self.assertEqual([table["values"][n] for n in (0,64,128,192)],[128,0,128,255])
        self.assertEqual(table["source"]["kind"],"Vastish.memoryless")
        self.assertEqual(len(table["source"]["params"]),86)
        self.assertEqual(len(table["source"]["frozen"]["sources"]),23)
        self.assertEqual(table["source"]["frozen"]["sources"][22],1)
    def test_stateful_rejection_preserves_output(self):
        bad = copy.deepcopy(self.recipe)
        next(x for x in bad["params"] if x["id"]==46)["value"] = 1
        (self.work/"out.json").write_text("keep")
        result = self.run_compile(self.write("filter.json",bad),False)
        self.assertIn("stateful",result.stderr)
        self.assertEqual((self.work/"out.json").read_text(),"keep")
    def test_legacy_arrays_and_normalized_fun(self):
        describe=json.loads(subprocess.check_output([str(ROOT/"tools/compile_vastish_backend"),"--describe"],text=True))
        params=describe["defaults"][:80]
        for entry in self.recipe["params"]:
            params[entry["id"]]=entry["value"]
        legacy={**self.recipe,"params":params,"frozen":{"sources":[0]*18}}
        expected=self.run_compile(self.write("legacy.json",legacy))["values"]
        names=describe["names"]; current=describe["defaults"][:]
        current[:80]=params
        for name,value in {"fun1-a":22,"fun1-b":22,"fun1-eq":0,"pitch-s1":5,"pitch-d1":6}.items():
            current[names.index(name)]=value
        recipe={**legacy,"params":current}
        numeric=self.run_compile(self.write("numeric.json",recipe))["values"]
        self.assertEqual(numeric,expected)  # (ONE+ONE)*6 is a full-cycle rotation.
        current[names.index("fun1-bit")]=1
        normalized=self.run_compile(self.write("bits.json",recipe))["values"]
        self.assertEqual(normalized,expected[128:]+expected[:128])
    def test_validation(self):
        duplicate = copy.deepcopy(self.recipe); duplicate["params"].append({"id":0,"value":1})
        self.assertIn("duplicate",self.run_compile(self.write("duplicate.json",duplicate),False).stderr)
        bad = copy.deepcopy(self.recipe); bad["frozen"]["velocity"] = 2
        self.assertIn("range",self.run_compile(self.write("velocity.json",bad),False).stderr)
        self.assertIn("Expected",self.run_compile(self.write("other.json",{}),False).stderr)
        missing = self.work/"missing"
        self.assertIn("backend",self.run_compile(self.write("ok.json",self.recipe),False,extra=["--backend",str(missing)]).stderr)
    def test_patch_selection_zip_and_zstd_tar(self):
        module = {"plugin":"VastishLab","model":"Vastish","id":17,"params":self.recipe["params"]}
        patch = {"modules":[module,{**module,"id":18}]}
        source = self.write("patch.vcv",patch)
        self.assertIn("Select exactly one",self.run_compile(source,False).stderr)
        expected = self.run_compile(source,extra=["--module-id","17"])["values"]
        archive = self.work/"patch.zip"
        with zipfile.ZipFile(archive,"w") as z:
            z.writestr("patch.json",json.dumps(patch))
        self.assertEqual(self.run_compile(archive,extra=["--module-id","17"])["values"],expected)
        if shutil.which("zstd"):
            stream = io.BytesIO()
            payload = json.dumps(patch).encode()
            with tarfile.open(fileobj=stream,mode="w") as tar:
                info = tarfile.TarInfo("patch.json"); info.size = len(payload)
                tar.addfile(info,io.BytesIO(payload))
            archive = self.work/"patch-zstd.vcv"
            archive.write_bytes(subprocess.check_output(["zstd","-q","-c"],input=stream.getvalue()))
            self.assertEqual(self.run_compile(archive,extra=["--module-id","17"])["values"],expected)
    def test_counter_example_matches_generator(self):
        import make_counter_patch
        saved = json.loads((ROOT/"examples/counter-divider.vcv").read_text())
        self.assertEqual(saved, make_counter_patch.patch())
        models = {m["model"] for m in saved["modules"] if m["plugin"] == "ByteBusLab"}
        self.assertEqual(models, {"Counter", "Lookup"})

if __name__ == "__main__":
    unittest.main()
