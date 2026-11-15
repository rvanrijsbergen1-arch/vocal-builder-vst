// Line protocol between the plugin and the VocalEngine helper process.
// The engine writes one message per line on stdout:
//   @progress <0..1|-1> <message>
//   @result <json>
//   @error <CODE> <message>
// Anything else on stdout/stderr is diagnostic text.
#pragma once
#include <cstdio>
#include <mutex>
#include <string>

namespace vb
{
inline std::mutex& protocolMutex()
{
    static std::mutex m;
    return m;
}

inline std::string oneLine (std::string s)
{
    for (auto& c : s)
        if (c == '\n' || c == '\r')
            c = ' ';
    return s;
}

inline void sendProgress (double fraction, const std::string& message)
{
    std::lock_guard<std::mutex> l (protocolMutex());
    std::printf ("@progress %.4f %s\n", fraction, oneLine (message).c_str());
    std::fflush (stdout);
}

inline void sendResult (const std::string& json)
{
    std::lock_guard<std::mutex> l (protocolMutex());
    std::printf ("@result %s\n", oneLine (json).c_str());
    std::fflush (stdout);
}

inline void sendError (const std::string& code, const std::string& message)
{
    std::lock_guard<std::mutex> l (protocolMutex());
    std::printf ("@error %s %s\n", code.c_str(), oneLine (message).c_str());
    std::fflush (stdout);
}

// Thrown inside the engine; converted to an @error line in main().
struct EngineError
{
    std::string code, message;
};
} // namespace vb
