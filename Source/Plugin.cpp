#include <JuceHeader.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

static const char* kAltID = "altitude";
static constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

static inline float smooth01 (float x)
{
    x = juce::jlimit (0.f, 1.f, x);
    return x * x * (3.f - 2.f * x);
}

//==============================================================================
// Октавный питч-шифтер (два плавающих "зерна")
//==============================================================================
struct PitchUp
{
    static constexpr int size = 8192;
    static constexpr float win = 2048.f;

    std::vector<float> buf;
    int w = 0;
    float ph = 0.f;

    void reset() { buf.assign ((size_t) size, 0.f); w = 0; ph = 0.f; }

    float tap (float delay) const
    {
        float rp = (float) w - delay;
        while (rp < 0.f) rp += (float) size;
        int i0 = (int) rp;
        const float fr = rp - (float) i0;
        const int i1 = (i0 + 1) & (size - 1);
        i0 &= (size - 1);
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * fr;
    }

    float process (float x, float ratio)
    {
        buf[(size_t) w] = x;
        ph += (ratio - 1.f) / win;
        if (ph >= 1.f) ph -= 1.f;
        const float p2 = (ph + 0.5f >= 1.f) ? ph - 0.5f : ph + 0.5f;
        const float g1 = 0.5f - 0.5f * std::cos (kTwoPi * ph);
        const float g2 = 0.5f - 0.5f * std::cos (kTwoPi * p2);
        const float y = tap (win * (1.f - ph)) * g1 + tap (win * (1.f - p2)) * g2;
        w = (w + 1) & (size - 1);
        return y;
    }
};

//==============================================================================
// Звук
//==============================================================================
class AscendProcessor : public juce::AudioProcessor
{
public:
    AscendProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "STATE", createLayout())
    {
        shift[0].reset();
        shift[1].reset();
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kAltID, 1 }, "Altitude",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f));
        return layout;
    }

    const juce::String getName() const override { return "Ascend"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void prepareToPlay (double sampleRate, int) override
    {
        sr = sampleRate;
        reverb.setSampleRate (sampleRate);
        reverb.reset();
        shift[0].reset();
        shift[1].reset();
        altSmooth.reset (sampleRate, 0.05);
        altSmooth.setCurrentAndTargetValue (apvts.getRawParameterValue (kAltID)->load());
        for (int c = 0; c < 2; ++c) { hp[c] = 0.f; fb[c] = 0.f; }
        lfo = 0.f;
    }
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        auto out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
            return false;
        return out == layouts.getMainInputChannelSet();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const int n = buffer.getNumSamples();
        const int chs = juce::jmin (buffer.getNumChannels(), 2);
        if (chs == 0) return;
        for (int c = chs; c < buffer.getNumChannels(); ++c)
            buffer.clear (c, 0, n);

        altSmooth.setTargetValue (apvts.getRawParameterValue (kAltID)->load());

        const float aNow = altSmooth.getCurrentValue();
        juce::Reverb::Parameters rp;
        rp.roomSize   = 0.55f + 0.41f * aNow;
        rp.damping    = 0.55f - 0.40f * aNow;
        rp.wetLevel   = 0.33f;
        rp.dryLevel   = 0.0f;
        rp.width      = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        float* d0 = buffer.getWritePointer (0);
        float* d1 = chs > 1 ? buffer.getWritePointer (1) : nullptr;
        float peak = 0.f;

        for (int i = 0; i < n; ++i)
        {
            const float a      = altSmooth.getNextValue();
            const float wetAmt = 0.95f * std::pow (a, 0.65f);
            const float shAmt  = 0.45f * smooth01 ((a - 0.08f) / 0.7f);
            const float drive  = 1.f + 2.5f * a * a;
            const float hpCut  = 60.f + 700.f * a;
            const float hpA    = 1.f - std::exp (-kTwoPi * hpCut / (float) sr);

            lfo += kTwoPi * 0.35f / (float) sr;
            if (lfo > kTwoPi) lfo -= kTwoPi;
            const float ratio = 2.f + 0.012f * a * std::sin (lfo);

            const float in0 = d0[i];
            const float in1 = d1 != nullptr ? d1[i] : in0;

            float w[2] = { in0 * 0.8f + fb[0], in1 * 0.8f + fb[1] };
            reverb.processStereo (&w[0], &w[1], 1);

            float wetOut[2];
            for (int c = 0; c < 2; ++c)
            {
                hp[c] += hpA * (w[c] - hp[c]);
                const float x = w[c] - hp[c];
                const float s = std::tanh (x * drive) / std::sqrt (drive);
                wetOut[c] = s;
                const float sh = shift[c].process (s * 0.35f, ratio);
                fb[c] = std::tanh (sh) * shAmt;
            }

            const float dryG = 1.f - 0.45f * wetAmt;
            const float o0 = juce::jlimit (-2.f, 2.f, in0 * dryG + wetOut[0] * wetAmt * 0.7f);
            d0[i] = o0;
            peak = juce::jmax (peak, std::abs (o0));
            if (d1 != nullptr)
            {
                const float o1 = juce::jlimit (-2.f, 2.f, in1 * dryG + wetOut[1] * wetAmt * 0.7f);
                d1[i] = o1;
                peak = juce::jmax (peak, std::abs (o1));
            }
        }
        level.store (peak);
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> level { 0.f };

private:
    double sr = 44100.0;
    juce::Reverb reverb;
    PitchUp shift[2];
    juce::SmoothedValue<float> altSmooth;
    float hp[2] {}, fb[2] {};
    float lfo = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AscendProcessor)
};

