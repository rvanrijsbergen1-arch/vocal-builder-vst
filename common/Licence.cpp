#include "Licence.h"
#include "monocypher-ed25519.h"
#include "monocypher.h"
#include <cstring>

namespace vb::licence
{
static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64UrlEncode (const unsigned char* d, size_t n)
{
    std::string out;
    size_t i = 0;
    for (; i + 2 < n; i += 3)
    {
        const unsigned v = (unsigned) d[i] << 16 | (unsigned) d[i + 1] << 8 | d[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if (n - i == 1)
    {
        const unsigned v = (unsigned) d[i] << 16;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
    }
    else if (n - i == 2)
    {
        const unsigned v = (unsigned) d[i] << 16 | (unsigned) d[i + 1] << 8;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
    }
    return out;
}

std::optional<std::vector<unsigned char>> base64UrlDecode (const std::string& s)
{
    int map[256];
    for (int& m : map)
        m = -1;
    for (int i = 0; i < 64; ++i)
        map[(unsigned char) kAlphabet[i]] = i;
    map[(unsigned char) '+'] = 62; // tolerate standard base64 too
    map[(unsigned char) '/'] = 63;
    std::vector<unsigned char> out;
    unsigned buf = 0;
    int bits = 0;
    for (char c : s)
    {
        if (c == '=')
            break;
        const int v = map[(unsigned char) c];
        if (v < 0)
            return std::nullopt;
        buf = buf << 6 | (unsigned) v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back ((unsigned char) ((buf >> bits) & 0xff));
        }
    }
    return out;
}

std::optional<std::string> verifyToken (const std::string& tokenIn, const std::string& prefix, const PublicKey& key)
{
    std::string token;
    for (char c : tokenIn) // ignore whitespace/line breaks from e-mail clients
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
            token += c;
    const auto p1 = token.find ('.');
    const auto p2 = token.find ('.', p1 == std::string::npos ? 0 : p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos || token.substr (0, p1) != prefix)
        return std::nullopt;
    const auto payload = base64UrlDecode (token.substr (p1 + 1, p2 - p1 - 1));
    const auto sig = base64UrlDecode (token.substr (p2 + 1));
    if (! payload || ! sig || sig->size() != 64 || payload->empty() || payload->size() > 4096)
        return std::nullopt;
    std::vector<unsigned char> msg (prefix.begin(), prefix.end());
    msg.push_back ('|');
    msg.insert (msg.end(), payload->begin(), payload->end());
    if (crypto_ed25519_check (sig->data(), key.data(), msg.data(), msg.size()) != 0)
        return std::nullopt;
    return std::string (payload->begin(), payload->end());
}

std::string shortDigest (const std::string& text)
{
    uint8_t h[64];
    crypto_sha512 (h, (const uint8_t*) text.data(), text.size());
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 16; ++i)
    {
        out += hex[h[i] >> 4];
        out += hex[h[i] & 15];
    }
    return out;
}
} // namespace vb::licence
