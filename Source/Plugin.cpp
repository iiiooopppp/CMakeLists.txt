#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <vector>

static constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
static constexpr float kPi    = juce::MathConstants<float>::pi;

static inline float smooth01 (float x)
{
    x = juce::jlimit (0.f, 1.f, x);
    return x * x * (3.f - 2.f * x);
}

//==============================================================================
// Параметры
//==============================================================================
struct ParamDef { const char* id; const char* name; float lo, hi, def, skew, step; bool choice; };

static const ParamDef kParams[] =
{
    { "wave1",   "Osc A",         0.f,    3.f,     0.f,    1.f,   1.f, true  },
    { "wave2",   "Osc B",         0.f,    3.f,     0.f,    1.f,   1.f, true  },
    { "mixb",    "Osc B Level",   0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "detune",  "Detune",        0.f,    1.f,     0.15f,  1.f,   0.f, false },
    { "fmamt",   "FM",            0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "fmratio", "FM Ratio",      0.5f,   8.f,     2.f,    1.f,   0.f, false },
    { "sub",     "Sub",           0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "noise",   "Noise",         0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "cutoff",  "Cutoff",        30.f,   18000.f, 8000.f, 0.3f,  0.f, false },
    { "res",     "Resonance",     0.f,    1.f,     0.1f,   1.f,   0.f, false },
    { "fenv",    "Filter Env",    0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "fdecay",  "Filter Decay",  0.02f,  3.f,     0.3f,   0.5f,  0.f, false },
    { "attack",  "Attack",        0.001f, 4.f,     0.005f, 0.35f, 0.f, false },
    { "decay",   "Decay",         0.02f,  5.f,     0.8f,   0.4f,  0.f, false },
    { "sustain", "Sustain",       0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "release", "Release",       0.01f,  6.f,     0.4f,   0.4f,  0.f, false },
    { "oct",     "Octave",       -2.f,    2.f,     0.f,    1.f,   1.f, false },
    { "vol",     "Volume",        0.f,    1.f,     0.7f,   1.f,   0.f, false },
    { "dmix",    "Echo",          0.f,    1.f,     0.2f,   1.f,   0.f, false },
    { "dtime",   "Echo Time",     0.05f,  0.8f,    0.35f,  1.f,   0.f, false },
    { "dfb",     "Echo Feedback", 0.f,    0.9f,    0.35f,  1.f,   0.f, false },
    { "rmix",    "Space",         0.f,    1.f,     0.25f,  1.f,   0.f, false },
    { "rsize",   "Space Size",    0.f,    1.f,     0.6f,   1.f,   0.f, false },
};

enum ParamIndex
{
    WAVE1, WAVE2, MIXB, DETUNE, FMAMT, FMRATIO, SUB, NOISE, CUTOFF, RES, FENV, FDECAY,
    ATTACK, DECAY, SUSTAIN, RELEASE, OCT, VOL, DMIX, DTIME, DFB, RMIX, RSIZE, NUM_PARAMS
};

struct VoiceParams
{
    int   wave1 = 0, wave2 = 0;
    float mixb = 0.f, detune = 0.f, fmamt = 0.f, fmratio = 2.f, sub = 0.f, noise = 0.f;
    float cutoff = 8000.f, res = 0.1f, fenv = 0.f, fdecay = 0.3f;
    float attack = 0.005f, decay = 0.8f, sustain = 0.f, release = 0.4f, oct = 0.f;
    float dtime = 0.35f, dfb = 0.35f, dmix = 0.2f, rmix = 0.25f, rsize = 0.6f, vol = 0.7f;
};

//==============================================================================
// Пресеты: папки жанров
//==============================================================================
struct PV        { const char* id; float v; };
struct PresetDef { juce::String name; std::vector<PV> v; };
struct GenreDef  { juce::String name; juce::Colour a, b; std::vector<PresetDef> presets; };

static PresetDef P (const char* n, std::initializer_list<PV> l)
{
    PresetDef d;
    d.name = n;
    d.v = std::vector<PV> (l);
    return d;
}

