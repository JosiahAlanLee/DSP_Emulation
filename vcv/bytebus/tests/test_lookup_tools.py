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
    def test_sine_recipe_and_frozen_provenance(self):
        table = self.run_compile(self.write("sine.json",self.recipe))
        self.assertEqual([table["values"][n] for n in (0,64,128,192)],[128,0,128,255])
        self.assertEqual(table["source"]["kind"],"Vastish.memoryless")
        self.assertEqual(len(table["source"]["params"]),80)
        self.assertEqual(len(table["source"]["frozen"]["sources"]),18)
    def test_stateful_rejection_preserves_output(self):
        bad = copy.deepcopy(self.recipe)
        next(x for x in bad["params"] if x["id"]==46)["value"] = 1
        (self.work/"out.json").write_text("keep")
        result = self.run_compile(self.write("filter.json",bad),False)
        self.assertIn("stateful",result.stderr)
        self.assertEqual((self.work/"out.json").read_text(),"keep")
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

if __name__ == "__main__":
    unittest.main()