//==============================================================================
// Цвета неба
//==============================================================================
static juce::Colour skyTop (float a)
{
    static const juce::Colour k[5] = { juce::Colour (0xff3a6fb5), juce::Colour (0xff2457a8),
                                       juce::Colour (0xff0d1f5c), juce::Colour (0xff050a24),
                                       juce::Colour (0xff000008) };
    const float x = juce::jlimit (0.f, 1.f, a) * 4.f;
    const int i = juce::jlimit (0, 3, (int) x);
    return k[i].interpolatedWith (k[i + 1], x - (float) i);
}

static juce::Colour skyBot (float a)
{
    static const juce::Colour k[5] = { juce::Colour (0xffffb98a), juce::Colour (0xff8ec5ff),
                                       juce::Colour (0xff3d7bd8), juce::Colour (0xff1a2f78),
                                       juce::Colour (0xff2a1250) };
    const float x = juce::jlimit (0.f, 1.f, a) * 4.f;
    const int i = juce::jlimit (0, 3, (int) x);
    return k[i].interpolatedWith (k[i + 1], x - (float) i);
}

static juce::Colour orbColour (float a)
{
    const juce::Colour warm (0xffffe1b0), mid (0xffbfe8ff), hi (0xffd8b4ff);
    return a < 0.5f ? warm.interpolatedWith (mid, a * 2.f)
                    : mid.interpolatedWith (hi, (a - 0.5f) * 2.f);
}

//==============================================================================
// Ручка
//==============================================================================
class AscendLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startA, float endA, juce::Slider&) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.f);
        auto c = b.getCentre();
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;

        const juce::Colour col = juce::Colour (0xff7fd8ff).interpolatedWith (juce::Colour (0xffd9a8ff), pos);

        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.30f * pos), c.x, c.y,
                                                 col.withAlpha (0.f), c.x, c.y - r * 1.8f, true));
        g.fillEllipse (c.x - r * 1.8f, c.y - r * 1.8f, r * 3.6f, r * 3.6f);

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillEllipse (c.x - r, c.y - r, r * 2.f, r * 2.f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.f, r * 2.f, 1.5f);

        const float ar = r * 0.78f;
        juce::Path track;
        track.addCentredArc (c.x, c.y, ar, ar, 0.f, startA, endA, true);
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.strokePath (track, juce::PathStrokeType (4.f));

        const float a = startA + pos * (endA - startA);
        juce::Path val;
        val.addCentredArc (c.x, c.y, ar, ar, 0.f, startA, a, true);
        g.setColour (col.withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (10.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (val, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float tx = c.x + std::sin (a) * ar;
        const float ty = c.y - std::cos (a) * ar;
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, tx, ty, col.withAlpha (0.f), tx + 11.f, ty, true));
        g.fillEllipse (tx - 11.f, ty - 11.f, 22.f, 22.f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (tx - 3.f, ty - 3.f, 6.f, 6.f);

        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.fillEllipse (c.x - 3.f, c.y - 3.f, 6.f, 6.f);
    }
};

