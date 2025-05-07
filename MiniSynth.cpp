#include "daisy_seed.h"
#include "daisysp.h"
#include "dev/oled_ssd130x.h"
#include "PolyBleP_Saw.h"

using namespace daisy;
using namespace daisy::seed;
using namespace daisysp;

// Core hardware components
DaisySeed            hw;
MidiUsbHandler       midi;
OledDisplay<SSD130xI2c128x64Driver> display;

// OSC2 Switch
Switch 	osc2Switch;
bool	osc2Enabled = false; 

// Synth components
PolyBleP_Saw		saw1, saw2;
Svf            filter;
AdEnv               env;

// State variables
int     current_note = -1;
float   cutoff = 0.f, q = 0.f, env_mod_amount = 0.f, detune_cents = 0.f;
float   env_out = 0.f;
float   detune_ratio = 1.f;

// Display variables - updated only when values change significantly
int     last_cut_i = -1, last_env_i = -1, last_q_int = -1, last_note = -2;
float   display_cutoff = 0.f, display_q = 0.f, display_env_amt = 0.f;
bool    last_osc2_state = true;

// Envelope state
enum EnvStage { ENV_IDLE, ENV_ATTACK, ENV_HOLD, ENV_DECAY };
static EnvStage current_stage = ENV_IDLE;

