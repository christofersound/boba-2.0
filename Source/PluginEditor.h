// PluginEditor.h - Boba UI, estilo Soothe3: fondo oscuro, espectro grande con la curva de reduccion,
// marcadores de rango, mandos finos con arco, y controles agrupados abajo. TODO en este unico archivo (header-only).
// PluginEditor.cpp solo necesita:  #include "PluginEditor.h"
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <BinaryData.h>
#include <array>
#include <complex>
#include <functional>
#include <initializer_list>
#include <memory>
#include <vector>
#include "PluginProcessor.h"

namespace bobaui
{
namespace colours
{
    inline const juce::Colour bg1       { 0xff202226 };
    inline const juce::Colour bg2       { 0xff141518 };
    inline const juce::Colour panel     { 0xff1a1b1f };
    inline const juce::Colour panelEdge { 0xff2e3036 };
    inline const juce::Colour display   { 0xff0f1012 };
    inline const juce::Colour text      { 0xffe8e9ec };
    inline const juce::Colour dim       { 0xff80848c };
    inline const juce::Colour accent    { 0xffff7a50 };   // reduccion / valor de los mandos
    inline const juce::Colour accent2   { 0xffffb36b };   // hover
    inline const juce::Colour eq        { 0xff8ec9ff };   // curva del EQ de enfasis
    inline const juce::Colour seg       { 0xff3a3e46 };   // opcion activa en los selectores
}

constexpr float kWidth = 900.0f, kHeight = 590.0f;        // tamano logico; el editor lo escala

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::String fmtMs (double v)     { return v < 100.0 ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
inline juce::String fmtPct (double v)    { return juce::String (juce::roundToInt (v)) + " %"; }
inline juce::String fmtSigned (double v) { const int i = juce::roundToInt (v); return (i > 0 ? juce::String ("+") : juce::String()) + juce::String (i) + " %"; }
inline juce::String fmtDb (double v)     { return juce::String (v, 1) + " dB"; }
inline juce::String fmt1 (double v)      { return juce::String (v, 1); }

//==============================================================================
class BobaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BobaLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, colours::display);
        setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::arrowColourId, colours::dim);
        setColour (juce::PopupMenu::backgroundColourId, colours::panel);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::seg);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
        setColour (juce::Label::textColourId, colours::dim);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        g.setColour (colours::display);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
        juce::Path p;
        p.startNewSubPath (cx - 3.5f, cy - 1.5f);
        p.lineTo (cx, cy + 2.0f);
        p.lineTo (cx + 3.5f, cy - 1.5f);
        g.setColour (colours::dim);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
};

//==============================================================================
// Mando rotativo fino: titulo arriba, arco de valor, valor abajo.
class Knob : public juce::Slider
{
public:
    using Formatter = std::function<juce::String (double)>;

    Knob (juce::String t, Formatter f) : title (std::move (t)), format (std::move (f))
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setMouseDragSensitivity (220);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        g.setColour (colours::dim);
        g.setFont (font (10.0f, true).withExtraKerningFactor (0.14f));
        g.drawText (title, b.removeFromTop (16.0f), juce::Justification::centred);

        auto valueArea = b.removeFromBottom (18.0f);
        const float dia = std::min (b.getWidth(), b.getHeight()) - 4.0f;
        const auto k = juce::Rectangle<float> (dia, dia).withCentre (b.getCentre());
        const float cx = k.getCentreX(), cy = k.getCentreY(), r = dia * 0.5f;

        const float a0 = -2.35f, a1 = 2.35f;
        const float prop = (float) valueToProportionOfLength (getValue());
        const float ang = a0 + prop * (a1 - a0);
        const bool hot = hover || isMouseButtonDown();

        juce::Path track;
        track.addCentredArc (cx, cy, r - 2.0f, r - 2.0f, 0.0f, a0, a1, true);
        g.setColour (juce::Colour (0xff34363c));
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        const float aFrom = bipolar ? 0.0f : a0;
        val.addCentredArc (cx, cy, r - 2.0f, r - 2.0f, 0.0f, std::min (aFrom, ang), std::max (aFrom, ang), true);
        g.setColour (hot ? colours::accent2 : colours::accent);
        g.strokePath (val, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto body = k.reduced (8.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff34363c), body.getX(), body.getY(),
                                                 juce::Colour (0xff1c1d21), body.getX(), body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (juce::Colour (0xff4a4d55));
        g.drawEllipse (body, 1.0f);

        const float br = body.getWidth() * 0.5f, p0 = br * 0.35f, p1 = br * 0.85f;
        g.setColour (colours::text);
        g.drawLine (cx + p0 * std::sin (ang), cy - p0 * std::cos (ang),
                    cx + p1 * std::sin (ang), cy - p1 * std::cos (ang), 2.0f);

        g.setColour (hot ? colours::text : juce::Colour (0xffb4b7be));
        g.setFont (font (12.0f));
        g.drawText (format ? format (getValue()) : juce::String (getValue(), 1), valueArea, juce::Justification::centred);
    }

    void setBipolar (bool b) { bipolar = b; repaint(); }

    void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent& e) override { juce::Slider::mouseExit (e);  hover = false; repaint(); }

private:
    juce::String title;
    Formatter format;
    bool hover = false;
    bool bipolar = false;
};

