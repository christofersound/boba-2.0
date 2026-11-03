#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using namespace juce;
using Float = AudioParameterFloat;

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto id = [] (const String& s) { return ParameterID { s, 1 }; };
    auto flt = [&] (const String& i, const String& name, NormalisableRange<float> r, float def, const char* unit)
    { p.push_back (std::make_unique<Float> (id (i), name, r, def, AudioParameterFloatAttributes().withLabel (unit))); };
    auto choice = [&] (const String& i, const String& name, StringArray items, int def)
    { p.push_back (std::make_unique<AudioParameterChoice> (id (i), name, items, def)); };
    auto boolean = [&] (const String& i, const String& name, bool def)
    { p.push_back (std::make_unique<AudioParameterBool> (id (i), name, def)); };

    // ---- main (soothe-style)
    flt ("depth",     "Depth",      { 0.f, 10.f, 0.01f },          4.5f,  "");
    flt ("detail",    "Detail",     { 0.f, 10.f, 0.01f },          3.0f,  "");
    flt ("sharpness", "Sharpness",  { 0.f, 1.f, 0.01f },           0.35f, "");          // soft .. hard
    flt ("attack",    "Attack",     { 0.f, 1200.f, 0.01f, 0.35f }, 10.f,  "ms");
    flt ("release",   "Release",    { 0.f, 1200.f, 0.01f, 0.35f }, 80.f,  "ms");

    // ---- tilt: shifts detail / attack / release along the spectrum
    flt ("tiltDetail",  "Tilt Detail",  { -100.f, 100.f, 0.1f }, 0.f, "%");
    flt ("tiltAttack",  "Tilt Attack",  { -100.f, 100.f, 0.1f }, 0.f, "%");
    flt ("tiltRelease", "Tilt Release", { -100.f, 100.f, 0.1f }, 0.f, "%");

    // ---- scale / range
    choice ("maxCut", "Max Cut", StringArray { "Off", "-3 dB", "-6 dB", "-9 dB", "-12 dB", "-18 dB", "-24 dB" }, 0);
    flt ("rangeLo", "Range Low",  { 20.f, 2000.f, 0.f, 0.35f },     20.f,    "Hz");
    flt ("rangeHi", "Range High", { 2000.f, 20000.f, 0.f, 0.35f },  20000.f, "Hz");

    // ---- stereo
    choice ("stereoMode", "Stereo Mode", StringArray { "L/R", "M/S" }, 0);
    flt ("link",  "Stereo Link", { 0.f, 100.f, 1.f },     100.f, "%");
    flt ("focus", "Focus",       { -100.f, 100.f, 1.f },  0.f,   "%");

    // ---- side chain / monitoring
    choice  ("sidechain", "Side Chain", StringArray { "Internal", "External" }, 0);
    boolean ("solo",   "Listen Side Chain", false);
    boolean ("delta",  "Delta",  false);
    boolean ("bypass", "Bypass", false);

    // ---- output
    flt ("wetTrim", "Wet Trim", { -12.f, 12.f, 0.1f },  0.f,   "dB");
    flt ("mix",     "Mix",      { 0.f, 100.f, 0.1f },   100.f, "%");
    flt ("out",     "Output",   { -24.f, 24.f, 0.1f },  0.f,   "dB");

    // ---- quality
    choice  ("quality", "Quality", StringArray { "Low", "Medium", "High", "Ultra" }, 2);
    boolean ("lowLatency", "Low Latency", false);

    // ---- engine
    choice ("engine", "Engine", StringArray { "Resonance", "Compressor" }, 0);
    flt ("threshold", "Threshold (Compressor)", { -70.f, 30.f, 0.1f }, 0.f, "dB");

    // ---- emphasis EQ on the detection signal (flat by default)
    flt ("eqHp", "EQ High-pass", { 20.f, 500.f, 0.f, 0.4f },     20.f,    "Hz");
    flt ("eqLp", "EQ Low-pass",  { 2000.f, 20000.f, 0.f, 0.4f }, 20000.f, "Hz");
    const char* names[3] = { "Low", "Mid", "High" };
    const NormalisableRange<float> fr[3] = { { 20.f, 1000.f, 0.f, 0.4f }, { 100.f, 5000.f, 0.f, 0.4f }, { 1000.f, 20000.f, 0.f, 0.4f } };
    const float defF[3] = { 121.87f, 548.f, 8307.35f }, defQ[3] = { 0.7547f, 0.7284f, 0.5211f };
    for (int i = 0; i < 3; ++i)
    {
        const String n = String (i + 1), label = String ("EQ ") + names[i];
        flt ("eq" + n + "Freq", label + " Freq", fr[i], defF[i], "Hz");
        flt ("eq" + n + "Gain", label + " Gain", { -18.f, 18.f, 0.01f }, 0.f, "dB");
        flt ("eq" + n + "Q",    label + " Q",    { 0.1f, 10.f, 0.01f, 0.5f }, defQ[i], "");
    }
    return { p.begin(), p.end() };
}

