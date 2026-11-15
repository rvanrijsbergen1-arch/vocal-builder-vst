"""Writes a PublicKey.h for the key in $VB_LICENSING_HOME (CI test builds only)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import licence_admin as la
from cryptography.hazmat.primitives import serialization
pub = la.load_key().public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
arr = ", ".join(f"0x{b:02x}" for b in pub)
Path(sys.argv[1]).write_text('#pragma once\n#include "Licence.h"\n'
    f"static constexpr vb::licence::PublicKey kLicencePublicKey {{ {{ {arr} }} }};\n"
    "static constexpr bool kIsDevelopmentKey = true;\n")