// Selector de dos opciones: L/R <-> M/S, INT <-> EXT.
class SegToggle : public juce::Button
{
public:
    SegToggle (juce::String off, juce::String on)
        : juce::Button (off + "/" + on), offText (std::move (off)), onText (std::move (on))
    {
        setClickingTogglesState (true);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        const auto left = r.withWidth (r.getWidth() * 0.5f);
        const auto right = r.withLeft (r.getCentreX());

        g.setColour (colours::display);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (over ? colours::seg.brighter (0.15f) : colours::seg);
        g.fillRoundedRectangle ((on ? right : left).reduced (2.0f), 4.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        g.setFont (font (11.0f, true).withExtraKerningFactor (0.08f));
        g.setColour (on ? colours::dim : colours::text);
        g.drawText (offText, left, juce::Justification::centred);
        g.setColour (on ? colours::text : colours::dim);
        g.drawText (onText, right, juce::Justification::centred);
    }

private:
    juce::String offText, onText;
};

// Boton de texto encendido/apagado: SOLO, DELTA.
class TextToggle : public juce::Button
{
public:
    explicit TextToggle (juce::String t, bool toggles = true) : juce::Button (t) { setButtonText (t); setClickingTogglesState (toggles); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        g.setColour (on ? colours::accent : colours::display);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (on ? colours::accent : colours::panelEdge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setFont (font (11.0f, true).withExtraKerningFactor (0.1f));
        g.setColour (on ? juce::Colour (0xff1a1208) : (over ? colours::text : colours::dim));
        g.drawText (getButtonText(), r, juce::Justification::centred);
    }
};

// Engranaje de ajustes (FFT, overlap, modo de tiempo).
class GearButton : public juce::Button
{
public:
    GearButton() : juce::Button ("settings") { setClickingTogglesState (true); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced (5.0f);
        const float rad = std::min (r.getWidth(), r.getHeight()) * 0.5f;
        const float cx = r.getCentreX(), cy = r.getCentreY();
        g.setColour (getToggleState() || over ? colours::text : colours::dim);
        g.drawEllipse (r.withSizeKeepingCentre (rad * 1.3f, rad * 1.3f), 1.8f);
        g.drawEllipse (r.withSizeKeepingCentre (rad * 0.5f, rad * 0.5f), 1.4f);
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::pi * 0.25f;
            g.drawLine (cx + std::sin (a) * rad * 0.8f, cy - std::cos (a) * rad * 0.8f,
                        cx + std::sin (a) * rad * 1.05f, cy - std::cos (a) * rad * 1.05f, 3.0f);
        }
    }
};

//==============================================================================
// Espectro (entrada rellena, salida en linea, reduccion resaltada) con el EQ de enfasis interactivo.
class EqDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EqDisplay (BobaAudioProcessor& p) : proc (p)
    {
        fft.init (kFFT);
        for (int i = 0; i < kFFT; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * boba::kPi * i / kFFT));
        specIn.fill (-140.0f);
        specOut.fill (-140.0f);

        auto P = [this] (const char* id) { return proc.apvts.getParameter (id); };
        nodes[0] = { "High-pass", highpass, P ("eqHp"),    nullptr,       nullptr };
        nodes[1] = { "Low",       peak,     P ("eq1Freq"), P ("eq1Gain"), P ("eq1Q") };
        nodes[2] = { "Mid",       peak,     P ("eq2Freq"), P ("eq2Gain"), P ("eq2Q") };
        nodes[3] = { "High",      peak,     P ("eq3Freq"), P ("eq3Gain"), P ("eq3Q") };
        nodes[4] = { "Low-pass",  lowpass,  P ("eqLp"),    nullptr,       nullptr };
        pRangeLo = P ("rangeLo");
        pRangeHi = P ("rangeHi");
        curve.fill (0.0f);

        rangeBtn.onClick = [this] { cycleRange(); };
        eqBtn.onClick = [this] { repaint(); };
        addAndMakeVisible (rangeBtn);
        addAndMakeVisible (eqBtn);

