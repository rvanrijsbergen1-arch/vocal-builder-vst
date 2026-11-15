#!/usr/bin/env python3
"""Vocal Builder — owner licence tool (keep this and the private key OFF customer machines).

Offline model (no server needed):
  1. `issue` creates a signed licence key for a customer (you e-mail it to them).
  2. The customer pastes the key in the plugin. The plugin shows an *activation request*
     containing a machine ID. They send it to you.
  3. `activate` checks the device allowance, records the device and prints a signed
     *activation code* bound to that machine. The customer pastes it in the plugin.
  4. `deactivate` frees a device slot (transfer to a new computer); `replace` re-issues a key.

The plugin only contains the PUBLIC key. Revocation is recorded here, but an offline
licence that is already activated keeps working on that machine: offline licensing
cannot revoke remotely. An optional activation server can automate steps 2-3 later.

Usage:
  python licence_admin.py init                       # once: creates the signing key pair
  python licence_admin.py issue --customer "Jan Jansen" --email jan@example.com --devices 2
  python licence_admin.py list
  python licence_admin.py show VB-0001
  python licence_admin.py activate "VBR1.xxxxx"      # paste the customer's activation request
  python licence_admin.py deactivate VB-0001 <machine-id>
  python licence_admin.py revoke VB-0001 --reason "refund"
  python licence_admin.py replace VB-0001            # new key, old one revoked, devices kept
  python licence_admin.py export licences.csv
Data lives in $VB_LICENSING_HOME (default ~/VocalBuilderLicensing). Back it up.
"""
from __future__ import annotations

import argparse
import base64
import csv
import datetime as dt
import json
import os
import sqlite3
import sys
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

HOME = Path(os.environ.get("VB_LICENSING_HOME", Path.home() / "VocalBuilderLicensing"))
KEY_FILE = HOME / "private_key.pem"
DB_FILE = HOME / "licences.db"
PRODUCT = "vocal-builder"
KEY_PREFIX, ACT_PREFIX, REQ_PREFIX = "VB1", "VBA1", "VBR1"


def b64u(b: bytes) -> str:
    return base64.urlsafe_b64encode(b).decode().rstrip("=")


def b64u_dec(s: str) -> bytes:
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def today() -> str:
    return dt.date.today().isoformat()


def load_key() -> Ed25519PrivateKey:
    if not KEY_FILE.exists():
        sys.exit(f"No signing key at {KEY_FILE}. Run `init` first.")
    return serialization.load_pem_private_key(KEY_FILE.read_bytes(), password=None)


def sign(prefix: str, payload: dict, key: Ed25519PrivateKey) -> str:
    body = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode()
    sig = key.sign(prefix.encode() + b"|" + body)
    return f"{prefix}.{b64u(body)}.{b64u(sig)}"


def db() -> sqlite3.Connection:
    HOME.mkdir(parents=True, exist_ok=True)
    c = sqlite3.connect(DB_FILE)
    c.row_factory = sqlite3.Row
    c.executescript("""
      CREATE TABLE IF NOT EXISTS licences(
        id TEXT PRIMARY KEY, customer TEXT NOT NULL, email TEXT, edition TEXT, max_devices INTEGER,
        issued TEXT, expires TEXT, status TEXT DEFAULT 'active', notes TEXT, key TEXT, replaced_by TEXT);
      CREATE TABLE IF NOT EXISTS devices(
        licence_id TEXT, machine_id TEXT, label TEXT, activated TEXT, deactivated TEXT,
        PRIMARY KEY(licence_id, machine_id));
      CREATE TABLE IF NOT EXISTS events(at TEXT, licence_id TEXT, what TEXT);
    """)
    return c


def log(c, lid, what):
    c.execute("INSERT INTO events VALUES(?,?,?)", (dt.datetime.now().isoformat(timespec="seconds"), lid, what))


def next_id(c) -> str:
    n = c.execute("SELECT COUNT(*) FROM licences").fetchone()[0] + 1
    return f"VB-{n:04d}"