static const std::vector<GenreDef>& genres()
{
    static const std::vector<GenreDef> g =
    {
        { "Futuristic Glo", juce::Colour (0xff5ee7ff), juce::Colour (0xffff6ad5), {
            P ("Glass Bell",       { {"fmamt",.55f}, {"fmratio",3.5f}, {"cutoff",12000.f}, {"fdecay",.8f}, {"decay",1.8f}, {"release",1.2f}, {"oct",1.f}, {"dmix",.25f}, {"rmix",.35f} }),
            P ("Holo Strings",     { {"wave1",2.f}, {"wave2",2.f}, {"mixb",.8f}, {"detune",.35f}, {"cutoff",3500.f}, {"res",.12f}, {"fenv",.2f}, {"fdecay",1.2f}, {"attack",.35f}, {"decay",1.f}, {"sustain",.8f}, {"release",1.2f}, {"rmix",.4f}, {"dmix",.1f} }),
            P ("Neon Pluck",       { {"wave1",2.f}, {"wave2",3.f}, {"mixb",.3f}, {"detune",.15f}, {"cutoff",1200.f}, {"fenv",.7f}, {"fdecay",.22f}, {"decay",.45f}, {"release",.3f}, {"dmix",.3f}, {"dtime",.3f}, {"dfb",.4f} }),
            P ("Zero Gravity Pad", { {"wave1",1.f}, {"wave2",2.f}, {"mixb",.6f}, {"detune",.5f}, {"cutoff",2200.f}, {"attack",1.2f}, {"decay",1.5f}, {"sustain",.9f}, {"release",2.5f}, {"rmix",.55f}, {"rsize",.9f} }),
            P ("Crystal Hit",      { {"fmamt",.8f}, {"fmratio",5.f}, {"noise",.12f}, {"cutoff",9000.f}, {"fdecay",.12f}, {"decay",.35f}, {"release",.4f}, {"oct",1.f}, {"rmix",.3f} }),
            P ("Starlight Keys",   { {"wave1",1.f}, {"fmamt",.3f}, {"fmratio",2.f}, {"mixb",.3f}, {"cutoff",5000.f}, {"fenv",.3f}, {"fdecay",.5f}, {"decay",1.2f}, {"sustain",.1f}, {"release",.8f}, {"dmix",.2f}, {"rmix",.3f} }),
            P ("Comet Lead",       { {"wave1",3.f}, {"wave2",2.f}, {"mixb",.5f}, {"detune",.2f}, {"cutoff",4500.f}, {"res",.25f}, {"fenv",.4f}, {"fdecay",.4f}, {"decay",.3f}, {"sustain",.7f}, {"release",.4f}, {"dmix",.35f}, {"dfb",.45f}, {"rmix",.3f} }),
        } },

        { "Plugg", juce::Colour (0xffffb3e6), juce::Colour (0xff8dffd8), {
            P ("Sugar Bell",    { {"fmamt",.35f}, {"fmratio",2.f}, {"cutoff",8000.f}, {"fdecay",.4f}, {"decay",1.f}, {"release",.7f}, {"oct",1.f}, {"dmix",.3f}, {"dtime",.375f}, {"dfb",.45f}, {"rmix",.3f} }),
            P ("Cloud Harp",    { {"wave1",1.f}, {"wave2",0.f}, {"mixb",.5f}, {"detune",.1f}, {"fmamt",.2f}, {"fmratio",4.f}, {"cutoff",6000.f}, {"fenv",.4f}, {"fdecay",.3f}, {"decay",.9f}, {"release",.6f}, {"dmix",.3f}, {"rmix",.3f} }),
            P ("Soft Mallet",   { {"fmamt",.5f}, {"fmratio",3.f}, {"cutoff",4500.f}, {"fdecay",.15f}, {"decay",.5f}, {"release",.3f}, {"noise",.05f}, {"dmix",.25f}, {"rmix",.25f} }),
            P ("Pastel Keys",   { {"wave1",1.f}, {"wave2",2.f}, {"mixb",.25f}, {"cutoff",2800.f}, {"fenv",.5f}, {"fdecay",.25f}, {"decay",.7f}, {"release",.4f}, {"dmix",.3f}, {"rmix",.3f} }),
            P ("Bubble Pluck",  { {"wave1",3.f}, {"cutoff",900.f}, {"res",.3f}, {"fenv",.8f}, {"fdecay",.15f}, {"decay",.3f}, {"release",.2f}, {"sub",.2f}, {"dmix",.3f} }),
            P ("Candy Strings", { {"wave1",2.f}, {"wave2",2.f}, {"mixb",.7f}, {"detune",.3f}, {"cutoff",3000.f}, {"attack",.2f}, {"decay",1.f}, {"sustain",.7f}, {"release",.9f}, {"rmix",.4f} }),
        } },

        { "Dark Plugg", juce::Colour (0xff9b5cff), juce::Colour (0xffff3a5e), {
            P ("Graveyard Bell", { {"fmamt",.6f}, {"fmratio",3.5f}, {"cutoff",4000.f}, {"fdecay",.6f}, {"decay",2.2f}, {"release",1.5f}, {"rmix",.55f}, {"rsize",.9f}, {"dmix",.25f} }),
            P ("Haunted Choir",  { {"wave1",1.f}, {"wave2",1.f}, {"mixb",.7f}, {"detune",.45f}, {"cutoff",1800.f}, {"res",.15f}, {"attack",.8f}, {"decay",1.f}, {"sustain",.9f}, {"release",2.f}, {"rmix",.6f}, {"rsize",.95f} }),
            P ("Cold Pluck",     { {"wave1",2.f}, {"cutoff",700.f}, {"res",.25f}, {"fenv",.8f}, {"fdecay",.2f}, {"decay",.5f}, {"release",.3f}, {"sub",.3f}, {"dmix",.3f}, {"rmix",.35f} }),
            P ("Shadow Strings", { {"wave1",2.f}, {"wave2",2.f}, {"mixb",.8f}, {"detune",.4f}, {"cutoff",1400.f}, {"attack",.5f}, {"decay",1.2f}, {"sustain",.8f}, {"release",1.6f}, {"rmix",.45f} }),
            P ("Void Hit",       { {"fmamt",.7f}, {"fmratio",1.5f}, {"noise",.25f}, {"sub",.6f}, {"cutoff",2500.f}, {"fenv",.6f}, {"fdecay",.1f}, {"decay",.4f}, {"release",.5f}, {"oct",-1.f}, {"rmix",.5f} }),
            P ("Crimson Keys",   { {"wave1",1.f}, {"fmamt",.25f}, {"fmratio",2.f}, {"cutoff",2000.f}, {"fenv",.4f}, {"fdecay",.4f}, {"decay",.9f}, {"release",.6f}, {"rmix",.4f} }),
        } },

        { "Pluggnb", juce::Colour (0xffff9b5e), juce::Colour (0xffff5e9b), {
            P ("Silk Keys",      { {"fmamt",.3f}, {"fmratio",1.f}, {"wave2",1.f}, {"mixb",.3f}, {"cutoff",6000.f}, {"fenv",.25f}, {"fdecay",.6f}, {"decay",1.6f}, {"sustain",.1f}, {"release",.9f}, {"rmix",.35f} }),
            P ("Love Rhodes",    { {"fmamt",.45f}, {"fmratio",1.f}, {"sub",.15f}, {"cutoff",4500.f}, {"fdecay",.5f}, {"decay",1.8f}, {"sustain",.15f}, {"release",1.f}, {"dmix",.2f}, {"rmix",.3f} }),
            P ("Velvet Pad",     { {"wave1",1.f}, {"wave2",2.f}, {"mixb",.5f}, {"detune",.3f}, {"cutoff",2500.f}, {"attack",1.f}, {"decay",1.5f}, {"sustain",.85f}, {"release",2.2f}, {"rmix",.5f} }),
            P ("Melt Bell",      { {"fmamt",.4f}, {"fmratio",2.f}, {"cutoff",7000.f}, {"fdecay",.5f}, {"decay",1.4f}, {"release",1.1f}, {"oct",1.f}, {"dmix",.3f}, {"rmix",.4f} }),
            P ("Sunset Strings", { {"wave1",2.f}, {"wave2",1.f}, {"mixb",.6f}, {"detune",.3f}, {"cutoff",3200.f}, {"attack",.3f}, {"decay",1.f}, {"sustain",.8f}, {"release",1.3f}, {"rmix",.4f} }),
            P ("Heartbeat Pluck",{ {"wave1",1.f}, {"wave2",3.f}, {"mixb",.2f}, {"cutoff",1800.f}, {"fenv",.6f}, {"fdecay",.25f}, {"decay",.6f}, {"release",.35f}, {"sub",.25f}, {"dmix",.25f} }),
        } },

        { "Rage", juce::Colour (0xffff3b3b), juce::Colour (0xffffd23b), {
            P ("Rage Stab",     { {"wave1",2.f}, {"wave2",2.f}, {"mixb",.9f}, {"detune",.4f}, {"cutoff",3500.f}, {"res",.2f}, {"fenv",.6f}, {"fdecay",.18f}, {"decay",.35f}, {"sustain",.3f}, {"release",.15f}, {"dmix",.15f}, {"rmix",.15f} }),
            P ("Super Lead",    { {"wave1",2.f}, {"wave2",2.f}, {"mixb",1.f}, {"detune",.6f}, {"cutoff",6000.f}, {"res",.15f}, {"decay",.3f}, {"sustain",.85f}, {"release",.3f}, {"dmix",.2f} }),
            P ("Siren Bell",    { {"wave1",3.f}, {"fmamt",.6f}, {"fmratio",7.f}, {"cutoff",8000.f}, {"fdecay",.3f}, {"decay",.7f}, {"release",.3f}, {"oct",1.f}, {"dmix",.2f} }),
            P ("Opium Strings", { {"wave1",2.f}, {"wave2",3.f}, {"mixb",.6f}, {"detune",.35f}, {"cutoff",2800.f}, {"res",.2f}, {"attack",.08f}, {"decay",.6f}, {"sustain",.6f}, {"release",.5f}, {"rmix",.25f} }),
            P ("Brick Hit",     { {"wave1",3.f}, {"noise",.35f}, {"sub",.5f}, {"cutoff",2500.f}, {"fenv",.7f}, {"fdecay",.1f}, {"decay",.25f}, {"release",.2f}, {"oct",-1.f}, {"dmix",.1f} }),
            P ("Demon Pluck",   { {"wave1",2.f}, {"wave2",2.f}, {"mixb",.5f}, {"detune",.5f}, {"cutoff",900.f}, {"res",.35f}, {"fenv",.9f}, {"fdecay",.2f}, {"decay",.4f}, {"release",.2f}, {"sub",.3f}, {"dmix",.2f} }),
        } },

        { "Hyperpop", juce::Colour (0xff6bff9e), juce::Colour (0xffff4fd8), {
            P ("Glitter Lead",     { {"wave1",2.f}, {"wave2",3.f}, {"mixb",.5f}, {"detune",.25f}, {"fmamt",.2f}, {"fmratio",2.f}, {"cutoff",9000.f}, {"res",.2f}, {"decay",.2f}, {"sustain",.8f}, {"release",.2f}, {"dmix",.3f}, {"dtime",.22f}, {"dfb",.5f} }),
            P ("Sugar Rush Pluck", { {"wave1",3.f}, {"cutoff",1500.f}, {"res",.35f}, {"fenv",.9f}, {"fdecay",.12f}, {"decay",.3f}, {"release",.15f}, {"oct",1.f}, {"dmix",.35f} }),
            P ("Chrome Saw",       { {"wave1",2.f}, {"wave2",2.f}, {"mixb",1.f}, {"detune",.7f}, {"cutoff",7000.f}, {"res",.1f}, {"attack",.01f}, {"decay",.5f}, {"sustain",.8f}, {"release",.4f}, {"rmix",.25f} }),
            P ("Bubblegum Bell",   { {"fmamt",.9f}, {"fmratio",4.f}, {"cutoff",12000.f}, {"fdecay",.2f}, {"decay",1.f}, {"release",.5f}, {"oct",1.f}, {"dmix",.25f} }),
            P ("Laser Hit",        { {"fmamt",1.f}, {"fmratio",6.f}, {"noise",.2f}, {"cutoff",10000.f}, {"fdecay",.08f}, {"decay",.3f}, {"release",.2f}, {"dmix",.2f} }),
            P ("Pixel Keys",       { {"wave1",3.f}, {"wave2",1.f}, {"mixb",.4f}, {"cutoff",3500.f}, {"fenv",.4f}, {"fdecay",.2f}, {"decay",.5f}, {"sustain",.2f}, {"release",.25f}, {"dmix",.25f} }),
        } },

        { "Dreamcore", juce::Colour (0xffc9a8ff), juce::Colour (0xff8fd0ff), {
            P ("Music Box",      { {"fmamt",.25f}, {"fmratio",5.f}, {"cutoff",9000.f}, {"fdecay",.3f}, {"decay",1.4f}, {"release",1.f}, {"oct",1.f}, {"dmix",.3f}, {"rmix",.45f} }),
            P ("Faded Pad",      { {"wave1",1.f}, {"wave2",0.f}, {"mixb",.6f}, {"detune",.4f}, {"cutoff",1800.f}, {"attack",1.5f}, {"decay",1.5f}, {"sustain",.9f}, {"release",3.f}, {"rmix",.65f}, {"rsize",.95f} }),
            P ("Lullaby Bell",   { {"fmamt",.4f}, {"fmratio",3.f}, {"cutoff",6000.f}, {"fdecay",.7f}, {"decay",2.4f}, {"release",2.f}, {"oct",1.f}, {"rmix",.55f}, {"dmix",.3f}, {"dfb",.5f} }),
            P ("Childhood Keys", { {"wave1",1.f}, {"wave2",0.f}, {"mixb",.4f}, {"cutoff",3500.f}, {"fenv",.3f}, {"fdecay",.5f}, {"decay",1.2f}, {"sustain",.1f}, {"release",.9f}, {"rmix",.4f} }),
            P ("Ghost Strings",  { {"wave1",2.f}, {"wave2",1.f}, {"mixb",.6f}, {"detune",.5f}, {"cutoff",1500.f}, {"attack",.9f}, {"decay",1.f}, {"sustain",.8f}, {"release",2.2f}, {"rmix",.6f} }),
            P ("Memory Hit",     { {"fmamt",.5f}, {"fmratio",2.5f}, {"noise",.1f}, {"cutoff",4000.f}, {"fdecay",.2f}, {"decay",.8f}, {"release",.8f}, {"rmix",.5f} }),
        } },
    };
    return g;
}