        startTimerHz (30);
    }
    ~EqDisplay() override { stopTimer(); }

    void updateSpectrum()
    {
        proc.copyScope (false, scratch.data(), kFFT);
        analyse (scratch.data(), specIn);
        proc.copyScope (true, scratch.data(), kFFT);
        analyse (scratch.data(), specOut);
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const float w = bounds.getWidth(), h = bounds.getHeight();

        g.setColour (colours::display);
        g.fillRoundedRectangle (bounds, 10.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (bounds, 10.0f);
            g.reduceClipRegion (clip, juce::AffineTransform());
            drawGrid (g, w, h);
            drawSpectra (g, w, h);
            if (eqVisible()) drawEq (g, w, h);
            else { drawReduction (g, w, h); drawRange (g, w, h); }
            drawOverlay (g);
        }
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);
    }

    void resized() override
    {
        const int w = getWidth();
        eqBtn.setBounds (w - 164, 10, 92, 20);
        rangeBtn.setBounds (w - 62, 10, 52, 20);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = nodeAt (e.position), m = markerAt (e.position);
        if (h != hover || m != markerHover)
        {
            hover = h; markerHover = m;
            setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor
                                   : (m >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor));
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; markerHover = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = nodeAt (e.position);
        markerDrag = dragging >= 0 ? -1 : markerAt (e.position);
        if (dragging >= 0) beginGesture (dragging);
        if (markerDrag >= 0) markerParam (markerDrag)->beginChangeGesture();
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (markerDrag >= 0)
        {
            const float f = xToFreq (e.position.x);
            setReal (markerParam (markerDrag), markerDrag == 0 ? juce::jlimit (20.0f, 2000.0f, f) : juce::jlimit (2000.0f, 20000.0f, f));
            repaint();
            return;
        }
        if (dragging < 0) return;
        auto& n = nodes[(size_t) dragging];
        setReal (n.freq, xToFreq (e.position.x));
        if (n.gain != nullptr) setReal (n.gain, yToGain (e.position.y));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0) endGesture (dragging);
        if (markerDrag >= 0) markerParam (markerDrag)->endChangeGesture();
        dragging = -1;
        markerDrag = -1;
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int m = markerAt (e.position);
        if (m >= 0)
        {
            auto* mp = markerParam (m);
            mp->beginChangeGesture();
            mp->setValueNotifyingHost (mp->getDefaultValue());
            mp->endChangeGesture();
            repaint();
            return;
        }
        const int i = nodeAt (e.position);
        if (i < 0) return;
        for (auto* p : { nodes[(size_t) i].freq, nodes[(size_t) i].gain, nodes[(size_t) i].q })
            if (p != nullptr)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getDefaultValue());
                p->endChangeGesture();
            }
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wd) override
    {
        const int i = nodeAt (e.position);
        if (i < 0 || nodes[(size_t) i].q == nullptr) return;
        auto* q = nodes[(size_t) i].q;
        q->beginChangeGesture();
        setReal (q, real (q) * std::exp (wd.deltaY * 2.0f));
        q->endChangeGesture();
        repaint();
    }

