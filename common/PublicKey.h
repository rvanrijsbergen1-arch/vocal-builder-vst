// Public half of the licence signing key. Safe to publish; the private key stays with the owner.
//
// !!! This is a DEVELOPMENT key generated during the build-out. Before selling, run
//     `python tools/licence_admin.py init` on YOUR computer, paste the printed line below
//     and set kIsDevelopmentKey = false. The release CI job refuses to build otherwise.
#pragma once
#include "Licence.h"

static constexpr vb::licence::PublicKey kLicencePublicKey { { 0xc6, 0x5d, 0x40, 0x84, 0x83, 0xd6, 0x0a, 0x98, 0xe7, 0x01, 0xfc, 0x4f, 0x9e, 0x02, 0x09, 0x88, 0x75, 0x74, 0x99, 0x16, 0xa7, 0xb9, 0xd3, 0x6f, 0xb0, 0xaf, 0xe7, 0x5d, 0x82, 0xb7, 0x25, 0x39 } };
static constexpr bool kIsDevelopmentKey = true;