//==============================================================================
// Окно: подъём в космос
//==============================================================================
class AscendEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AscendEditor (AscendProcessor& p) : AudioProcessorEditor (&p), proc (p)
    {
        setSize ((int) kW, (int) kH);

        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                  juce::MathConstants<float>::pi * 2.8f, true);
        knob.setDoubleClickReturnValue (true, 0.0);
        knob.setLookAndFeel (&look);
        addAndMakeVisible (knob);
        attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, kAltID, knob);

        juce::Random r (7);
        for (int i = 0; i < 150; ++i)
            stars.push_back ({ r.nextFloat() * kW, r.nextFloat() * kH, 0.2f + r.nextFloat() * 0.8f, r.nextFloat() * 6.28f });

        for (int i = 0; i < 14; ++i)
        {
            Cloud c;
            c.x = r.nextFloat() * kW;
            c.y = r.nextFloat() * kH;
            c.s = 0.6f + r.nextFloat() * 0.9f;
            c.z = 0.3f + r.nextFloat() * 0.7f;
            for (int k = 0; k < 5; ++k)
            {
                c.ox[k] = ((float) k - 2.f) * 38.f + (r.nextFloat() - 0.5f) * 20.f;
                c.oy[k] = (r.nextFloat() - 0.5f) * 20.f;
                c.r[k]  = 48.f + r.nextFloat() * 30.f;
            }
            clouds.push_back (c);
        }

        av = (float) knob.getValue();
        startTimerHz (60);
    }

    ~AscendEditor() override
    {
        stopTimer();
        knob.setLookAndFeel (nullptr);
    }

    void resized() override { knob.setBounds ((int) (kW * 0.5f) - 55, 296, 110, 110); }

    //----------------------------------------------------------------------
    void timerCallback() override
    {
        const float dt = 1.f / 60.f;
        t += dt;
        av += ((float) knob.getValue() - av) * 0.12f;
        lv += (proc.level.load() - lv) * 0.2f;

        speed = 0.12f + 13.f * av * av;

        for (auto& s : stars)
        {
            s.y += speed * s.z + 0.05f;
            s.ph += 0.04f + 0.03f * s.z;
            if (s.y > kH + 40.f) { s.y = -40.f; s.x = rnd.nextFloat() * kW; }
        }

        for (auto& c : clouds)
        {
            c.y += (speed * 1.6f + 0.15f) * c.z;
            c.x += 0.1f * c.z;
            if (c.y > kH + 120.f * c.s) { c.y = -120.f * c.s; c.x = rnd.nextFloat() * kW; }
            if (c.x > kW + 200.f * c.s) c.x = -200.f * c.s;
        }

        const float ox = kW * 0.5f;
        const float oy = 140.f + std::sin (t * 0.8f) * 4.f;
        for (int i = 0; i < 2; ++i)
            dots.push_back ({ ox + (rnd.nextFloat() - 0.5f) * 8.f, oy + 8.f,
                              (rnd.nextFloat() - 0.5f) * 0.4f, 1.f + speed * 0.35f + rnd.nextFloat(),
                              0.f, 40.f + rnd.nextFloat() * 40.f, 1.5f + rnd.nextFloat() * 2.5f });
        for (auto& d : dots) { d.life += 1.f; d.x += d.vx; d.y += d.vy; }
        dots.erase (std::remove_if (dots.begin(), dots.end(), [] (const Dot& d) { return d.life >= d.maxLife; }), dots.end());

        ringTimer += dt;
        const float interval = 0.9f - 0.5f * juce::jmin (1.f, lv * 3.f);
        if (ringTimer > interval) { ringTimer = 0.f; rings.push_back ({ 18.f, 0.45f }); }
        for (auto& r : rings) { r.r += 0.9f + 1.5f * av + lv * 3.f; r.a -= 0.0045f; }
        rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.a <= 0.f; }), rings.end());

        repaint();
    }

    //----------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const float a = av;
        const float W = kW, H = kH;
        const float ox = W * 0.5f;
        const float oy = 140.f + std::sin (t * 0.8f) * 4.f;
        const juce::Colour oc = orbColour (a);

        // небо
        g.setGradientFill (juce::ColourGradient (skyTop (a), 0.f, 0.f, skyBot (a), 0.f, H, false));
        g.fillAll();

        // рассветное солнце у горизонта
        const float sg = 1.f - smooth01 (a / 0.45f);
        if (sg > 0.01f)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc27a).withAlpha (0.75f * sg), W * 0.5f, H + 30.f,
                                                     juce::Colour (0xffffc27a).withAlpha (0.f), W * 0.5f, H + 30.f - 430.f, true));
            g.fillEllipse (W * 0.5f - 430.f, H + 30.f - 430.f, 860.f, 860.f);
        }

        // звёзды
        const float sa = smooth01 ((a - 0.28f) / 0.4f);
        if (sa > 0.01f)
        {
            for (auto& s : stars)
            {
                const float tw = 0.6f + 0.4f * std::sin (s.ph);
                const float al = sa * tw * (0.35f + 0.65f * s.z);
                const float len = speed * 2.2f * s.z;
                g.setColour (juce::Colour (0xffdfe9ff).withAlpha (juce::jlimit (0.f, 1.f, al)));
                if (len > 2.f)
                    g.drawLine (s.x, s.y - len, s.x, s.y, 0.8f + s.z);
                else
                    g.fillEllipse (s.x - s.z, s.y - s.z, s.z * 2.f, s.z * 2.f);
            }
        }

        // северное сияние
        const float vis = smooth01 ((a - 0.55f) / 0.35f);
        if (vis > 0.01f)
        {
            const juce::Colour cols[3] = { juce::Colour (0xff5dffb0), juce::Colour (0xff8f7bff), juce::Colour (0xff5ad7ff) };
            for (int b = 0; b < 3; ++b)
            {
                juce::Path p;
                const float base = 100.f + 55.f * (float) b;
                for (float x = 0.f; x <= W + 16.f; x += 16.f)
                {
                    const float y = base + 24.f * std::sin (x * 0.012f + t * 0.5f + (float) b * 2.f)
                                         + 14.f * std::sin (x * 0.027f - t * 0.8f + (float) b);
                    if (x == 0.f) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                for (float wd = 44.f; wd >= 8.f; wd -= 12.f)
                {
                    g.setColour (cols[b].withAlpha (vis * 0.045f));
                    g.strokePath (p, juce::PathStrokeType (wd, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }
        }

        // планета
        const float pv = smooth01 ((a - 0.42f) / 0.35f);
        if (pv > 0.01f)
        {
            const float R = 1300.f - 700.f * pv;
            const float edgeY = H - 55.f * pv;
            const float cx = W * 0.5f, cy = edgeY + R;
            const float rr = R + 60.f;
            const juce::Colour atmo (0xff4fb4ff);

            juce::ColourGradient ag (atmo.withAlpha (0.f), cx, cy, atmo.withAlpha (0.f), cx, cy - rr, true);
            ag.addColour ((double) ((R - 8.f) / rr), atmo.withAlpha (0.f));
            ag.addColour ((double) (R / rr), atmo.withAlpha (0.9f * pv));
            g.setGradientFill (ag);
            g.fillEllipse (cx - rr, cy - rr, rr * 2.f, rr * 2.f);

            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1f63b8), 0.f, edgeY,
                                                     juce::Colour (0xff04122e), 0.f, H, false));
            g.fillEllipse (cx - R, cy - R, R * 2.f, R * 2.f);

            g.setColour (juce::Colour (0xffbfe6ff).withAlpha (0.55f * pv));
            g.drawEllipse (cx - R, cy - R, R * 2.f, R * 2.f, 1.5f);
        }

        // облака
        const float ca = 1.f - smooth01 (a / 0.6f);
        if (ca > 0.01f)
        {
            const juce::Colour cc = juce::Colours::white.interpolatedWith (skyBot (a), 0.25f);
            for (auto& c : clouds)
            {
                const float la = ca * (0.25f + 0.5f * c.z);
                for (int k = 0; k < 5; ++k)
                {
                    const float px = c.x + c.ox[k] * c.s;
                    const float py = c.y + c.oy[k] * c.s;
                    const float pr = c.r[k] * c.s;
                    g.setGradientFill (juce::ColourGradient (cc.withAlpha (la), px, py, cc.withAlpha (0.f), px + pr, py, true));
                    g.fillEllipse (px - pr, py - pr, pr * 2.f, pr * 2.f);
                }
            }
        }

        // кольца реверберации
        for (auto& r : rings)
        {
            g.setColour (oc.withAlpha (juce::jlimit (0.f, 1.f, r.a)));
            g.drawEllipse (ox - r.r, oy - r.r, r.r * 2.f, r.r * 2.f, 1.5f);
        }

        // шлейф
        for (auto& d : dots)
        {
            const float k = d.life / d.maxLife;
            g.setColour (oc.withAlpha ((1.f - k) * 0.8f));
            const float s = d.size * (1.f - k * 0.5f);
            g.fillEllipse (d.x - s * 0.5f, d.y - s * 0.5f, s, s);
        }

        // лучи
        {
            const float L = 55.f + 40.f * lv + 25.f * a;
            for (int i = 0; i < 10; ++i)
            {
                const float ang = t * 0.15f + (float) i * kTwoPi / 10.f;
                const float dx = std::cos (ang), dy = std::sin (ang);
                g.setColour (oc.withAlpha (0.06f));
                g.drawLine (ox + dx * 20.f, oy + dy * 20.f, ox + dx * L, oy + dy * L, 5.f);
                g.setColour (oc.withAlpha (0.14f));
                g.drawLine (ox + dx * 20.f, oy + dy * 20.f, ox + dx * L, oy + dy * L, 1.2f);
            }
        }

        // шар
        {
            const float haloR = 90.f + 40.f * lv;
            g.setGradientFill (juce::ColourGradient (oc.withAlpha (0.45f), ox, oy, oc.withAlpha (0.f), ox + haloR, oy, true));
            g.fillEllipse (ox - haloR, oy - haloR, haloR * 2.f, haloR * 2.f);

            const float coreR = 18.f + 8.f * lv;
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, ox, oy, oc.withAlpha (0.f), ox + coreR * 2.f, oy, true));
            g.fillEllipse (ox - coreR * 2.f, oy - coreR * 2.f, coreR * 4.f, coreR * 4.f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (ox - coreR * 0.6f, oy - coreR * 0.6f, coreR * 1.2f, coreR * 1.2f);
        }

        // стеклянная панель
        juce::Rectangle<float> panel (W * 0.5f - 130.f, 226.f, 260.f, 216.f);
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRoundedRectangle (panel, 26.f);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawRoundedRectangle (panel, 26.f, 1.2f);

        // текст
        auto txt = [&] (const juce::String& s, juce::Rectangle<int> r, float size, float kern, bool bold, juce::Colour c)
        {
            juce::Font f (size, bold ? juce::Font::bold : juce::Font::plain);
            f.setExtraKerningFactor (kern);
            g.setFont (f);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawText (s, r.translated (0, 1), juce::Justification::centred);
            g.setColour (c);
            g.drawText (s, r, juce::Justification::centred);
        };

        const int Wi = (int) W;
        txt ("ASCEND", { 0, 20, Wi, 30 }, 24.f, 0.45f, false, juce::Colours::white.withAlpha (0.92f));

        const float km = a * 130.f;
        const char* stage = a < 0.01f ? "GROUND LEVEL" : km < 12.f ? "TROPOSPHERE" : km < 50.f ? "STRATOSPHERE"
                          : km < 85.f ? "MESOSPHERE" : km < 100.f ? "THERMOSPHERE" : "OUTER SPACE";
        txt (juce::String (km, 1) + " KM", { 0, 234, Wi, 36 }, 30.f, 0.15f, false, juce::Colours::white);
        txt (stage, { 0, 270, Wi, 18 }, 12.f, 0.35f, true, oc);
        txt ("ALTITUDE", { 0, 412, Wi, 18 }, 11.f, 0.4f, false, juce::Colours::white.withAlpha (0.65f));

        // виньетка
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, W * 0.5f, H * 0.5f,
                                                 juce::Colours::black.withAlpha (0.4f), 0.f, 0.f, true));
        g.fillAll();
    }

private:
    struct Star  { float x, y, z, ph; };
    struct Cloud { float x, y, s, z; float ox[5], oy[5], r[5]; };
    struct Dot   { float x, y, vx, vy, life, maxLife, size; };
    struct Ring  { float r, a; };

    static constexpr float kW = 640.f;
    static constexpr float kH = 460.f;

    AscendProcessor& proc;
    AscendLook look;
    juce::Slider knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;

    std::vector<Star> stars;
    std::vector<Cloud> clouds;
    std::vector<Dot> dots;
    std::vector<Ring> rings;
    juce::Random rnd;

    float av = 0.f, lv = 0.f, t = 0.f, speed = 0.f, ringTimer = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AscendEditor)
};

juce::AudioProcessorEditor* AscendProcessor::createEditor() { return new AscendEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AscendProcessor(); }