//==============================================================================
// Голос синтезатора
//==============================================================================
static inline float polyBlep (float t, float dt)
{
    if (t < dt)         { t /= dt;         return t + t - t * t - 1.f; }
    if (t > 1.f - dt)   { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

static inline float oscShape (int wave, float t, float dt)
{
    switch (wave)
    {
        case 0:  return std::sin (kTwoPi * t);
        case 1:  return t < 0.25f ? 4.f * t : (t < 0.75f ? 2.f - 4.f * t : 4.f * t - 4.f);
        case 2:  return 2.f * t - 1.f - polyBlep (t, dt);
        default:
        {
            float v = t < 0.5f ? 1.f : -1.f;
            v += polyBlep (t, dt);
            float t2 = t + 0.5f;
            if (t2 >= 1.f) t2 -= 1.f;
            v -= polyBlep (t2, dt);
            return v;
        }
    }
}

struct NebulaSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class NebulaVoice : public juce::SynthesiserVoice
{
public:
    explicit NebulaVoice (const VoiceParams& p) : vp (p) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<NebulaSound*> (s) != nullptr;
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        sr = (float) getSampleRate();
        const float note = (float) midiNote + 12.f * vp.oct;
        freq = 440.f * std::pow (2.f, (note - 69.f) / 12.f);
        vel = velocity;
        phA = 0.f;
        phB = rng.nextFloat();
        phS = 0.f;
        phM = 0.f;
        detuneRatio = std::pow (2.f, vp.detune * 50.f / 1200.f);
        env = 1.f;
        envCoef = std::exp (-1.f / (juce::jmax (0.005f, vp.fdecay) * sr));
        z1 = z2 = 0.f;
        pan = (rng.nextFloat() - 0.5f) * 0.6f;

        adsr.setSampleRate ((double) sr);
        adsr.setParameters (juce::ADSR::Parameters (vp.attack, vp.decay, vp.sustain, vp.release));
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.setParameters (juce::ADSR::Parameters (vp.attack, vp.decay, vp.sustain, vp.release));
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        float* L = out.getWritePointer (0, startSample);
        float* R = out.getNumChannels() > 1 ? out.getWritePointer (1, startSample) : nullptr;

        const float dtA = freq / sr;
        const float dtB = dtA * detuneRatio;
        const float gL  = std::cos ((pan + 1.f) * kPi * 0.25f);
        const float gR  = std::sin ((pan + 1.f) * kPi * 0.25f);

        for (int i = 0; i < numSamples; ++i)
        {
            const float a = adsr.getNextSample();
            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }

            env *= envCoef;

            // FM-модулятор
            float mod = 0.f;
            if (vp.fmamt > 0.001f)
            {
                phM += dtA * vp.fmratio;
                phM -= std::floor (phM);
                mod = std::sin (kTwoPi * phM) * vp.fmamt * 7.f * (0.25f + 0.75f * env);
            }

            float pa = phA + mod / kTwoPi;
            pa -= std::floor (pa);

            float s = oscShape (vp.wave1, pa, dtA);
            if (vp.mixb > 0.001f) s += vp.mixb * oscShape (vp.wave2, phB, dtB);
            if (vp.sub > 0.001f)  s += vp.sub * std::sin (kTwoPi * phS);
            if (vp.noise > 0.001f) s += vp.noise * env * (rng.nextFloat() * 2.f - 1.f);

            phA += dtA; if (phA >= 1.f) phA -= 1.f;
            phB += dtB; if (phB >= 1.f) phB -= 1.f;
            phS += dtA * 0.5f; if (phS >= 1.f) phS -= 1.f;

            // фильтр (TPT state variable, low-pass)
            float fc = vp.cutoff * std::pow (2.f, vp.fenv * 5.f * env);
            fc = juce::jlimit (20.f, 0.45f * sr, fc);
            const float g  = std::tan (kPi * fc / sr);
            const float Rr = 1.f / (2.f * (0.55f + vp.res * 9.f));
            const float h  = (s - (2.f * Rr + g) * z1 - z2) / (1.f + 2.f * Rr * g + g * g);
            const float bp = g * h + z1;
            z1 = g * h + bp;
            const float lp = g * bp + z2;
            z2 = g * bp + lp;

            const float o = lp * a * vel * 0.5f;
            L[i] += o * gL;
            if (R != nullptr) R[i] += o * gR;
        }
    }

private:
    const VoiceParams& vp;
    juce::ADSR adsr;
    juce::Random rng;
    float sr = 44100.f, freq = 440.f, vel = 1.f, pan = 0.f, detuneRatio = 1.f;
    float phA = 0.f, phB = 0.f, phS = 0.f, phM = 0.f;
    float env = 1.f, envCoef = 0.999f;
    float z1 = 0.f, z2 = 0.f;
};

