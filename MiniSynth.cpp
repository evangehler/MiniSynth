#include "daisy_seed.h"
#include "daisysp.h"
#include "dev/oled_ssd130x.h"

using namespace daisy;
using namespace daisy::seed;
using namespace daisysp;

// ─── Hardware & MIDI ─────────────────────────────────
DaisySeed            hw;
MidiUsbHandler       midi;

// ─── OLED  ──────────────────────────────────────────────────
OledDisplay<SSD130xI2c128x64Driver> display;

// ─── Synth Parts ────────────────────────────────────────
Oscillator           osc;      // primary saw
Oscillator           osc2;     // detuned saw
Svf                  filter;
AdEnv                env;

// ─── Display state ──────────────────────────────────────
float display_cutoff  = 0.f;
float display_q       = 0.f;
float display_env_amt = 0.f;

// ─── Real‑Time Params ────────────────────────────────────
int   current_note   = -1;
float cutoff         = 0.f;
float q              = 0.f;
float env_mod_amount = 0.f;
float detune_cents   = 0.f;

// ─── Smoothed Display Ints ──────────────────────────────
static int  last_cut_i   = -1;
static int  last_env_i   = -1;
static int  last_q_int   = -1;
static int  last_note    = -2;

// ─── MIDI Note Names ────────────────────────────────────
static const char* noteNames[12] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

// ─── Pot smoothing & detune ─────────────────────────────
static float last_cutoff_pot = -1.f;
static float last_q_pot      = -1.f;
static float last_env_pot    = -1.f;
static float last_det_pot    = -1.f;
static float detune_ratio    = 1.f;
static float last_mod_cutoff = -1.f;
static float last_q          = -1.f;

// ─── Envelope Stages ───────────────────────────────────
enum EnvStage { ENV_IDLE, ENV_ATTACK, ENV_HOLD, ENV_DECAY };
static EnvStage current_stage = ENV_IDLE;
static float    env_out       = 0.f;

// ─── Audio callback ─────────────────────────────────────
void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size)
{
    // Read knobs once per block
    float cutoff_pot = hw.adc.GetFloat(0);
    float q_pot      = hw.adc.GetFloat(1);
    float env_amt    = hw.adc.GetFloat(2);
    float det_pot    = hw.adc.GetFloat(3);

    if(cutoff_pot != last_cutoff_pot) {
        last_cutoff_pot = cutoff_pot;
        cutoff = 40.f * powf(10000.f/40.f, cutoff_pot);
    }
    if(q_pot != last_q_pot) {
        last_q_pot = q_pot;
        q = fmap(q_pot, 0.1f, 1.5f);
    }
    if(env_amt != last_env_pot) {
        last_env_pot = env_amt;
        env_mod_amount = fmap(env_amt, 0.f, 4000.f);
    }
    if(det_pot != last_det_pot) {
        last_det_pot = det_pot;
        detune_cents = fmap(det_pot, -50.f, 50.f);
        detune_ratio = powf(2.f, detune_cents / 1200.f);
    }

    // Apply envelope-modulated cutoff
    float mod_cutoff = cutoff + env_out * env_mod_amount;
    mod_cutoff = fclamp(mod_cutoff, 20.f, 18000.f);
    if(mod_cutoff != last_mod_cutoff || q != last_q) {
        filter.SetFreq(mod_cutoff);
        filter.SetRes(q);
        last_mod_cutoff = mod_cutoff;
        last_q = q;
    }

    // Set oscillator freqs
    float midi_base = 36.f;
    int note_for_hz = (current_note >= 0) ? current_note : int(midi_base);
    float base_hz   = mtof(note_for_hz);
    osc .SetFreq(base_hz);
    osc2.SetFreq(base_hz * detune_ratio);

    // Envelope processing
    switch(current_stage) {
        case ENV_IDLE:
            env_out = 0.f;
            break;
        case ENV_ATTACK:
            env_out = env.Process();
            if(env_out >= 0.99f) { env_out = 1.f; current_stage = ENV_HOLD; }
            break;
        case ENV_HOLD:
            env_out = 1.f;
            break;
        case ENV_DECAY:
            env_out = env.Process();
            if(env_out <= 0.01f) {
                env_out = 0.f;
                current_stage = ENV_IDLE;
                current_note = -1;
            }
            break;
    }

    // Inner sample loop
    for(size_t i = 0; i < size; i += 2) {
        osc .SetAmp(env_out);
        osc2.SetAmp(env_out);
        float mixed = 0.5f * (osc.Process() + osc2.Process());
        filter.Process(mixed);
        float filtered = filter.Low();
        out[i]     = filtered;
        out[i + 1] = filtered;
    }
}