//==============================================================================
// Factory presets: values in real units; everything not listed goes back to its default
// (except output / monitoring / quality, which are left alone).
struct PresetValue { const char* id; float value; };
struct Preset { const char* name; std::vector<PresetValue> values; };

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Init", {} },
        { "Harsh Female Voc",   { { "depth", 4.5f }, { "detail", 3.0f }, { "sharpness", 0.45f }, { "attack", 6.f }, { "release", 40.f },
                                  { "tiltDetail", 40.f }, { "rangeLo", 150.f }, { "rangeHi", 19000.f } } },
        { "Sibilance Tamer",    { { "depth", 6.5f }, { "detail", 6.0f }, { "sharpness", 0.8f }, { "attack", 1.f }, { "release", 25.f },
                                  { "rangeLo", 4000.f }, { "tiltRelease", -30.f } } },
        { "Gentle Master",      { { "depth", 2.5f }, { "detail", 2.0f }, { "sharpness", 0.2f }, { "attack", 25.f }, { "release", 200.f },
                                  { "stereoMode", 1.f }, { "maxCut", 2.f } } },
        { "Muddy Low-End",      { { "depth", 4.0f }, { "detail", 1.5f }, { "sharpness", 0.3f }, { "attack", 30.f }, { "release", 150.f },
                                  { "rangeHi", 1200.f }, { "tiltDetail", -50.f } } },
        { "Spectral Ducker",    { { "engine", 1.f }, { "threshold", -20.f }, { "depth", 6.f }, { "sharpness", 0.5f },
                                  { "attack", 5.f }, { "release", 120.f }, { "sidechain", 1.f } } },
    };
    return list;
}

void qualityToFFT (int quality, bool lowLatency, int& n, int& overlap)
{
    static const int sizes[4] = { 1024, 2048, 2048, 4096 }, overlaps[4] = { 4, 4, 8, 8 };
    quality = juce::jlimit (0, 3, quality);
    n = sizes[quality]; overlap = overlaps[quality];
    if (lowLatency) { n = 512; overlap = 4; }          // ~10 ms at 48 kHz
}
} // namespace

BobaAudioProcessor::BobaAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "BobaState", createLayout())
{
}

void BobaAudioProcessor::updateEngineSize()
{
    int n = 2048, ov = 8;
    qualityToFFT ((int) get ("quality"), get ("lowLatency") > 0.5f, n, ov);
    if (n != core.fftSize() || ov != core.overlap())
    {
        core.setFFT (n, ov);                   // no allocation: capacity is reserved up front
        setLatencySamples (core.latencySamples());
    }
}

void BobaAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    core.prepare (sr);
    int n = 2048, ov = 8;
    qualityToFFT ((int) get ("quality"), get ("lowLatency") > 0.5f, n, ov);
    core.setFFT (n, ov);
    setLatencySamples (core.latencySamples());
}

bool BobaAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainInputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::stereo();
}