//==============================================================================
// Процессор
//==============================================================================
class NebulaProcessor : public juce::AudioProcessor
{
public:
    NebulaProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "STATE", createLayout())
    {
        for (int i = 0; i < NUM_PARAMS; ++i)
            raw[i] = apvts.getRawParameterValue (kParams[i].id);

        for (auto& b : noteState)
            b.store (false);

        for (int i = 0; i < 8; ++i)
            synth.addVoice (new NebulaVoice (vp));
        synth.addSound (new NebulaSound());
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        const juce::StringArray waves { "Sine", "Triangle", "Saw", "Square" };

        for (auto& d : kParams)
        {
            if (d.choice)
                layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { d.id, 1 }, d.name, waves, (int) d.def));
            else
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    juce::ParameterID { d.id, 1 }, d.name,
                    juce::NormalisableRange<float> (d.lo, d.hi, d.step, d.skew), d.def));
        }
        return layout;
    }

    const juce::String getName() const override { return "Nebula"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay (double sampleRate, int) override
    {
        sr = sampleRate;
        synth.setCurrentPlaybackSampleRate (sampleRate);
        reverb.setSampleRate (sampleRate);
        reverb.reset();
        dSize = (int) (sampleRate * 1.0) + 8;
        for (auto& b : dBuf)
            b.assign ((size_t) dSize, 0.f);
        dW = 0;
    }
    void releaseResources() override {}

    void updateParams()
    {
        auto v = [this] (int i) { return raw[i]->load(); };
        vp.wave1   = (int) std::lround (v (WAVE1));
        vp.wave2   = (int) std::lround (v (WAVE2));
        vp.mixb    = v (MIXB);
        vp.detune  = v (DETUNE);
        vp.fmamt   = v (FMAMT);
        vp.fmratio = v (FMRATIO);
        vp.sub     = v (SUB);
        vp.noise   = v (NOISE);
        vp.cutoff  = v (CUTOFF);
        vp.res     = v (RES);
        vp.fenv    = v (FENV);
        vp.fdecay  = v (FDECAY);
        vp.attack  = v (ATTACK);
        vp.decay   = v (DECAY);
        vp.sustain = v (SUSTAIN);
        vp.release = v (RELEASE);
        vp.oct     = std::round (v (OCT));
        vp.vol     = v (VOL);
        vp.dmix    = v (DMIX);
        vp.dtime   = v (DTIME);
        vp.dfb     = v (DFB);
        vp.rmix    = v (RMIX);
        vp.rsize   = v (RSIZE);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;

        const int n = buffer.getNumSamples();
        buffer.clear();
        if (buffer.getNumChannels() < 2)
            return;

        updateParams();
        keyboardState.processNextMidiBuffer (midi, 0, n, true);

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn())
                noteState[(size_t) msg.getNoteNumber()].store (true);
            else if (msg.isNoteOff())
                noteState[(size_t) msg.getNoteNumber()].store (false);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                for (auto& b : noteState) b.store (false);
        }

        synth.renderNextBlock (buffer, midi, 0, n);

        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);

        // эхо (пинг-понг)
        if (dSize > 16)
        {
            const int ds = juce::jlimit (1, dSize - 1, (int) (vp.dtime * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                int r = dW - ds;
                if (r < 0) r += dSize;
                const float dl = dBuf[0][(size_t) r];
                const float dr = dBuf[1][(size_t) r];
                const float in = (L[i] + R[i]) * 0.5f;

                dBuf[0][(size_t) dW] = in + dr * vp.dfb;
                dBuf[1][(size_t) dW] = dl * vp.dfb;

                L[i] += dl * vp.dmix;
                R[i] += dr * vp.dmix;

                if (++dW >= dSize) dW = 0;
            }
        }

        // реверб
        juce::Reverb::Parameters rp;
        rp.roomSize   = vp.rsize;
        rp.damping    = 0.45f;
        rp.wetLevel   = vp.rmix * 0.4f;
        rp.dryLevel   = 1.f - vp.rmix * 0.2f;
        rp.width      = 1.f;
        rp.freezeMode = 0.f;
        reverb.setParameters (rp);
        reverb.processStereo (L, R, n);

        // мастер
        float peak = 0.f;
        for (int i = 0; i < n; ++i)
        {
            L[i] = std::tanh (L[i] * 1.1f) * vp.vol;
            R[i] = std::tanh (R[i] * 1.1f) * vp.vol;
            peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
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

    void setParamValue (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    void applyPreset (int g, int p)
    {
        const auto& gs = genres();
        if (g < 0 || g >= (int) gs.size()) return;
        if (p < 0 || p >= (int) gs[(size_t) g].presets.size()) return;

        for (auto& d : kParams)
            setParamValue (d.id, d.def);
        for (auto& pv : gs[(size_t) g].presets[(size_t) p].v)
            setParamValue (pv.id, pv.v);

        apvts.state.setProperty ("genre", g, nullptr);
        apvts.state.setProperty ("preset", p, nullptr);
    }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    std::array<std::atomic<bool>, 128> noteState;
    std::atomic<float> level { 0.f };

private:
    double sr = 44100.0;
    VoiceParams vp;
    juce::Synthesiser synth;
    juce::Reverb reverb;
    std::atomic<float>* raw[NUM_PARAMS] {};
    std::vector<float> dBuf[2];
    int dSize = 0, dW = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaProcessor)
};

//==============================================================================
// Вид: ручки и списки
//==============================================================================
class NebulaLook : public juce::LookAndFeel_V4
{
public:
    juce::Colour accent { 0xff5ee7ff };

    NebulaLook()
    {
        setColour (juce::TextButton::buttonColourId, juce::Colours::black.withAlpha (0.35f));
        setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        setColour (juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha (0.3f));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startA, float endA, juce::Slider&) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.f);
        auto c = b.getCentre();
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;

        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.28f * (0.3f + pos)), c.x, c.y,
                                                 accent.withAlpha (0.f), c.x, c.y - r * 1.8f, true));
        g.fillEllipse (c.x - r * 1.8f, c.y - r * 1.8f, r * 3.6f, r * 3.6f);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2f44), c.x, c.y - r,
                                                 juce::Colour (0xff0c0d18), c.x, c.y + r, false));
        g.fillEllipse (c.x - r, c.y - r, r * 2.f, r * 2.f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.f, r * 2.f, 1.2f);

        const float ar = r * 0.8f;
        juce::Path track;
        track.addCentredArc (c.x, c.y, ar, ar, 0.f, startA, endA, true);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.strokePath (track, juce::PathStrokeType (3.f));

        const float a = startA + pos * (endA - startA);
        juce::Path val;
        val.addCentredArc (c.x, c.y, ar, ar, 0.f, startA, a, true);
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (8.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (juce::Colours::white);
        g.drawLine (c.x + std::sin (a) * r * 0.2f, c.y - std::cos (a) * r * 0.2f,
                    c.x + std::sin (a) * r * 0.58f, c.y - std::cos (a) * r * 0.58f, 2.5f);
    }
};