// ─── OLED update ─────────────────────────────────────────
void UpdateDisplay()
{
    char buf[32], noteBuf[16];

    display.Fill(false);
    display.SetCursor(0, 0);
    if(current_note >= 0) {
        int idx    = current_note % 12;
        int octave = (current_note / 12) - 1;
        std::snprintf(noteBuf, sizeof(noteBuf), "%s%d", noteNames[idx], octave);
    } else {
        std::snprintf(noteBuf, sizeof(noteBuf), "---");
    }
    std::snprintf(buf, sizeof(buf), "Note: %s", noteBuf);
    display.WriteString(buf, Font_11x18, true);

    int cut_i = int(display_cutoff + 0.5f);
    display.SetCursor(0, 20);
    std::snprintf(buf, sizeof(buf), "Cutoff: %d Hz", cut_i);
    display.WriteString(buf, Font_6x8, true);

    int q_int   = int(display_q * 100.0f + 0.5f);
    int q_whole = q_int / 100;
    int q_frac  = q_int % 100;
    display.SetCursor(0, 30);
    std::snprintf(buf, sizeof(buf), "Res: %d.%02d", q_whole, q_frac);
    display.WriteString(buf, Font_6x8, true);

    int env_i = int(display_env_amt + 0.5f);
    display.SetCursor(0, 40);
    std::snprintf(buf, sizeof(buf), "Filt Env: %d", env_i);
    display.WriteString(buf, Font_6x8, true);

    display.Update();
}

int main()
{
    hw.Configure(); hw.Init();

    // OLED configuration
    OledDisplay<SSD130xI2c128x64Driver>::Config display_config;
    display_config.driver_config.transport_config.i2c_config.periph = I2CHandle::Config::Peripheral::I2C_1;
    display_config.driver_config.transport_config.i2c_config.pin_config.scl = hw.GetPin(11);
    display_config.driver_config.transport_config.i2c_config.pin_config.sda = hw.GetPin(12);
    display_config.driver_config.transport_config.i2c_config.speed = I2CHandle::Config::Speed::I2C_400KHZ;

    display.Init({});
    display.Fill(false);
    display.WriteString("OLED Ready", Font_6x8, true);
    display.Update();

    hw.SetAudioBlockSize(128);
    midi.Init({});
    AdcChannelConfig adc_cfg[4];
    for(int i=0;i<4;i++) adc_cfg[i].InitSingle(hw.GetPin(15+i));
    hw.adc.Init(adc_cfg, 4);
    hw.adc.Start();

    float sr = hw.AudioSampleRate();
    osc.Init(sr); osc.SetWaveform(Oscillator::WAVE_SAW); osc.SetAmp(1.f);
    osc2.Init(sr); osc2.SetWaveform(Oscillator::WAVE_SAW); osc2.SetAmp(1.f);
    filter.Init(sr); filter.SetRes(0.5f); filter.SetFreq(1000.f);
    env.Init(sr); env.SetTime(ADENV_SEG_ATTACK, 0.01f); env.SetTime(ADENV_SEG_DECAY, 0.1f);
    env.SetMin(0.f); env.SetMax(1.f); env.SetCurve(-50);

    hw.StartAudio(AudioCallback);

    uint32_t frame_counter = 0;
    while(1) {
        midi.Listen();
        while(midi.HasEvents()) {
            auto msg = midi.PopEvent();
            if(msg.type==NoteOn && msg.AsNoteOn().velocity) {
                current_note = msg.AsNoteOn().note;
                env.Trigger(); current_stage = ENV_ATTACK;
            } else if(msg.type==NoteOff) {
                current_stage = ENV_DECAY;
            }
        }

        if(++frame_counter >= 1000) {
            frame_counter = 0;
            int cut_i = ((int(cutoff+0.5f)+5)/10)*10;
            int env_i = ((int(env_mod_amount+0.5f)+50)/100)*100;
            int q_i   = int(q*100+0.5f);
            if(cut_i!=last_cut_i||env_i!=last_env_i||q_i!=last_q_int||current_note!=last_note) {
                last_cut_i=cut_i; last_env_i=env_i; last_q_int=q_i; last_note=current_note;
                display_cutoff  = float(cut_i);
                display_env_amt = float(env_i);
                display_q       = float(q_i)/100.f;
                UpdateDisplay();
            }
        }
    }
}