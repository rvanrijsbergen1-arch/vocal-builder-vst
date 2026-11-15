"""Writes test vectors (valid/invalid tokens) for tests/LicenceTests.cpp using the key in $VB_LICENSING_HOME."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import licence_admin as la
from cryptography.hazmat.primitives import serialization

k = la.load_key()
pub = k.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
print("pubkey", pub.hex())
t = la.sign("VB1", {"v": 1, "p": "vocal-builder", "lid": "VB-0001", "cus": "Jan Jansen", "dev": 2, "iat": "2026-11-15", "exp": None}, k)
print("valid VB1", t)
print("invalid VBA1", t)
pre, body, sig = t.split(".")
print("invalid VB1", f"{pre}.{body[:-2]}AA.{sig}")
print("invalid VB1", f"{pre}.{body}.{sig[:-3]}AAA")
print("invalid VB1", "VB1.garbage")
print("valid VBA1", la.sign("VBA1", {"v": 1, "p": "vocal-builder", "lid": "VB-0001", "mid": "abc", "iat": "2026-11-15"}, k))
print("invalid VB1", la.sign("VB1", {"v": 1}, la.Ed25519PrivateKey.generate()))
