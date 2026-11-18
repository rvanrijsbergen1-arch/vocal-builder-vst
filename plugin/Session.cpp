#include "Session.h"

namespace vb
{
juce::File Session::rootDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("VocalBuilder");
}

Session Session::createNew()
{
    Session s;
    s.dir = rootDir().getChildFile ("sessions").getChildFile (juce::Uuid().toString());
    s.dir.createDirectory();
    return s;
}

juce::String Session::keyFor (double start, double end, bool full)
{
    if (full)
        return "full";
    return juce::String (juce::roundToInt (start * 1000.0)) + "-" + juce::String (juce::roundToInt (end * 1000.0));
}

juce::var Session::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("dir", dir.getFullPathName());
    o->setProperty ("source", source.getFullPathName());
    o->setProperty ("isolated", isolatedVocal);
    o->setProperty ("duration", duration);
    o->setProperty ("sourceRate", sourceRate);
    o->setProperty ("sourceChannels", sourceChannels);
    o->setProperty ("regionStart", regionStart);
    o->setProperty ("regionEnd", regionEnd);
    o->setProperty ("regionFull", regionFull);
    o->setProperty ("segmentKey", segmentKey);
    o->setProperty ("extracted", extracted);
    o->setProperty ("hasInstrumental", hasInstrumental);
    o->setProperty ("hasBacking", hasBacking);
    o->setProperty ("leadOnly", leadOnly);
    o->setProperty ("dereverb", dereverb);
    o->setProperty ("separationEngine", separationEngine);
    o->setProperty ("rmsVocal", rmsVocal);
    o->setProperty ("rmsInstrumental", rmsInstrumental);
    juce::Array<juce::var> convs;
    for (auto& c : conversions)
    {
        auto* co = new juce::DynamicObject();
        co->setProperty ("id", c.id);
        co->setProperty ("voiceId", c.voiceId);
        co->setProperty ("voiceName", c.voiceName);
        co->setProperty ("pitch", c.pitch);
        co->setProperty ("indexRate", c.indexRate);
        co->setProperty ("protect", c.protect);
        co->setProperty ("dynamics", c.dynamics);
        co->setProperty ("rmsDb", c.rmsDb);
        co->setProperty ("file", c.file.getFullPathName());
        convs.add (juce::var (co));
    }
    o->setProperty ("conversions", convs);
    return juce::var (o);
}

Session Session::fromVar (const juce::var& v)
{
    Session s;
    s.dir = juce::File (v["dir"].toString());
    s.source = juce::File (v["source"].toString());
    s.isolatedVocal = v["isolated"];
    s.duration = v["duration"];
    s.sourceRate = v["sourceRate"];
    s.sourceChannels = v["sourceChannels"];
    s.regionStart = v["regionStart"];
    s.regionEnd = v["regionEnd"];
    s.regionFull = v["regionFull"];
    s.segmentKey = v["segmentKey"].toString();
    s.extracted = v["extracted"];
    s.hasInstrumental = v["hasInstrumental"];
    s.hasBacking = v["hasBacking"];
    s.leadOnly = v["leadOnly"];
    s.dereverb = v["dereverb"];
    s.separationEngine = v["separationEngine"].toString();
    s.rmsVocal = v["rmsVocal"];
    s.rmsInstrumental = v["rmsInstrumental"];
    if (auto* arr = v["conversions"].getArray())
        for (auto& c : *arr)
        {
            Conversion cv;
            cv.id = c["id"].toString();
            cv.voiceId = c["voiceId"].toString();
            cv.voiceName = c["voiceName"].toString();
            cv.pitch = c["pitch"];
            cv.indexRate = c["indexRate"];
            cv.protect = c["protect"];
            cv.dynamics = c["dynamics"];
            cv.rmsDb = c["rmsDb"];
            cv.file = juce::File (c["file"].toString());
            if (cv.file.existsAsFile())
                s.conversions.add (cv);
        }
    // Files may have been cleaned up since the project was saved.
    if (s.extracted && ! s.vocalsFile().existsAsFile())
    {
        s.extracted = false;
        s.conversions.clear();
    }
    return s;
}

void Session::save() const
{
    if (dir.isDirectory())
        dir.getChildFile ("session.json").replaceWithText (juce::JSON::toString (toVar()));
}
} // namespace vb