private:
    enum Kind { highpass, peak, lowpass };
    struct Node
    {
        const char* name = "";
        Kind kind = peak;
        juce::RangedAudioParameter* freq = nullptr;
        juce::RangedAudioParameter* gain = nullptr;
        juce::RangedAudioParameter* q    = nullptr;
    };

    static constexpr int kFFT = 2048;
    static constexpr int kBins = kFFT / 2 + 1;
    static constexpr float kDbTop = -15.0f, kDbBottom = -105.0f;   // rango visible del espectro

    void timerCallback() override
    {
        updateSpectrum();
        float raw[boba::kCurvePoints];
        proc.copyCurve (raw);
        for (int i = 0; i < boba::kCurvePoints; ++i)
            curve[(size_t) i] += (raw[i] - curve[(size_t) i]) * (raw[i] > curve[(size_t) i] ? 0.5f : 0.2f);
        if (isShowing()) repaint();
    }

    //----------------------------------------------------------------------- reduction curve + range markers
    bool eqVisible() const { return eqBtn.getToggleState(); }

    void cycleRange()
    {
        static const float steps[] = { 3.0f, 6.0f, 9.0f, 12.0f, 18.0f, 24.0f };
        int i = 0;
        for (int j = 0; j < 6; ++j) if (steps[j] == dispRange) i = j;
        dispRange = steps[(i + 1) % 6];
        rangeBtn.setButtonText ("-" + juce::String ((int) dispRange) + " dB");
        repaint();
    }

    float redY (float db) const { const float h = (float) getHeight(); return 0.14f * h + juce::jlimit (0.0f, 1.0f, db / dispRange) * 0.40f * h; }

    juce::RangedAudioParameter* markerParam (int i) const { return i == 0 ? pRangeLo : pRangeHi; }
    float markerX (int i) const { return juce::jlimit (7.0f, (float) getWidth() - 7.0f, freqToX (real (markerParam (i)))); }

    int markerAt (juce::Point<float> p) const
    {
        if (eqVisible()) return -1;
        int best = -1; float bestD = 9.0f;
        for (int i = 0; i < 2; ++i)
        {
            const float d = std::abs (p.x - markerX (i));
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    void drawReduction (juce::Graphics& g, float w, float)
    {
        const float y0 = redY (0.0f);
        juce::Path line, area;
        for (int i = 0; i < boba::kCurvePoints; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / (float) (boba::kCurvePoints - 1));
            const float x = freqToX (f), y = redY (curve[(size_t) i]);
            if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        area = line;
        area.lineTo (w, y0);
        area.lineTo (0.0f, y0);
        area.closeSubPath();

        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.70f), 0.0f, y0,
                                                 colours::accent.withAlpha (0.06f), 0.0f, redY (dispRange), false));
        g.fillPath (area);

        const float dashes[] = { 3.0f, 4.0f };
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawDashedLine (juce::Line<float> (0.0f, y0, w, y0), dashes, 2, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (colours::dim.withAlpha (0.9f));
        g.setFont (font (9.5f));
        g.drawText ("0", juce::Rectangle<float> (w - 30.0f, y0 - 13.0f, 22.0f, 12.0f), juce::Justification::right);
        for (int j = 1; j <= 3; ++j)
        {
            const float db = dispRange * (float) j / 3.0f;
            g.drawText ("-" + juce::String (db, dispRange < 9.0f ? 1 : 0),
                        juce::Rectangle<float> (w - 30.0f, redY (db) - 6.0f, 22.0f, 12.0f), juce::Justification::right);
        }
    }

    void drawRange (juce::Graphics& g, float w, float h)
    {
        const float xl = markerX (0), xh = markerX (1), y0 = redY (0.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (0.0f, 0.0f, std::max (0.0f, xl), h);
        g.fillRect (xh, 0.0f, std::max (0.0f, w - xh), h);
        const juce::Colour cols[2] = { juce::Colour (0xffff5a6e), juce::Colour (0xff4cd98f) };
        const float xs[2] = { xl, xh };
        for (int i = 0; i < 2; ++i)
        {
            const bool hot = (i == markerHover || i == markerDrag);
            g.setColour (cols[i].withAlpha (hot ? 0.9f : 0.45f));
            g.drawVerticalLine ((int) xs[i], 0.0f, h);
            g.setColour (cols[i]);
            g.fillRoundedRectangle (xs[i] - 4.0f, y0 - 9.0f, 8.0f, 18.0f, 3.0f);
        }
    }

    double sr() const { const double s = proc.getSampleRate(); return s > 0.0 ? s : 48000.0; }

    // Magnitud en dB (con +4.5 dB/oct de inclinacion para que la musica se vea plana) suavizada en el tiempo.
    void analyse (const float* x, std::array<float, kBins>& smoothed)
    {
        for (int i = 0; i < kFFT; ++i) fbuf[(size_t) i] = { x[i] * window[(size_t) i], 0.0f };
        fft.forward (fbuf.data());
        const double binHz = sr() / kFFT;
        for (int k = 0; k < kBins; ++k)
        {
            const float mag = std::abs (fbuf[(size_t) k]) * (4.0f / (float) kFFT);
            float db = 20.0f * std::log10 (std::max (mag, 1.0e-7f));
            db += 4.5f * (float) std::log2 (std::max ((double) k, 0.5) * binHz / 1000.0);
            auto& s = smoothed[(size_t) k];
            s += (db - s) * (db > s ? 0.5f : 0.12f);
        }
    }

    float sampleSpectrum (const std::array<float, kBins>& s, float f0, float f1) const
    {
        const float binHz = (float) (sr() / kFFT);
        const float b0 = f0 / binHz, b1 = f1 / binHz;
        if (b1 - b0 < 1.0f)
        {
            const float b = juce::jlimit (0.0f, (float) (kBins - 1), 0.5f * (b0 + b1));
            const int i = (int) b, j = std::min (i + 1, kBins - 1);
            return s[(size_t) i] + (s[(size_t) j] - s[(size_t) i]) * (b - (float) i);
        }
        const int i0 = juce::jlimit (0, kBins - 1, (int) std::ceil (b0));
        const int i1 = std::max (i0, juce::jlimit (0, kBins - 1, (int) std::floor (b1)));
        float sum = 0.0f;
        for (int i = i0; i <= i1; ++i) sum += s[(size_t) i];
        return sum / (float) (i1 - i0 + 1);
    }

    void buildPoints (const std::array<float, kBins>& s, int n, float w, std::vector<float>& out)
    {
        out.assign ((size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const float x = w * (float) i / (float) (n - 1);
            out[(size_t) i] = sampleSpectrum (s, xToFreq (x - 1.5f), xToFreq (x + 1.5f));
        }
        tmp = out;
        for (int i = 1; i < n - 1; ++i)
            out[(size_t) i] = 0.25f * tmp[(size_t) i - 1] + 0.5f * tmp[(size_t) i] + 0.25f * tmp[(size_t) i + 1];
    }

    void drawGrid (juce::Graphics& g, float w, float h)
    {
        const float lines[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
        for (float f : lines)
        {
            const float x = freqToX (f);
            const bool major = (f == 100.0f || f == 1000.0f || f == 10000.0f);
            g.setColour (juce::Colours::white.withAlpha (major ? 0.09f : 0.045f));
            g.drawVerticalLine ((int) x, 0.0f, h);
            g.setColour (colours::dim.withAlpha (0.8f));
            g.setFont (font (10.0f));
            const juce::String t = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
            g.drawText (t, juce::Rectangle<float> (x + 3.0f, h - 16.0f, 36.0f, 12.0f), juce::Justification::left);
        }
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        for (int i = 1; i < 6; ++i)
            g.drawHorizontalLine ((int) (h * (float) i / 6.0f), 0.0f, w);
    }

    void drawSpectra (juce::Graphics& g, float w, float h)
    {
        const int n = std::max (2, (int) (w / 2.0f) + 1);
        buildPoints (specIn, n, w, ptsIn);
        buildPoints (specOut, n, w, ptsOut);

        auto X = [w, n] (int i) { return w * (float) i / (float) (n - 1); };
        auto Y = [h] (float db) { return juce::jlimit (0.0f, h, h * (kDbTop - db) / (kDbTop - kDbBottom)); };

        juce::Path inLine, outLine, fill, red;
        for (int i = 0; i < n; ++i)
        {
            const float x = X (i), yi = Y (ptsIn[(size_t) i]), yo = Y (ptsOut[(size_t) i]);
            if (i == 0) { inLine.startNewSubPath (x, yi); outLine.startNewSubPath (x, yo); }
            else        { inLine.lineTo (x, yi);          outLine.lineTo (x, yo); }
        }
        fill = inLine;
        fill.lineTo (w, h);
        fill.lineTo (0.0f, h);
        fill.closeSubPath();

        red.startNewSubPath (0.0f, Y (ptsIn[0]));
        for (int i = 0; i < n; ++i) red.lineTo (X (i), Y (ptsIn[(size_t) i]));
        for (int i = n - 1; i >= 0; --i) red.lineTo (X (i), std::max (Y (ptsIn[(size_t) i]), Y (ptsOut[(size_t) i])));
        red.closeSubPath();

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillPath (fill);

        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.9f), 0.0f, 0.0f,
                                                 colours::accent.withAlpha (0.35f), 0.0f, h, false));
        g.fillPath (red);

        const juce::PathStrokeType thin (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const juce::PathStrokeType thick (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (juce::Colours::white.withAlpha (0.30f));
        g.strokePath (inLine, thin);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.strokePath (outLine, thick);
    }

    void drawEq (juce::Graphics& g, float w, float)
    {
        const auto filters = buildFilters();
        const double fs = sr();
        const float zeroY = gainToY (0.0f);

        juce::Path curve, area;
        for (float x = 0.0f; x <= w; x += 2.0f)
        {
            const double db = juce::jlimit (-24.0, 24.0, responseDb (filters, (double) xToFreq (x), fs));
            const float y = gainToY ((float) db);
            if (x == 0.0f) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        area = curve;
        area.lineTo (w, zeroY);
        area.lineTo (0.0f, zeroY);
        area.closeSubPath();

        g.setColour (colours::eq.withAlpha (0.07f));
        g.fillPath (area);

        const float dashes[] = { 3.0f, 4.0f };
        g.setColour (colours::eq.withAlpha (0.25f));
        g.drawDashedLine (juce::Line<float> (0.0f, zeroY, w, zeroY), dashes, 2, 1.0f);

        g.setColour (colours::eq.withAlpha (0.9f));
        g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (int i = 0; i < (int) nodes.size(); ++i)
        {
            const auto p = nodePos (i);
            const bool hot = (i == hover || i == dragging);
            const float r = hot ? 7.0f : 5.0f;
            g.setColour (i == dragging ? colours::eq : colours::display);
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
            g.setColour (colours::eq);
            g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, 1.6f);
        }
    }

    void drawOverlay (juce::Graphics& g)
    {
        // leyenda
        struct Item { const char* label; juce::Colour c; float width; };
        const Item items[] = { { "INPUT", juce::Colours::white.withAlpha (0.35f), 66.0f },
                               { "OUTPUT", juce::Colours::white.withAlpha (0.92f), 76.0f },
                               { "REDUCTION", colours::accent, 96.0f } };
        float x = 14.0f;
        g.setFont (font (9.5f, true).withExtraKerningFactor (0.12f));
        for (const auto& it : items)
        {
            g.setColour (it.c);
            g.fillRoundedRectangle (x, 15.0f, 8.0f, 8.0f, 2.0f);
            g.setColour (colours::dim);
            g.drawText (it.label, juce::Rectangle<float> (x + 13.0f, 11.0f, it.width, 16.0f), juce::Justification::left);
            x += it.width + 6.0f;
        }

        // lectura del punto bajo el raton
        const int idx = dragging >= 0 ? dragging : hover;
        if (idx >= 0)
        {
            g.setColour (colours::eq);
            g.setFont (font (12.0f, true));
            g.drawText (describe (idx), getLocalBounds().reduced (14, 10).withHeight (16).withTrimmedRight (170), juce::Justification::topRight);
        }
    }

    //----------------------------------------------------------------------- EQ
    std::array<boba::Biquad, 5> buildFilters() const
    {
        const double fs = sr();
        std::array<boba::Biquad, 5> f;
        f[0].setHighpass (fs, real (nodes[0].freq), 0.70710678);
        for (size_t i = 0; i < 3; ++i)
            f[1 + i].setPeak (fs, real (nodes[1 + i].freq), real (nodes[1 + i].q), real (nodes[1 + i].gain));
        f[4].setLowpass (fs, real (nodes[4].freq), 0.849070);
        return f;
    }

    static double responseDb (const std::array<boba::Biquad, 5>& f, double freq, double fs)
    {
        const double w = 2.0 * boba::kPi * freq / fs;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        double db = 0.0;
        for (const auto& b : f)
        {
            const std::complex<double> num = b.b0 + b.b1 * z1 + b.b2 * z2;
            const std::complex<double> den = 1.0 + b.a1 * z1 + b.a2 * z2;
            db += 20.0 * std::log10 (std::max (std::abs (num) / std::max (std::abs (den), 1.0e-12), 1.0e-6));
        }
        return db;
    }

    float freqToX (float f) const { return (float) (getWidth() * std::log ((double) f / 20.0) / std::log (1000.0)); }
    float xToFreq (float x) const { return (float) (20.0 * std::pow (1000.0, (double) x / (double) std::max (1, getWidth()))); }
    // escala del EQ: +-18 dB sobre la mitad superior de la pantalla
    float gainToY (float g) const { const float h = (float) getHeight(); return 0.26f * h - g / 18.0f * 0.20f * h; }
    float yToGain (float y) const { const float h = (float) getHeight(); return (0.26f * h - y) / (0.20f * h) * 18.0f; }

    juce::Point<float> nodePos (int i) const
    {
        const auto& n = nodes[(size_t) i];
        const float f = real (n.freq);
        if (n.gain != nullptr) return { freqToX (f), gainToY (real (n.gain)) };
        const double db = juce::jlimit (-24.0, 24.0, responseDb (buildFilters(), (double) f, sr()));
        return { freqToX (f), gainToY ((float) db) };
    }

    int nodeAt (juce::Point<float> p) const
    {
        if (! eqVisible()) return -1;
        int best = -1; float bestD = 12.0f;
        for (int i = 0; i < (int) nodes.size(); ++i)
        {
            const float d = nodePos (i).getDistanceFrom (p);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    static float real (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }
    static void setReal (juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); }

    void beginGesture (int i)
    {
        auto& n = nodes[(size_t) i];
        n.freq->beginChangeGesture();
        if (n.gain != nullptr) n.gain->beginChangeGesture();
    }
    void endGesture (int i)
    {
        auto& n = nodes[(size_t) i];
        n.freq->endChangeGesture();
        if (n.gain != nullptr) n.gain->endChangeGesture();
    }

    juce::String describe (int i) const
    {
        const auto& n = nodes[(size_t) i];
        juce::String s (n.name);
        s << "   " << juce::String (real (n.freq), 0) << " Hz";
        if (n.gain != nullptr) s << "   " << juce::String (real (n.gain), 1) << " dB";
        if (n.q != nullptr)    s << "   Q " << juce::String (real (n.q), 2);
        return s;
    }

    BobaAudioProcessor& proc;
    std::array<Node, 5> nodes;
    int hover = -1, dragging = -1, markerHover = -1, markerDrag = -1;
    juce::RangedAudioParameter* pRangeLo = nullptr;
    juce::RangedAudioParameter* pRangeHi = nullptr;
    std::array<float, boba::kCurvePoints> curve {};
    float dispRange = 9.0f;
    TextToggle eqBtn { "EMPH EQ" };
    TextToggle rangeBtn { "-9 dB", false };

    boba::FFT fft;
    std::array<float, kFFT> window {}, scratch {};
    std::array<std::complex<float>, kFFT> fbuf {};
    std::array<float, kBins> specIn {}, specOut {};
    std::vector<float> ptsIn, ptsOut, tmp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqDisplay)
};

//==============================================================================
// Informacion (detras del engranaje): latencia, tamano de FFT y ayuda rapida.
class SettingsPanel : public juce::Component, private juce::Timer
{
public:
    explicit SettingsPanel (BobaAudioProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xf2191a1d));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r, 10.0f, 1.0f);

        g.setColour (colours::dim);
        g.setFont (font (10.0f, true).withExtraKerningFactor (0.14f));
        g.drawText ("INFO", 14, 12, 150, 16, juce::Justification::left);

        const double fs = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        const int lat = proc.getLatencySamples();
        g.setFont (font (11.0f));
        g.setColour (colours::text);
        g.drawText ("Latency: " + juce::String (lat) + " samples (" + juce::String (1000.0 * lat / fs, 1) + " ms)",
                    14, 36, getWidth() - 28, 16, juce::Justification::left);
        g.drawText ("FFT " + juce::String (proc.getFftSize()) + "  -  overlap " + juce::String (proc.getOverlap()) + "x",
                    14, 56, getWidth() - 28, 16, juce::Justification::left);

        g.setColour (colours::dim);
        g.setFont (font (10.5f));
        g.drawFittedText ("Double-click a knob to reset it.\nDrag the red / green markers in the display to set the processed range.\n"
                          "Quality up = finer resolution, more latency. LOW LAT = ~10 ms.",
                          juce::Rectangle<int> (14, 82, getWidth() - 28, 70), juce::Justification::topLeft, 5);
    }

    void visibilityChanged() override { if (isVisible()) startTimerHz (4); else stopTimer(); }

