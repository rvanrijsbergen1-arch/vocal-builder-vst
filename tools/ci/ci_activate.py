"""CI only: issue a licence with the CI key in $VB_LICENSING_HOME and activate this machine.
Usage: python ci_activate.py <VocalEngine.exe>"""
import json, os, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import licence_admin as la

engine = sys.argv[1]
store = Path(os.environ["APPDATA"] if os.name == "nt" else Path.home() / ".config") / "VocalBuilder"
store.mkdir(parents=True, exist_ok=True)
k = la.load_key()
key = la.sign("VB1", {"v": 1, "p": "vocal-builder", "lid": "CI-1", "cus": "CI runner", "edn": "ci", "dev": 1, "iat": la.today(), "exp": None}, k)
(store / "licence.json").write_text(json.dumps({"key": key, "activation": ""}))
out = subprocess.run([engine, "licence-status"], capture_output=True, text=True).stdout
req = json.loads(out.split("@result ", 1)[1])["request"]
mid = json.loads(la.b64u_dec(req.split(".")[1]))["mid"]
act = la.sign("VBA1", {"v": 1, "p": "vocal-builder", "lid": "CI-1", "mid": mid, "iat": la.today()}, k)
(store / "licence.json").write_text(json.dumps({"key": key, "activation": act}))
print(subprocess.run([engine, "licence-status"], capture_output=True, text=True).stdout)
