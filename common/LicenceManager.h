// Offline licence check: a signed licence key plus a signed, machine-bound activation code.
// Both are verified with the embedded public key every time the plugin loads.
// There is no "activated = true" flag to edit: without a valid signature nothing unlocks.
#pragma once
#include <juce_core/juce_core.h>

namespace vb
{
class LicenceManager
{
public:
    enum class State { noKey, keyInvalid, needsActivation, activationInvalid, activated, expired };

    LicenceManager();

    State state() const { return currentState; }
    bool canProcess() const { return currentState == State::activated; }
    juce::String customer() const { return customerName; }
    juce::String licenceId() const { return lid; }
    juce::String machineId() const;      // grouped, e.g. 1a2b-3c4d-…
    juce::String statusText() const;

    // Returns an error message, or empty on success.
    juce::String enterKey (const juce::String& key);
    juce::String enterActivation (const juce::String& code);
    juce::String activationRequest() const; // VBR1.… string the customer sends to the seller
    void removeLicence();

    static bool usingDevelopmentKey();

private:
    void load();
    void save() const;
    void evaluate();
    juce::File storeFile() const;

    juce::String key, activation, customerName, lid, expires;
    int maxDevices = 0;
    State currentState = State::noKey;
};
} // namespace vb
