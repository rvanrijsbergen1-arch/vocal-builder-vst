// Licence token cryptography (no JUCE dependency, so it can be unit-tested on its own).
//
// Tokens look like   <prefix>.<base64url(payload)>.<base64url(signature)>
// The signature is Ed25519 (RFC 8032) over the bytes  "<prefix>|" + payload.
// Only the PUBLIC key is compiled into the plugin. The private key never leaves the
// owner's licence tool.
#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace vb::licence
{
using PublicKey = std::array<unsigned char, 32>;

std::optional<std::vector<unsigned char>> base64UrlDecode (const std::string& s);
std::string base64UrlEncode (const unsigned char* data, size_t len);

// Returns the payload text if the token has the expected prefix and a valid signature.
std::optional<std::string> verifyToken (const std::string& token, const std::string& expectedPrefix, const PublicKey& key);

// Lower-case hex SHA-512 based digest (first 16 bytes), used for machine IDs.
std::string shortDigest (const std::string& text);
} // namespace vb::licence