def cmd_init(a):
    HOME.mkdir(parents=True, exist_ok=True)
    if KEY_FILE.exists() and not a.force:
        sys.exit(f"{KEY_FILE} already exists. Re-creating it would invalidate every issued licence.")
    k = Ed25519PrivateKey.generate()
    KEY_FILE.write_bytes(k.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                         serialization.NoEncryption()))
    os.chmod(KEY_FILE, 0o600)
    print_public(k)


def print_public(k):
    pub = k.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    arr = ", ".join(f"0x{b:02x}" for b in pub)
    print("Put this in plugin/PublicKey.h (it is safe to publish):\n")
    print(f"static constexpr vb::licence::PublicKey kLicencePublicKey {{ {{ {arr} }} }};")


def cmd_pubkey(a):
    print_public(load_key())


def cmd_issue(a):
    key, c = load_key(), db()
    lid = next_id(c)
    payload = {"v": 1, "p": PRODUCT, "lid": lid, "cus": a.customer, "edn": a.edition, "dev": a.devices,
               "iat": today(), "exp": a.expires}
    token = sign(KEY_PREFIX, payload, key)
    c.execute("INSERT INTO licences(id,customer,email,edition,max_devices,issued,expires,key,notes) VALUES(?,?,?,?,?,?,?,?,?)",
              (lid, a.customer, a.email, a.edition, a.devices, payload["iat"], a.expires, token, a.notes))
    log(c, lid, f"issued ({a.devices} devices)")
    c.commit()
    print(f"Licence {lid} for {a.customer}\n\n{token}\n")


def cmd_list(a):
    c = db()
    for r in c.execute("SELECT * FROM licences ORDER BY id"):
        used = c.execute("SELECT COUNT(*) FROM devices WHERE licence_id=? AND deactivated IS NULL", (r["id"],)).fetchone()[0]
        print(f"{r['id']}  {r['status']:<9} {used}/{r['max_devices']} devices  {r['customer']} <{r['email'] or ''}>  issued {r['issued']}")


def cmd_show(a):
    c = db()
    r = c.execute("SELECT * FROM licences WHERE id=?", (a.lid,)).fetchone()
    if not r:
        sys.exit("Unknown licence")
    print(json.dumps(dict(r), indent=2))
    for d in c.execute("SELECT * FROM devices WHERE licence_id=?", (a.lid,)):
        print("  device", dict(d))
    for e in c.execute("SELECT * FROM events WHERE licence_id=? ORDER BY at", (a.lid,)):
        print("  ", e["at"], e["what"])


def parse_request(req: str) -> dict:
    req = "".join(req.split())
    parts = req.split(".")
    if len(parts) != 2 or parts[0] != REQ_PREFIX:
        sys.exit("That is not a Vocal Builder activation request (it should start with VBR1.)")
    return json.loads(b64u_dec(parts[1]))


def cmd_activate(a):
    key, c = load_key(), db()
    req = parse_request(a.request)
    lid, mid = req["lid"], req["mid"]
    r = c.execute("SELECT * FROM licences WHERE id=?", (lid,)).fetchone()
    if not r:
        sys.exit(f"Unknown licence {lid}")
    if r["status"] != "active":
        sys.exit(f"Licence {lid} is {r['status']}; not activating.")
    existing = c.execute("SELECT * FROM devices WHERE licence_id=? AND machine_id=?", (lid, mid)).fetchone()
    used = c.execute("SELECT COUNT(*) FROM devices WHERE licence_id=? AND deactivated IS NULL", (lid,)).fetchone()[0]
    if not (existing and existing["deactivated"] is None) and used >= r["max_devices"]:
        sys.exit(f"Licence {lid} already uses {used}/{r['max_devices']} devices. Deactivate one first (transfer).")
    c.execute("INSERT OR REPLACE INTO devices VALUES(?,?,?,?,NULL)", (lid, mid, req.get("host", ""), today()))
    log(c, lid, f"activated {mid} ({req.get('host', '')})")
    c.commit()
    print(sign(ACT_PREFIX, {"v": 1, "p": PRODUCT, "lid": lid, "mid": mid, "iat": today()}, key))