struct ListModel : public juce::ListBoxModel
{
    std::function<int()> count;
    std::function<juce::String (int)> text;
    std::function<void (int)> select;
    std::function<juce::Colour()> accent;

    int getNumRows() override { return count ? count() : 0; }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected)
        {
            g.setColour ((accent ? accent() : juce::Colours::white).withAlpha (0.38f));
            g.fillRoundedRectangle (2.f, 1.f, (float) w - 4.f, (float) h - 2.f, 7.f);
        }
        g.setColour (juce::Colours::white.withAlpha (selected ? 1.f : 0.78f));
        g.setFont (juce::Font (14.5f, selected ? juce::Font::bold : juce::Font::plain));
        g.drawText (text ? text (row) : juce::String(), 12, 0, w - 16, h, juce::Justification::centredLeft);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (select) select (row);
    }
};

//==============================================================================
// Окно
//==============================================================================
class NebulaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NebulaEditor (NebulaProcessor& p)
        : AudioProcessorEditor (&p), proc (p),
          keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        setSize ((int) kW, (int) kH);
        setLookAndFeel (&look);

        // списки
        genreModel.count  = [] { return (int) genres().size(); };
        genreModel.text   = [] (int i) { return genres()[(size_t) i].name; };
        genreModel.accent = [this] { return curA; };
        genreModel.select = [this] (int row) { curGenre = row; refreshLists(); };

        presetModel.count  = [this] { return (int) genres()[(size_t) curGenre].presets.size(); };
        presetModel.text   = [this] (int i) { return genres()[(size_t) curGenre].presets[(size_t) i].name; };
        presetModel.accent = [this] { return curA; };
        presetModel.select = [this] (int row) { loadPreset (curGenre, row); };

        for (auto* l : { &genreList, &presetList })
        {
            l->setRowHeight (27);
            l->setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
            l->setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible (*l);
        }
        genreList.setModel (&genreModel);
        presetList.setModel (&presetModel);

        // кнопки
        prevBtn.onClick = [this] { step (-1); };
        nextBtn.onClick = [this] { step (1); };
        skyBtn.onClick  = [this]
        {
            skyMode = (skyMode + 1) % 3;
            skyBtn.setButtonText (skyMode == 0 ? "SKY: AUTO" : (skyMode == 1 ? "SKY: NIGHT" : "SKY: DAY"));
        };
        addAndMakeVisible (prevBtn);
        addAndMakeVisible (nextBtn);
        addAndMakeVisible (skyBtn);

        // ручки
        struct KD { const char* id; const char* label; };
        static const KD kds[12] = {
            { "cutoff", "CUTOFF" }, { "res", "RESO" },   { "fenv", "FILTER ENV" },
            { "attack", "ATTACK" }, { "release", "RELEASE" }, { "detune", "DETUNE" },
            { "fmamt", "FM" },      { "sub", "SUB" },    { "noise", "NOISE" },
            { "dmix", "ECHO" },     { "rmix", "SPACE" }, { "vol", "VOLUME" } };

        for (auto& k : kds)
        {
            auto kn = std::make_unique<Knob>();
            kn->label = k.label;
            kn->s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            kn->s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            kn->s.setRotaryParameters (kPi * 1.2f, kPi * 2.8f, true);
            kn->s.setPopupDisplayEnabled (true, true, this);
            addAndMakeVisible (kn->s);
            kn->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, k.id, kn->s);
            knobs.push_back (std::move (kn));
        }

        // клавиатура
        keyboard.setAvailableRange (36, 96);
        keyboard.setKeyWidth (27.f);
        keyboard.setScrollButtonsVisible (false);
        keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xfff2f0ff));
        keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff14141f));
        keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0x33000000));
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour (0x3359d8ff));
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour (0xaa59d8ff));
        addAndMakeVisible (keyboard);

        // звёзды, облака, пылинки
        juce::Random r (11);
        for (int i = 0; i < 170; ++i)
            stars.push_back ({ r.nextFloat() * kW, r.nextFloat() * 390.f, 1.f + r.nextFloat() * 1.8f, r.nextFloat() * 6.28f });
        for (int i = 0; i < 7; ++i)
            clouds.push_back ({ r.nextFloat() * (kW + 300.f), 40.f + r.nextFloat() * 220.f, 0.7f + r.nextFloat() * 0.9f, 4.f + r.nextFloat() * 10.f });
        for (int i = 0; i < 55; ++i)
            motes.push_back ({ r.nextFloat() * kW, 80.f + r.nextFloat() * 300.f, 1.f + r.nextFloat() * 2.f, r.nextFloat() * 6.28f, 0.1f + r.nextFloat() * 0.35f });

        for (auto& b : prevHeld) b = false;
        for (auto& b : beam) b = 0.f;

        // стартовый пресет
        const int nG = (int) genres().size();
        if (! proc.apvts.state.hasProperty ("genre"))
        {
            curGenre = 0;
            curA = tgtA = genres()[0].a;
            curB = tgtB = genres()[0].b;
            loadPreset (0, 0);
        }
        else
        {
            const int g = juce::jlimit (0, nG - 1, (int) proc.apvts.state.getProperty ("genre", 0));
            const int pn = (int) genres()[(size_t) g].presets.size();
            const int pr = juce::jlimit (0, pn - 1, (int) proc.apvts.state.getProperty ("preset", 0));
            curGenre = loadedGenre = g;
            loadedPreset = pr;
            curA = tgtA = genres()[(size_t) g].a;
            curB = tgtB = genres()[(size_t) g].b;
            refreshLists();
        }

        startTimerHz (60);
    }

    ~NebulaEditor() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void resized() override
    {
        prevBtn.setBounds (352, 14, 34, 30);
        nextBtn.setBounds (594, 14, 34, 30);
        skyBtn.setBounds ((int) kW - 160, 14, 140, 30);

        genreList.setBounds (14, 90, 250, 190);
        presetList.setBounds (14, 304, 250, 268);

        const float colW = 692.f / 6.f;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int col = (int) (i % 6);
            const int row = (int) (i / 6);
            const float cx = 278.f + ((float) col + 0.5f) * colW;
            knobs[i]->s.setBounds ((int) (cx - 29.f), 412 + row * 80, 58, 58);
        }

        keyboard.setBounds (0, (int) kH - 80, (int) kW, 80);
    }

    //----------------------------------------------------------------------
    void timerCallback() override
    {
        const float dt = 1.f / 60.f;
        t += dt;

        float dayTarget;
        if (skyMode == 0)
        {
            autoPhase += dt / 55.f;
            dayTarget = 0.5f - 0.5f * std::cos (kTwoPi * autoPhase);
        }
        else
            dayTarget = skyMode == 2 ? 1.f : 0.f;
        day += (dayTarget - day) * 0.04f;

        lv += (proc.level.load() - lv) * 0.25f;
        curA = curA.interpolatedWith (tgtA, 0.06f);
        curB = curB.interpolatedWith (tgtB, 0.06f);
        look.accent = curA;

        // ноты
        for (int n = 0; n < 128; ++n)
        {
            const bool h = proc.noteState[(size_t) n].load();
            if (h && ! prevHeld[n])
                spawnNote (n);
            prevHeld[n] = h;
            beam[n] += ((h ? 1.f : 0.f) - beam[n]) * (h ? 0.35f : 0.07f);
        }

        // кольца вокруг шара
        ringTimer += dt;
        if (ringTimer > 0.9f - 0.5f * juce::jmin (1.f, lv * 3.f))
        {
            ringTimer = 0.f;
            rings.push_back ({ 24.f, 0.4f });
        }
        for (auto& r : rings) { r.r += 0.9f + lv * 3.f; r.a -= 0.0045f; }
        rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.a <= 0.f; }), rings.end());

        for (auto& r : ripples) { r.r += 2.4f; r.a -= 0.013f; }
        ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& r) { return r.a <= 0.f; }), ripples.end());

        for (auto& s : sparks) { s.x += s.vx; s.y += s.vy; s.vy += 0.06f; s.life += 1.f; }
        sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life >= s.max; }), sparks.end());

        for (auto& m : motes)
        {
            m.ph += 0.02f;
            m.x += std::sin (m.ph) * 0.3f;
            m.y -= m.sp;
            if (m.y < 70.f) { m.y = 400.f; m.x = rnd.nextFloat() * kW; }
        }

        for (auto& s : stars)
            s.ph += 0.05f;

        repaint();
    }

    //----------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const float W = kW, H = kH;
        const float d = smooth01 (day);
        const juce::Colour A = curA, B = curB;

        // небо
        const juce::Colour nTop = juce::Colour (0xff04050d).interpolatedWith (A.darker (2.2f), 0.18f);
        const juce::Colour nBot = juce::Colour (0xff150d2e).interpolatedWith (B.darker (1.2f), 0.24f);
        const juce::Colour dTop = juce::Colour (0xff5aa9ff).interpolatedWith (A, 0.12f);
        const juce::Colour dBot = juce::Colour (0xffffd9c0).interpolatedWith (B, 0.24f);
        const juce::Colour top = nTop.interpolatedWith (dTop, d);
        const juce::Colour bot = nBot.interpolatedWith (dBot, d);

        g.setGradientFill (juce::ColourGradient (top, 0.f, 0.f, bot, 0.f, 420.f, false));
        g.fillAll();

        // звёзды
        if (d < 0.97f)
        {
            for (auto& s : stars)
            {
                const float tw = 0.5f + 0.5f * std::sin (s.ph);
                g.setColour (juce::Colour (0xffe8f0ff).withAlpha ((1.f - d) * (0.3f + 0.7f * tw)));
                g.fillEllipse (s.x - s.s * 0.5f, s.y - s.s * 0.5f, s.s, s.s);
            }
        }

        // северное сияние
        const float vis = (1.f - d) * (1.f - d);
        if (vis > 0.01f)
        {
            const juce::Colour cols[3] = { A, B, A.interpolatedWith (B, 0.5f) };
            for (int b = 0; b < 3; ++b)
            {
                juce::Path p;
                const float base = 110.f + 50.f * (float) b;
                for (float x = 0.f; x <= W + 16.f; x += 16.f)
                {
                    const float y = base + 24.f * std::sin (x * 0.012f + t * 0.5f + (float) b * 2.f)
                                         + 14.f * std::sin (x * 0.027f - t * 0.8f + (float) b);
                    if (x == 0.f) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                for (float wd = 46.f; wd >= 8.f; wd -= 12.f)
                {
                    g.setColour (cols[b].withAlpha (vis * 0.05f));
                    g.strokePath (p, juce::PathStrokeType (wd, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }
        }

        // луна и солнце
        {
            const float sx = 860.f;
            const float moonY = 110.f + 230.f * d;
            const float sunY  = 340.f - 250.f * d;

            const float ma = 1.f - d;
            if (ma > 0.01f)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffcfe0ff).withAlpha (0.35f * ma), sx, moonY,
                                                         juce::Colour (0xffcfe0ff).withAlpha (0.f), sx + 110.f, moonY, true));
                g.fillEllipse (sx - 110.f, moonY - 110.f, 220.f, 220.f);
                g.setColour (juce::Colour (0xfff2f5ff).withAlpha (ma));
                g.fillEllipse (sx - 30.f, moonY - 30.f, 60.f, 60.f);
                g.setColour (juce::Colour (0xffb8c6e8).withAlpha (0.5f * ma));
                g.fillEllipse (sx - 14.f, moonY - 12.f, 14.f, 14.f);
                g.fillEllipse (sx + 6.f, moonY + 6.f, 10.f, 10.f);
                g.fillEllipse (sx - 2.f, moonY + 14.f, 8.f, 8.f);
            }
            if (d > 0.01f)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffe9a8).withAlpha (0.6f * d), sx, sunY,
                                                         juce::Colour (0xffffe9a8).withAlpha (0.f), sx + 190.f, sunY, true));
                g.fillEllipse (sx - 190.f, sunY - 190.f, 380.f, 380.f);
                g.setColour (juce::Colour (0xfffff6d0).withAlpha (d));
                g.fillEllipse (sx - 36.f, sunY - 36.f, 72.f, 72.f);
            }
        }

        // облака
        if (d > 0.02f)
        {
            for (auto& c : clouds)
            {
                const float cx = std::fmod (c.x + t * c.sp, W + 320.f) - 160.f;
                for (int k = 0; k < 4; ++k)
                {
                    const float px = cx + ((float) k - 1.5f) * 42.f * c.s;
                    const float py = c.y + ((k % 2) ? -10.f : 6.f) * c.s;
                    const float pr = (46.f + 8.f * (float) (k % 3)) * c.s;
                    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.42f * d), px, py,
                                                             juce::Colours::white.withAlpha (0.f), px + pr, py, true));
                    g.fillEllipse (px - pr, py - pr, pr * 2.f, pr * 2.f);
                }
            }
        }

        // шар
        {
            const float ox = kOrbX, oy = kOrbY + std::sin (t * 0.9f) * 5.f;
            const juce::Colour oc = A.interpolatedWith (B, 0.5f + 0.5f * std::sin (t * 0.6f));
            const float lvl = juce::jmin (1.f, lv * 2.f);

            for (auto& r : rings)
            {
                g.setColour (oc.withAlpha (juce::jlimit (0.f, 1.f, r.a)));
                g.drawEllipse (ox - r.r, oy - r.r, r.r * 2.f, r.r * 2.f, 1.5f);
            }

            const float haloR = 120.f + 90.f * lvl;
            g.setGradientFill (juce::ColourGradient (oc.withAlpha (0.5f), ox, oy, oc.withAlpha (0.f), ox + haloR, oy, true));
            g.fillEllipse (ox - haloR, oy - haloR, haloR * 2.f, haloR * 2.f);

            for (int i = 0; i < 10; ++i)
            {
                const float ang = t * 0.15f + (float) i * kTwoPi / 10.f;
                const float dx = std::cos (ang), dy = std::sin (ang);
                const float len = 70.f + 60.f * lvl;
                g.setColour (oc.withAlpha (0.1f));
                g.drawLine (ox + dx * 26.f, oy + dy * 26.f, ox + dx * len, oy + dy * len, 4.f);
                g.setColour (oc.withAlpha (0.2f));
                g.drawLine (ox + dx * 26.f, oy + dy * 26.f, ox + dx * len, oy + dy * len, 1.2f);
            }

            const float coreR = 22.f + 12.f * lvl;
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, ox, oy, oc.withAlpha (0.f), ox + coreR * 2.2f, oy, true));
            g.fillEllipse (ox - coreR * 2.2f, oy - coreR * 2.2f, coreR * 4.4f, coreR * 4.4f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (ox - coreR * 0.55f, oy - coreR * 0.55f, coreR * 1.1f, coreR * 1.1f);
        }

        // холмы
        for (int i = 0; i < 3; ++i)
        {
            juce::Path hill;
            const float baseY = 366.f + (float) i * 15.f;
            const float amp = 26.f - (float) i * 6.f;
            hill.startNewSubPath (0.f, H);
            for (float x = 0.f; x <= W + 20.f; x += 20.f)
            {
                const float y = baseY - amp * (0.5f + 0.5f * std::sin (x * 0.009f * (float) (i + 1) + (float) i * 2.3f))
                                      - 8.f * std::sin (x * 0.021f + (float) i);
                hill.lineTo (x, y);
            }
            hill.lineTo (W + 20.f, H);
            hill.closeSubPath();

            const juce::Colour nc = juce::Colour (0xff0d0a1c).interpolatedWith (A.darker (1.6f), 0.08f + 0.06f * (float) i).darker (0.15f * (float) i);
            const juce::Colour dc = dBot.darker (0.28f + 0.2f * (float) i);
            g.setColour (nc.interpolatedWith (dc, d));
            g.fillPath (hill);
        }

        // лучи нот, круги на земле, искры
        for (int n = 0; n < 128; ++n)
        {
            if (beam[n] < 0.02f) continue;
            const float x = noteX (n);
            const juce::Colour c = noteColour (n);
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.65f * beam[n]), x, kGroundY, c.withAlpha (0.f), x, kGroundY - 270.f, false));
            g.fillRect (x - 5.f, kGroundY - 270.f, 10.f, 270.f);
            g.setColour (c.withAlpha (0.9f * beam[n]));
            g.fillRect (x - 1.2f, kGroundY - 270.f * beam[n], 2.4f, 270.f * beam[n]);
        }
        for (auto& r : ripples)
        {
            g.setColour (r.c.withAlpha (juce::jlimit (0.f, 1.f, r.a)));
            g.drawEllipse (r.x - r.r, r.y - r.r * 0.3f, r.r * 2.f, r.r * 0.6f, 1.8f);
        }
        for (auto& s : sparks)
        {
            const float k = s.life / s.max;
            g.setColour (s.c.withAlpha ((1.f - k) * 0.35f));
            g.fillEllipse (s.x - s.size * 2.f, s.y - s.size * 2.f, s.size * 4.f, s.size * 4.f);
            g.setColour (juce::Colours::white.withAlpha (1.f - k));
            g.fillEllipse (s.x - s.size * 0.5f, s.y - s.size * 0.5f, s.size, s.size);
        }

        // светлячки и пыльца
        for (auto& m : motes)
        {
            const float tw = 0.5f + 0.5f * std::sin (m.ph * 3.f);
            const juce::Colour mc = B.interpolatedWith (juce::Colours::white, d);
            g.setColour (mc.withAlpha (0.18f + 0.35f * tw));
            g.fillEllipse (m.x, m.y, m.s, m.s);
        }

        // виньетка
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, W * 0.5f, H * 0.45f,
                                                 juce::Colours::black.withAlpha (0.4f), 0.f, 0.f, true));
        g.fillAll();

        // панели
        auto panel = [&] (juce::Rectangle<float> r)
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (r, 16.f);
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.drawRoundedRectangle (r, 16.f, 1.2f);
        };
        panel ({ 8.f, 8.f, W - 16.f, 48.f });
        panel ({ 8.f, 64.f, 262.f, 512.f });
        panel ({ 278.f, 400.f, 692.f, 176.f });

        // текст
        auto txt = [&] (const juce::String& s, juce::Rectangle<int> r, float size, bool bold, float kern, juce::Colour c,
                        juce::Justification j = juce::Justification::centred)
        {
            juce::Font f (size, bold ? juce::Font::bold : juce::Font::plain);
            f.setExtraKerningFactor (kern);
            g.setFont (f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawText (s, r.translated (0, 1), j);
            g.setColour (c);
            g.drawText (s, r, j);
        };

        txt ("NEBULA", { 24, 14, 200, 30 }, 24.f, true, 0.35f, juce::Colours::white, juce::Justification::centredLeft);
        txt ("SYNTH", { 150, 18, 80, 22 }, 11.f, false, 0.4f, A, juce::Justification::centredLeft);

        const auto& gd = genres()[(size_t) loadedGenre];
        txt (gd.name.toUpperCase(), { 392, 8, 196, 20 }, 11.f, true, 0.3f, A);
        txt (gd.presets[(size_t) loadedPreset].name, { 392, 24, 196, 28 }, 19.f, true, 0.05f, juce::Colours::white);

        txt ("FOLDERS", { 22, 70, 200, 16 }, 11.f, true, 0.4f, juce::Colours::white.withAlpha (0.6f), juce::Justification::centredLeft);
        txt ((genres()[(size_t) curGenre].name + " PRESETS").toUpperCase(), { 22, 284, 240, 16 }, 11.f, true, 0.3f, juce::Colours::white.withAlpha (0.6f), juce::Justification::centredLeft);

        const float colW = 692.f / 6.f;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int col = (int) (i % 6);
            const int row = (int) (i / 6);
            const int cx = (int) (278.f + ((float) col + 0.5f) * colW);
            txt (knobs[i]->label, { cx - 50, 472 + row * 80, 100, 16 }, 11.f, true, 0.25f, juce::Colours::white.withAlpha (0.8f));
        }
    }

