#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

// Global objects
DaisySeed hw;
MidiUsbHandler midi;

Oscillator osc;
Svf       filter;
AdEnv     env;
Switch    button1;

// Envelope Stages
enum EnvStage { ENV_IDLE, ENV_ATTACK, ENV_HOLD, ENV_DECAY };
EnvStage current_stage = ENV_IDLE;
float env_out = 0.0f;

// Note handling
int current_note = -1;

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size)
{
    float osc_out, filtered;

    // Read knobs
    float cutoff_pot 	= hw.adc.GetFloat(0); // A0
    float q_pot  		= hw.adc.GetFloat(1); // A1
	float env_amt 		= hw.adc.GetFloat(2); // A3
    
	// Map
	float base_cutoff    = fmap(cutoff_pot, 20.f, 10000.f);
    float q      	= fmap(q_pot, 0.1f, 1.5f);
	float depth 	= fmap(env_amt, 0.f, 4000.f); // max mod depth (Hz)

	// Compute final cutoff
	float mod_cutoff = base_cutoff + (env_out * depth);
	mod_cutoff = fclamp(mod_cutoff, 20.f, 18000.f); // clamp
	
	float midi_base  = 36.0f;

    if (current_note >= 0)
        osc.SetFreq(mtof(current_note));
    else
        osc.SetFreq(mtof(midi_base)); // fallback

    filter.SetFreq(mod_cutoff);
	filter.SetRes(q);

    // A-H-D envelope
    switch (current_stage)
    {
        case ENV_IDLE:
            env_out = 0.f;
            break;
        case ENV_ATTACK:
            env_out = env.Process();
            if(env_out >= 0.99f)
            {
                env_out = 1.f;
                current_stage = ENV_HOLD;
            }
            break;
        case ENV_HOLD:
            env_out = 1.f;
            break;
        case ENV_DECAY:
            env_out = env.Process();
            if(env_out <= 0.01f)
            {
                env_out = 0.f;
                current_stage = ENV_IDLE;
                current_note = -1;
            }
            break;
    }

    for(size_t i = 0; i < size; i += 2)
    {
        osc.SetAmp(env_out);
        osc_out = osc.Process();
        filter.Process(osc_out);
        filtered = filter.Low();
        out[i]     = filtered;
        out[i + 1] = filtered;
    }
}


int main(void)
{
    hw.Configure();
    hw.Init();
    hw.SetAudioBlockSize(4);

    // MIDI Init
    MidiUsbHandler::Config midi_cfg;
    midi_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    midi.Init(midi_cfg);

    // ADC Setup
    AdcChannelConfig adc_cfg[2];
    adc_cfg[0].InitSingle(hw.GetPin(15)); // A0: cutoff
    adc_cfg[1].InitSingle(hw.GetPin(16)); // A1: Q / reso
	adc_cfg[2].InitSingle(hw.GetPin(17)); // A2: Filter Env Depth

    hw.adc.Init(adc_cfg, 3);
    hw.adc.Start();

    // Modules
    float sr = hw.AudioSampleRate();
    osc.Init(sr);
    osc.SetWaveform(Oscillator::WAVE_SAW);
    osc.SetAmp(1.f);
    osc.SetFreq(220.f);

    filter.Init(sr);
    filter.SetRes(0.5f);
    filter.SetFreq(1000.f);

    env.Init(sr);
    env.SetTime(ADENV_SEG_ATTACK, 0.01f);
    env.SetTime(ADENV_SEG_DECAY, 0.25f);
    env.SetMin(0.f);
    env.SetMax(1.f);
    env.SetCurve(0);

    hw.StartAudio(AudioCallback);

    while(1)
    {
        midi.Listen();
        while(midi.HasEvents())
        {
            auto msg = midi.PopEvent();
            switch(msg.type)
            {
                case NoteOn:
                {
                    auto note_msg = msg.AsNoteOn();
                    if(note_msg.velocity != 0)
                    {
                        current_note = note_msg.note;
                        env.Trigger();
                        current_stage = ENV_ATTACK;
                    }
                    else
                    {
                        // Treat NoteOn with velocity 0 as NoteOff
                        current_stage = ENV_DECAY;
                    }
                    break;
                }
                case NoteOff:
                {
                    current_stage = ENV_DECAY;
                    break;
                }
                default: break;
            }
        }
    }
}
