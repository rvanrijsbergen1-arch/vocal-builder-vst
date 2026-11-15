#include "LicenceManager.h"
#include "Licence.h"
#ifndef VB_PUBLIC_KEY_HEADER
#define VB_PUBLIC_KEY_HEADER "PublicKey.h"
#endif
#include VB_PUBLIC_KEY_HEADER

namespace vb
{
static juce::String rawMachineId()
{
    // JUCE's device ID is stable across reboots and unaffected by storage or peripherals.
    const auto id = juce::SystemStats::getUniqueDeviceID();
    return juce::String (licence::shortDigest (("vocal-builder|" + id).toStdString()));
}

static juce::var parsePayload (const juce::String& token, const char* prefix)
{
    auto payload = licence::verifyToken (token.toStdString(), prefix, kLicencePublicKey);
    if (! payload)
        return {};
    auto v = juce::JSON::parse (juce::String::fromUTF8 (payload->c_str()));
    return v["p"].toString() == "vocal-builder" ? v : juce::var();
}

LicenceManager::LicenceManager() { load(); }

bool LicenceManager::usingDevelopmentKey() { return kIsDevelopmentKey; }

juce::File LicenceManager::storeFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("VocalBuilder").getChildFile ("licence.json");
}

void LicenceManager::load()
{
    auto v = juce::JSON::parse (storeFile().loadFileAsString());
    key = v["key"].toString();
    activation = v["activation"].toString();
    evaluate();
}

void LicenceManager::save() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("key", key);
    o->setProperty ("activation", activation);
    storeFile().getParentDirectory().createDirectory();
    storeFile().replaceWithText (juce::JSON::toString (juce::var (o)));
}

void LicenceManager::evaluate()
{
    customerName = lid = expires = {};
#if VB_PERSONAL_BUILD
    // Personal build: no activation needed. The signed-licence system stays in the code for later.
    customerName = "personal use";
    currentState = State::activated;
    return;
#endif
    if (key.isEmpty())
    {
        currentState = State::noKey;
        return;
    }
    auto k = parsePayload (key, "VB1");
    if (! k.isObject())
    {
        currentState = State::keyInvalid;
        return;
    }
    customerName = k["cus"].toString();
    lid = k["lid"].toString();
    maxDevices = (int) k["dev"];
    expires = k["exp"].isString() ? k["exp"].toString() : juce::String();
    if (expires.isNotEmpty() && juce::Time::getCurrentTime().formatted ("%Y-%m-%d") > expires)
    {
        currentState = State::expired;
        return;
    }
    if (activation.isEmpty())
    {
        currentState = State::needsActivation;
        return;
    }
    auto a = parsePayload (activation, "VBA1");
    if (! a.isObject() || a["lid"].toString() != lid || a["mid"].toString() != rawMachineId())
    {
        currentState = State::activationInvalid;
        return;
    }
    currentState = State::activated;
}

juce::String LicenceManager::enterKey (const juce::String& k)
{
    auto v = parsePayload (k.trim(), "VB1");
    if (! v.isObject())
        return "This licence key is not valid. Copy it exactly as you received it.";
    key = k.trim();
    activation = {};
    save();
    evaluate();
    return {};
}

juce::String LicenceManager::enterActivation (const juce::String& code)
{
    auto a = parsePayload (code.trim(), "VBA1");
    if (! a.isObject())
        return "This activation code is not valid.";
    if (a["lid"].toString() != lid)
        return "This activation code belongs to a different licence.";
    if (a["mid"].toString() != rawMachineId())
        return "This activation code was made for a different computer.";
    activation = code.trim();
    save();
    evaluate();
    return {};
}

juce::String LicenceManager::activationRequest() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("lid", lid);
    o->setProperty ("mid", rawMachineId());
    o->setProperty ("host", juce::SystemStats::getComputerName());
    const auto json = juce::JSON::toString (juce::var (o), true).toStdString();
    return "VBR1." + juce::String (licence::base64UrlEncode ((const unsigned char*) json.data(), json.size()));
}

juce::String LicenceManager::machineId() const
{
    auto id = rawMachineId().substring (0, 16).toUpperCase();
    return id.substring (0, 4) + "-" + id.substring (4, 8) + "-" + id.substring (8, 12) + "-" + id.substring (12, 16);
}

void LicenceManager::removeLicence()
{
    key = activation = {};
    save();
    evaluate();
}

juce::String LicenceManager::statusText() const
{
    switch (currentState)
    {
        case State::noKey: return "Not activated";
        case State::keyInvalid: return "Licence key invalid";
        case State::needsActivation: return "Licence " + lid + " — activate this computer";
        case State::activationInvalid: return "Activation not valid on this computer";
        case State::expired: return "Licence expired (" + expires + ")";
        case State::activated: return "Licensed to " + customerName;
    }
    return {};
}
} // namespace vb