boba::Settings BobaAudioProcessor::readSettings() const
{
    static const float maxCuts[7] = { 0.f, 3.f, 6.f, 9.f, 12.f, 18.f, 24.f };
    boba::Settings s;
    s.resonanceMode = get ("engine") < 0.5f;
    s.thresholdDb   = get ("threshold");
    s.depth         = get ("depth");
    s.detail        = get ("detail");
    s.sharpness     = get ("sharpness");
    s.attackMs      = get ("attack");
    s.releaseMs     = get ("release");
    s.tiltDetail    = get ("tiltDetail")  * 0.01f;
    s.tiltAttack    = get ("tiltAttack")  * 0.01f;
    s.tiltRelease   = get ("tiltRelease") * 0.01f;
    s.maxCutDb      = maxCuts[juce::jlimit (0, 6, (int) get ("maxCut"))];
    s.rangeLoHz     = get ("rangeLo");
    s.rangeHiHz     = get ("rangeHi");
    s.linkPercent   = get ("link");
    s.focus         = get ("focus") * 0.01f;
    s.midSide       = get ("stereoMode") > 0.5f;
    s.externalSidechain = get ("sidechain") > 0.5f;
    s.solo          = get ("solo") > 0.5f;
    s.delta         = get ("delta") > 0.5f;
    s.bypass        = get ("bypass") > 0.5f;
    s.mix           = get ("mix") * 0.01f;
    s.wetTrimDb     = get ("wetTrim");
    s.outDb         = get ("out");
    s.eq.hpFreq = get ("eqHp"); s.eq.lpFreq = get ("eqLp");
    const char* f[3] = { "eq1Freq", "eq2Freq", "eq3Freq" }; const char* g[3] = { "eq1Gain", "eq2Gain", "eq3Gain" }; const char* q[3] = { "eq1Q", "eq2Q", "eq3Q" };
    for (int i = 0; i < 3; ++i) { s.eq.peakFreq[i] = get (f[i]); s.eq.peakGainDb[i] = get (g[i]); s.eq.peakQ[i] = get (q[i]); }
    return s;
}

void BobaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    updateEngineSize();

    const float *scL = nullptr, *scR = nullptr;
    if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() >= 2) { scL = sc.getReadPointer (0); scR = sc.getReadPointer (1); }
    }

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);
    const int n = buffer.getNumSamples();

    // Feed the spectrum display (lock-free, display only).
    constexpr int mask = kScopeSize - 1;
    const int w = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) scopeIn[(size_t) ((w + i) & mask)].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);

    core.process (l, r, scL, scR, l, r, n, readSettings());

    for (int i = 0; i < n; ++i) scopeOut[(size_t) ((w + i) & mask)].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);
    scopeWrite.store ((w + n) & mask, std::memory_order_release);
}

void BobaAudioProcessor::copyScope (bool output, float* dest, int num) const
{
    num = juce::jlimit (0, kScopeSize, num);
    const int w = scopeWrite.load (std::memory_order_acquire);
    const auto& src = output ? scopeOut : scopeIn;
    for (int i = 0; i < num; ++i)
        dest[i] = src[(size_t) ((w - num + i + kScopeSize) & (kScopeSize - 1))].load (std::memory_order_relaxed);
}

//==============================================================================
int BobaAudioProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String BobaAudioProcessor::getProgramName (int index)
{
    const auto& list = presets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? juce::String (list[(size_t) index].name) : juce::String();
}

void BobaAudioProcessor::setCurrentProgram (int index)
{
    const auto& list = presets();
    if (! juce::isPositiveAndBelow (index, (int) list.size())) return;
    currentProgram = index;

    static const char* keep[] = { "out", "mix", "wetTrim", "bypass", "solo", "delta", "quality", "lowLatency", "sidechain" };
    auto isKept = [] (const juce::String& pid)
    {
        for (auto* k : keep) if (pid == k) return true;
        return false;
    };

    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (! isKept (rp->paramID))
                rp->setValueNotifyingHost (rp->getDefaultValue());

    for (const auto& v : list[(size_t) index].values)
        if (auto* rp = apvts.getParameter (v.id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (v.value));
}

//==============================================================================
void BobaAudioProcessor::switchSlot()
{
    slotState[slotB ? 1 : 0] = apvts.copyState();
    slotB = ! slotB;
    const auto& target = slotState[slotB ? 1 : 0];
    if (target.isValid()) apvts.replaceState (target.createCopy());   // first time: B starts as a copy of A
}

void BobaAudioProcessor::copyToOtherSlot()
{
    slotState[slotB ? 0 : 1] = apvts.copyState();
}

juce::AudioProcessorEditor* BobaAudioProcessor::createEditor() { return new BobaEditor (*this); }

void BobaAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void BobaAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BobaAudioProcessor(); }