private:
    struct Knob  { juce::Slider s; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; juce::String label; };
    struct Star  { float x, y, s, ph; };
    struct Cloud { float x, y, s, sp; };
    struct Mote  { float x, y, s, ph, sp; };
    struct Ring  { float r, a; };
    struct Ripple { float x, y, r, a; juce::Colour c; };
    struct Spark { float x, y, vx, vy, life, max, size; juce::Colour c; };

    static constexpr float kW = 980.f;
    static constexpr float kH = 660.f;
    static constexpr float kOrbX = 623.f;
    static constexpr float kOrbY = 205.f;
    static constexpr float kGroundY = 384.f;

    static float noteX (int n) { return 296.f + juce::jlimit (0.f, 1.f, ((float) n - 36.f) / 60.f) * 660.f; }

    juce::Colour noteColour (int n) const
    {
        return curA.interpolatedWith (curB, juce::jlimit (0.f, 1.f, ((float) n - 36.f) / 60.f));
    }

    void spawnNote (int n)
    {
        const float x = noteX (n);
        const juce::Colour c = noteColour (n);
        ripples.push_back ({ x, kGroundY, 6.f, 0.85f, c });
        for (int i = 0; i < 14; ++i)
            sparks.push_back ({ x + (rnd.nextFloat() - 0.5f) * 8.f, kGroundY,
                                (rnd.nextFloat() - 0.5f) * 2.4f, -(1.5f + rnd.nextFloat() * 3.f),
                                0.f, 40.f + rnd.nextFloat() * 40.f, 1.5f + rnd.nextFloat() * 2.f, c });
        if (ripples.size() > 80) ripples.erase (ripples.begin(), ripples.begin() + 20);
        if (sparks.size() > 600) sparks.erase (sparks.begin(), sparks.begin() + 150);
    }

    void loadPreset (int g, int p)
    {
        curGenre = g;
        loadedGenre = g;
        loadedPreset = p;
        proc.applyPreset (g, p);
        refreshLists();
    }

    void refreshLists()
    {
        tgtA = genres()[(size_t) curGenre].a;
        tgtB = genres()[(size_t) curGenre].b;
        genreList.updateContent();
        genreList.selectRow (curGenre, true, true);
        presetList.updateContent();
        if (loadedGenre == curGenre)
            presetList.selectRow (loadedPreset, false, true);
        else
            presetList.deselectAllRows();
        repaint();
    }

    void step (int dir)
    {
        const auto& gs = genres();
        const int nG = (int) gs.size();
        int g = loadedGenre;
        int p = loadedPreset + dir;
        if (p >= (int) gs[(size_t) g].presets.size())
        {
            g = (g + 1) % nG;
            p = 0;
        }
        else if (p < 0)
        {
            g = (g + nG - 1) % nG;
            p = (int) gs[(size_t) g].presets.size() - 1;
        }
        loadPreset (g, p);
    }

    NebulaProcessor& proc;
    NebulaLook look;
    ListModel genreModel, presetModel;
    juce::ListBox genreList { "genres", nullptr };
    juce::ListBox presetList { "presets", nullptr };
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, skyBtn { "SKY: AUTO" };
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::MidiKeyboardComponent keyboard;

    std::vector<Star> stars;
    std::vector<Cloud> clouds;
    std::vector<Mote> motes;
    std::vector<Ring> rings;
    std::vector<Ripple> ripples;
    std::vector<Spark> sparks;
    juce::Random rnd;

    bool  prevHeld[128] {};
    float beam[128] {};

    int curGenre = 0, loadedGenre = 0, loadedPreset = 0, skyMode = 0;
    juce::Colour curA { 0xff5ee7ff }, curB { 0xffff6ad5 }, tgtA { 0xff5ee7ff }, tgtB { 0xffff6ad5 };
    float day = 0.f, autoPhase = 0.f, t = 0.f, lv = 0.f, ringTimer = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaEditor)
};

juce::AudioProcessorEditor* NebulaProcessor::createEditor() { return new NebulaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NebulaProcessor(); }