def cmd_deactivate(a):
    c = db()
    n = c.execute("UPDATE devices SET deactivated=? WHERE licence_id=? AND machine_id=? AND deactivated IS NULL",
                  (today(), a.lid, a.machine_id)).rowcount
    log(c, a.lid, f"deactivated {a.machine_id}")
    c.commit()
    print("Device slot freed." if n else "No active device with that ID.")
    print("Note: offline activations cannot be switched off remotely; ask the customer to remove the old installation.")


def cmd_revoke(a):
    c = db()
    c.execute("UPDATE licences SET status='revoked', notes=COALESCE(notes,'')||? WHERE id=?", (f" revoked: {a.reason}", a.lid))
    log(c, a.lid, f"revoked: {a.reason}")
    c.commit()
    print("Marked revoked. No new activations will be issued. Existing offline activations keep working.")


def cmd_replace(a):
    key, c = load_key(), db()
    r = c.execute("SELECT * FROM licences WHERE id=?", (a.lid,)).fetchone()
    if not r:
        sys.exit("Unknown licence")
    nid = next_id(c)
    payload = {"v": 1, "p": PRODUCT, "lid": nid, "cus": r["customer"], "edn": r["edition"], "dev": r["max_devices"],
               "iat": today(), "exp": r["expires"]}
    token = sign(KEY_PREFIX, payload, key)
    c.execute("INSERT INTO licences(id,customer,email,edition,max_devices,issued,expires,key,notes) VALUES(?,?,?,?,?,?,?,?,?)",
              (nid, r["customer"], r["email"], r["edition"], r["max_devices"], payload["iat"], r["expires"], token, f"replaces {a.lid}"))
    c.execute("UPDATE licences SET status='replaced', replaced_by=? WHERE id=?", (nid, a.lid))
    log(c, a.lid, f"replaced by {nid}")
    log(c, nid, f"issued as replacement for {a.lid}")
    c.commit()
    print(f"New licence {nid}:\n\n{token}\n")


def cmd_export(a):
    c = db()
    with open(a.file, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["id", "customer", "email", "edition", "max_devices", "active_devices", "issued", "expires", "status"])
        for r in c.execute("SELECT * FROM licences ORDER BY id"):
            used = c.execute("SELECT COUNT(*) FROM devices WHERE licence_id=? AND deactivated IS NULL", (r["id"],)).fetchone()[0]
            w.writerow([r["id"], r["customer"], r["email"], r["edition"], r["max_devices"], used, r["issued"], r["expires"], r["status"]])
    print(f"Wrote {a.file}")


def main(argv=None):
    p = argparse.ArgumentParser(description="Vocal Builder licence administration")
    s = p.add_subparsers(dest="cmd", required=True)
    x = s.add_parser("init"); x.add_argument("--force", action="store_true"); x.set_defaults(f=cmd_init)
    s.add_parser("pubkey").set_defaults(f=cmd_pubkey)
    x = s.add_parser("issue"); x.add_argument("--customer", required=True); x.add_argument("--email")
    x.add_argument("--devices", type=int, default=2); x.add_argument("--edition", default="standard")
    x.add_argument("--expires", default=None); x.add_argument("--notes", default=None); x.set_defaults(f=cmd_issue)
    s.add_parser("list").set_defaults(f=cmd_list)
    x = s.add_parser("show"); x.add_argument("lid"); x.set_defaults(f=cmd_show)
    x = s.add_parser("activate"); x.add_argument("request"); x.set_defaults(f=cmd_activate)
    x = s.add_parser("deactivate"); x.add_argument("lid"); x.add_argument("machine_id"); x.set_defaults(f=cmd_deactivate)
    x = s.add_parser("revoke"); x.add_argument("lid"); x.add_argument("--reason", default=""); x.set_defaults(f=cmd_revoke)
    x = s.add_parser("replace"); x.add_argument("lid"); x.set_defaults(f=cmd_replace)
    x = s.add_parser("export"); x.add_argument("file"); x.set_defaults(f=cmd_export)
    a = p.parse_args(argv)
    a.f(a)


if __name__ == "__main__":
    main()