// MIDI note name lookup
static const char* noteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                  AudioHandle::InterleavingOutputBuffer out,
                  size_t size)
{
    // Read knobs once per block and update only if changed
    float pot_vals[4];
    for(int i = 0; i < 4; i++) {
        pot_vals[i] = hw.adc.GetFloat(i);
    }
    
    // Update parameters only if knobs have moved significantly
    static float last_pots[4] = {-1.f, -1.f, -1.f, -1.f};
    static float last_mod_cutoff = -1.f, last_q_val = -1.f;
    
    // Cutoff frequency
    if(fabsf(pot_vals[0] - last_pots[0]) > 0.005f) {
        last_pots[0] = pot_vals[0];
        cutoff = 20.f * powf(20000.f/20.f, pot_vals[0]);
    }
    
    // Resonance
    if(fabsf(pot_vals[1] - last_pots[1]) > 0.005f) {
        last_pots[1] = pot_vals[1];
        q = fmap(pot_vals[1], 0.0f, 1.0f);
    }
    
    // Envelope modulation amount
    if(fabsf(pot_vals[2] - last_pots[2]) > 0.005f) {
        last_pots[2] = pot_vals[2];
        env_mod_amount = fmap(pot_vals[2], 0.f, 15000.f);
    }
    
    // Oscillator detune
    if(fabsf(pot_vals[3] - last_pots[3]) > 0.005f) {
        last_pots[3] = pot_vals[3];
        detune_cents = fmap(pot_vals[3], -50.f, 50.f);
        detune_ratio = powf(2.f, detune_cents / 1200.f);
    }

    // Apply envelope-modulated cutoff (with clamping)
    float mod_cutoff = fclamp(cutoff + env_out * env_mod_amount, 20.f, 18000.f);
    
    // Update filter parameters only when necessary
    if(fabsf(mod_cutoff - last_mod_cutoff) > 1.f || fabsf(q - last_q_val) > 0.01f) {
        filter.SetFreq(mod_cutoff);
        filter.SetRes(q);
        last_mod_cutoff = mod_cutoff;
        last_q_val = q;
    }

    // Set oscillator frequencies
    if(current_note >= 0) {
        float base_hz = mtof(current_note);
        saw1.SetFreq(base_hz);
        saw2.SetFreq(base_hz * detune_ratio);
    }

    // Process envelope
    switch(current_stage) {
        case ENV_IDLE:
            env_out = 0.f;
            break;
        case ENV_ATTACK:
            env_out = env.Process();
            if(env_out >= 0.99f) { 
                env_out = 1.f; 
                current_stage = ENV_HOLD; 
            }
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

    // Set amplitudes based on envelope
    saw1.SetAmp(env_out);
    saw2.SetAmp(env_out);

	// Process audio
	for (size_t i = 0; i < size; i += 2) {
		// Mix oscillators - include saw2 only if enabled
		float mixed;
		if (osc2Enabled) {
			mixed = 0.5f * (saw1.Process() + saw2.Process());
		} else {
			mixed = saw1.Process();
		}
		
		// apply 4th‑order LP:
		filter.Process(mixed);
		float filtered = filter.Low();
		// write to both channels
		out[i]     = filtered;
		out[i + 1] = filtered;
	}
}

// Update display only when values change significantly
void UpdateDisplay()
{
    char buf[32];
    
    display.Fill(false);
    
    // Note display
    display.SetCursor(0, 0);
    if(current_note >= 0) {
        int idx = current_note % 12;
        int octave = (current_note / 12) - 1;
        snprintf(buf, sizeof(buf), "Note: %s%d", noteNames[idx], octave);
    } else {
        snprintf(buf, sizeof(buf), "Note: ---");
    }
    display.WriteString(buf, Font_11x18, true);

    // Parameter displays
    display.SetCursor(0, 20);
    snprintf(buf, sizeof(buf), "Cutoff: %d Hz", int(display_cutoff + 0.5f));
    display.WriteString(buf, Font_6x8, true);

    int q_int = int(display_q * 100.0f + 0.5f);
    display.SetCursor(0, 30);
    snprintf(buf, sizeof(buf), "Res: %d.%02d", q_int / 100, q_int % 100);
    display.WriteString(buf, Font_6x8, true);

    display.SetCursor(0, 40);
    snprintf(buf, sizeof(buf), "Filt Env: %d", int(display_env_amt + 0.5f));
    display.WriteString(buf, Font_6x8, true);

    // Display OSC2 status
    display.SetCursor(0, 50);
    snprintf(buf, sizeof(buf), "OSC2: %s", osc2Enabled ? "ON" : "OFF");
    display.WriteString(buf, Font_6x8, true);

    display.Update();
}

void HandleMidi() {
    midi.Listen();
    while(midi.HasEvents()) {
        auto msg = midi.PopEvent();
        if(msg.type == NoteOn && msg.AsNoteOn().velocity) {
            current_note = msg.AsNoteOn().note;
            env.Trigger();
            current_stage = ENV_ATTACK;
        } else if(msg.type == NoteOff && msg.AsNoteOff().note == current_note) {
            current_stage = ENV_DECAY;
        }
    }
}

void InitializeHardware() {
    hw.Configure();
    hw.Init();
    hw.SetAudioBlockSize(128);
    
    // Initialize ADC
    AdcChannelConfig adc_cfg[4];
    for(int i = 0; i < 4; i++) {
        adc_cfg[i].InitSingle(hw.GetPin(15 + i));
    }
    hw.adc.Init(adc_cfg, 4);
    hw.adc.Start();
    
    // Initialize MIDI
    midi.Init({});

    // Initialize OSC2 Switch
    osc2Switch.Init(hw.GetPin(20), 100);
    
    // Initialize OLED
    OledDisplay<SSD130xI2c128x64Driver>::Config display_config;
    display_config.driver_config.transport_config.i2c_config.periph = I2CHandle::Config::Peripheral::I2C_1;
    display_config.driver_config.transport_config.i2c_config.pin_config.scl = hw.GetPin(11);
    display_config.driver_config.transport_config.i2c_config.pin_config.sda = hw.GetPin(12);
    display_config.driver_config.transport_config.i2c_config.speed = I2CHandle::Config::Speed::I2C_400KHZ;
    display.Init(display_config);
    
    // Startup display
    display.Fill(false);
    display.SetCursor(0, 0);
    display.WriteString("Synth Ready", Font_11x18, true);
    display.Update();
}

void InitializeSynth() {
    float sr = hw.AudioSampleRate();
    
    // Initialize oscillators
    saw1.Init(sr);
    saw2.Init(sr);
    
    // Initialize filter
    filter.Init(sr);
    filter.SetRes(0.5f);
    filter.SetFreq(1000.f);
    
    // Initialize envelope
    env.Init(sr);
    env.SetTime(ADENV_SEG_ATTACK, 0.01f);
    env.SetTime(ADENV_SEG_DECAY, 0.1f);
    env.SetMin(0.f);
    env.SetMax(1.f);
    env.SetCurve(-50);
}

int main() {
    // Setup hardware and synth components
    InitializeHardware();
    InitializeSynth();
    
    // Start audio
    hw.StartAudio(AudioCallback);
    
    // Main loop
    uint32_t frame_counter = 0;
    while(1) {
        // Process MIDI messages
        HandleMidi();
        
        // Read the OSC2 switch state
        osc2Switch.Debounce();
        if (osc2Switch.RisingEdge()) {
            osc2Enabled = !osc2Enabled;
        }
        
        // Update display
        if(++frame_counter >= 2000) {
            frame_counter = 0;
            
            // Round values to avoid unnecessary display updates
            int cut_i = ((int(cutoff + 0.5f) + 5) / 10) * 10;
            int env_i = ((int(env_mod_amount + 0.5f)) / 10) * 10;
            int q_i = int(q * 100 + 0.5f);
            
            // Only update if values changed significantly
            if(cut_i != last_cut_i || env_i != last_env_i || 
               q_i != last_q_int || current_note != last_note || 
               osc2Enabled != last_osc2_state) {
                
                last_cut_i = cut_i;
                last_env_i = env_i; 
                last_q_int = q_i;
                last_note = current_note;
                last_osc2_state = osc2Enabled;
                
                display_cutoff = float(cut_i);
                display_env_amt = float(env_i);
                display_q = float(q_i) / 100.f;
                
                UpdateDisplay();
            }
        }
    }
}