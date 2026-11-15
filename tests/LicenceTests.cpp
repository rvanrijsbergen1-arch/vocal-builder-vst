// Cross-checks the plugin's Ed25519 token verification against tokens signed by
// tools/licence_admin.py.  Usage: vb_licence_tests <vectors.txt>
// vectors.txt lines:  pubkey <64 hex chars> | valid <prefix> <token> | invalid <prefix> <token>
#include "Licence.h"
#include <cstdio>
#include <fstream>
#include <sstream>

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;
    std::ifstream f (argv[1]);
    vb::licence::PublicKey key {};
    std::string line;
    int pass = 0, fail = 0;
    while (std::getline (f, line))
    {
        std::istringstream ss (line);
        std::string kind, a, b;
        ss >> kind >> a >> b;
        if (kind == "pubkey")
        {
            for (size_t i = 0; i < 32; ++i)
                key[i] = (unsigned char) std::stoi (a.substr (i * 2, 2), nullptr, 16);
            continue;
        }
        const bool ok = vb::licence::verifyToken (b, a, key).has_value();
        const bool expect = kind == "valid";
        if (ok == expect)
            ++pass;
        else
        {
            ++fail;
            std::printf ("FAIL: %s %s\n", kind.c_str(), b.substr (0, 40).c_str());
        }
    }
    // base64url round trip
    const unsigned char d[] = { 0, 1, 2, 250, 251, 252, 253 };
    for (size_t n = 0; n <= sizeof (d); ++n)
    {
        auto enc = vb::licence::base64UrlEncode (d, n);
        auto dec = vb::licence::base64UrlDecode (enc);
        if (dec && dec->size() == n && std::equal (d, d + n, dec->begin()))
            ++pass;
        else
            ++fail;
    }
    std::printf ("licence tests: %d passed, %d failed\n", pass, fail);
    return fail == 0 && pass > 0 ? 0 : 1;
}