private:
    void timerCallback() override { repaint(); }
    BobaAudioProcessor& proc;
};

//==============================================================================
// Todo en coordenadas logicas 900 x 590; el editor lo escala.
class Content : public juce::Component, private juce::Timer
{
public:
    explicit Content (BobaAudioProcessor& p)
        : proc (p),
          depth ("Depth", fmt1), detail ("Detail", fmt1), attack ("Attack", fmtMs), release ("Release", fmtMs),
          threshold ("Thresh", fmtDb), link ("Link", fmtPct), focus ("Focus", fmtSigned),
          wetTrim ("Wet Trim", fmtDb), mix ("Mix", fmtPct), out ("Out", fmtDb),
          tiltDetail ("Detail", fmtSigned), tiltAttack ("Attack", fmtSigned), tiltRelease ("Release", fmtSigned),
          stereo ("L/R", "M/S"), sidechain ("INT", "EXT"), sharp ("SOFT", "HARD"),
          listen ("LISTEN"), delta ("DELTA"), even ("EVEN", false), bypass ("BYPASS"), lowLat ("LOW LAT"),
          abA ("A", false), abB ("B", false), abCopy ("A>B", false), prevBtn ("<", false), nextBtn (">", false),
          eq (p), settings (p)
    {
        setLookAndFeel (&laf);
        art = juce::ImageCache::getFromMemory (BinaryData::boba_art_jpg, BinaryData::boba_art_jpgSize);

        focus.setBipolar (true);
        tiltDetail.setBipolar (true); tiltAttack.setBipolar (true); tiltRelease.setBipolar (true);

        for (auto* c : std::initializer_list<juce::Component*> { &depth, &detail, &attack, &release, &threshold, &link, &focus,
                                                                 &wetTrim, &mix, &out, &tiltDetail, &tiltAttack, &tiltRelease,
                                                                 &stereo, &sidechain, &sharp, &listen, &delta, &even, &bypass, &lowLat,
                                                                 &abA, &abB, &abCopy, &prevBtn, &nextBtn, &presetBox,
                                                                 &maxCutBox, &qualityBox, &engineBox, &gear, &eq })
            addAndMakeVisible (*c);
        addChildComponent (settings);

        auto bindS = [&] (juce::Slider& s, const char* id) { sliderAtts.push_back (std::make_unique<SA> (p.apvts, id, s)); };
        auto bindB = [&] (juce::Button& b, const char* id) { buttonAtts.push_back (std::make_unique<BA> (p.apvts, id, b)); };
        auto bindC = [&] (juce::ComboBox& b, const char* id)
        {
            if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id))) b.addItemList (cp->choices, 1);
            comboAtts.push_back (std::make_unique<CA> (p.apvts, id, b));
        };

        bindS (depth, "depth");       bindS (detail, "detail");     bindS (attack, "attack");   bindS (release, "release");
        bindS (threshold, "threshold"); bindS (link, "link");       bindS (focus, "focus");
        bindS (wetTrim, "wetTrim");   bindS (mix, "mix");          bindS (out, "out");
        bindS (tiltDetail, "tiltDetail"); bindS (tiltAttack, "tiltAttack"); bindS (tiltRelease, "tiltRelease");
        bindB (stereo, "stereoMode"); bindB (sidechain, "sidechain"); bindB (listen, "solo"); bindB (delta, "delta");
        bindB (bypass, "bypass");     bindB (lowLat, "lowLatency");
        bindC (maxCutBox, "maxCut");  bindC (qualityBox, "quality"); bindC (engineBox, "engine");

        // sharpness is continuous (soft ... hard); the SOFT/HARD selector sets the two usual values
        sharpAtt = std::make_unique<SA> (p.apvts, "sharpness", sharpHidden);
        sharp.onClick = [this] { sharpHidden.setValue (sharp.getToggleState() ? 0.85 : 0.25, juce::sendNotificationSync); };

        for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
        presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
        prevBtn.onClick = [this] { stepPreset (-1); };
        nextBtn.onClick = [this] { stepPreset (+1); };

        abA.onClick = [this] { if (proc.isSlotB()) proc.switchSlot(); };
        abB.onClick = [this] { if (! proc.isSlotB()) proc.switchSlot(); };
        abCopy.onClick = [this] { proc.copyToOtherSlot(); };

        even.onClick = [this]
        {
            if (auto* fp = proc.apvts.getParameter ("focus"))
            {
                fp->beginChangeGesture();
                fp->setValueNotifyingHost (fp->getDefaultValue());
                fp->endChangeGesture();
            }
        };
        gear.onClick = [this] { settings.setVisible (gear.getToggleState()); settings.toFront (false); };

        startTimerHz (8);
        timerCallback();
    }

    ~Content() override { stopTimer(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient (colours::bg1, 0.0f, 0.0f, colours::bg2, 0.0f, kHeight, false));
        g.fillAll();

        // logo + nombre
        if (art.isValid())
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (20.0f, 10.0f, 32.0f, 32.0f, 8.0f);
            g.reduceClipRegion (clip, juce::AffineTransform());
            const int side = std::min (art.getWidth(), art.getHeight());
            g.drawImage (art, 20, 10, 32, 32, (art.getWidth() - side) / 2, (art.getHeight() - side) / 2, side, side);
        }
        g.setColour (colours::text);
        g.setFont (font (20.0f, true).withExtraKerningFactor (0.3f));
        g.drawText ("BOBA", 62, 7, 160, 24, juce::Justification::left);
        g.setColour (colours::dim);
        g.setFont (font (9.5f, true).withExtraKerningFactor (0.18f));
        g.drawText ("SPECTRAL RESONANCE SUPPRESSOR", 62, 30, 232, 14, juce::Justification::left);

        // paneles
        for (const auto& panel : { juce::Rectangle<float> (20.0f, 336.0f, 860.0f, 124.0f), juce::Rectangle<float> (20.0f, 468.0f, 860.0f, 104.0f) })
        {
            g.setColour (colours::panel);
            g.fillRoundedRectangle (panel, 12.0f);
            g.setColour (colours::panelEdge);
            g.drawRoundedRectangle (panel.reduced (0.5f), 12.0f, 1.0f);
        }
        g.setColour (colours::panelEdge);
        for (float x : { 352.0f, 442.0f, 610.0f }) g.drawLine (x, 352.0f, x, 444.0f, 1.0f);
        for (float x : { 161.0f, 295.0f, 509.0f, 643.0f }) g.drawLine (x, 484.0f, x, 556.0f, 1.0f);

        g.setColour (colours::dim);
        g.setFont (font (9.5f, true).withExtraKerningFactor (0.14f));
        g.drawText ("STEREO",    34, 474, 118, 13, juce::Justification::left);
        g.drawText ("SIDE CHAIN", 168, 474, 118, 13, juce::Justification::left);
        g.drawText ("TILT",      302, 474, 118, 13, juce::Justification::left);
        g.drawText ("SHARPNESS", 516, 474, 118, 13, juce::Justification::left);
        g.drawText ("MAX CUT",   650, 474, 104, 13, juce::Justification::left);
        g.drawText ("QUALITY",   766, 474, 104, 13, juce::Justification::left);
    }

    void resized() override
    {
        // cabecera
        prevBtn.setBounds (300, 12, 26, 26);   presetBox.setBounds (330, 12, 170, 26);   nextBtn.setBounds (504, 12, 26, 26);
        abA.setBounds (548, 12, 30, 26);       abB.setBounds (582, 12, 30, 26);          abCopy.setBounds (616, 12, 44, 26);
        bypass.setBounds (676, 12, 80, 26);
        gear.setBounds (848, 12, 28, 28);

        // display
        eq.setBounds (20, 56, 860, 270);
        settings.setBounds (640, 64, 232, 150);

        // panel 1: perillas
        const int kw = 78, ky = 342, kh = 112;
        depth.setBounds (34, ky, kw, kh);   detail.setBounds (112, ky, kw, kh);  attack.setBounds (190, ky, kw, kh);  release.setBounds (268, ky, kw, kh);
        threshold.setBounds (358, ky, kw, kh);
        link.setBounds (448, ky, kw, kh);   focus.setBounds (526, ky, kw, kh);
        wetTrim.setBounds (616, ky, kw, kh); mix.setBounds (694, ky, kw, kh);   out.setBounds (772, ky, kw, kh);

        // panel 2
        stereo.setBounds (34, 490, 120, 26);     even.setBounds (34, 524, 120, 26);
        sidechain.setBounds (168, 490, 120, 26); listen.setBounds (168, 524, 120, 26);
        tiltDetail.setBounds (302, 486, 66, 82); tiltAttack.setBounds (368, 486, 66, 82); tiltRelease.setBounds (434, 486, 66, 82);
        sharp.setBounds (516, 490, 120, 26);     delta.setBounds (516, 524, 120, 26);
        maxCutBox.setBounds (650, 490, 104, 26); qualityBox.setBounds (766, 490, 104, 26);
        engineBox.setBounds (650, 524, 104, 26); lowLat.setBounds (766, 524, 104, 26);
    }

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void stepPreset (int d)
    {
        const int n = proc.getNumPrograms();
        const int next = (presetBox.getSelectedId() - 1 + d + n) % n;
        presetBox.setSelectedId (next + 1, juce::sendNotification);
    }

    void timerCallback() override
    {
        const bool b = proc.isSlotB();
        abA.setToggleState (! b, juce::dontSendNotification);
        abB.setToggleState (b, juce::dontSendNotification);

        const bool comp = proc.apvts.getRawParameterValue ("engine")->load() > 0.5f;
        threshold.setEnabled (comp);   threshold.setAlpha (comp ? 1.0f : 0.35f);
        for (auto* k : { &detail, &tiltDetail }) { k->setEnabled (! comp); k->setAlpha (comp ? 0.35f : 1.0f); }

        const bool hard = proc.apvts.getRawParameterValue ("sharpness")->load() >= 0.5f;
        if (sharp.getToggleState() != hard) sharp.setToggleState (hard, juce::dontSendNotification);

        const int prog = proc.getCurrentProgram();
        if (presetBox.getSelectedId() != prog + 1) presetBox.setSelectedId (prog + 1, juce::dontSendNotification);
    }

    BobaAudioProcessor& proc;
    BobaLookAndFeel laf;
    juce::Image art;

    Knob depth, detail, attack, release, threshold, link, focus, wetTrim, mix, out, tiltDetail, tiltAttack, tiltRelease;
    SegToggle stereo, sidechain, sharp;
    TextToggle listen, delta, even, bypass, lowLat, abA, abB, abCopy, prevBtn, nextBtn;
    juce::ComboBox presetBox, maxCutBox, qualityBox, engineBox;
    juce::Slider sharpHidden;                  // enlaza el parametro continuo "sharpness" con el selector SOFT/HARD
    GearButton gear;
    EqDisplay eq;
    SettingsPanel settings;

    // los attachments van despues de los componentes: se destruyen antes que ellos
    std::vector<std::unique_ptr<SA>> sliderAtts;
    std::vector<std::unique_ptr<BA>> buttonAtts;
    std::vector<std::unique_ptr<CA>> comboAtts;
    std::unique_ptr<SA> sharpAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
};

} // namespace bobaui

//==============================================================================
class BobaEditor : public juce::AudioProcessorEditor
{
public:
    explicit BobaEditor (BobaAudioProcessor& p) : juce::AudioProcessorEditor (p), content (p)
    {
        addAndMakeVisible (content);
        setResizeLimits (600, (int) (600.0f * bobaui::kHeight / bobaui::kWidth), 1800, (int) (1800.0f * bobaui::kHeight / bobaui::kWidth));
        getConstrainer()->setFixedAspectRatio ((double) bobaui::kWidth / (double) bobaui::kHeight);
        setResizable (true, true);
        setSize ((int) bobaui::kWidth, (int) bobaui::kHeight);
    }

    void paint (juce::Graphics& g) override { g.fillAll (bobaui::colours::bg2); }

    void resized() override
    {
        const float s = (float) getWidth() / bobaui::kWidth;
        content.setTransform (juce::AffineTransform::scale (s));
        content.setBounds (0, 0, (int) bobaui::kWidth, (int) bobaui::kHeight);
    }

private:
    bobaui::Content content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BobaEditor)
};
