#include "daisy_seed.h"
#include "daisysp.h"
#include "dev/oled_ssd130x.h"

using namespace daisy;
using namespace daisy::seed;
using namespace daisysp;

// Global objects
DaisySeed hw;
int current_note = -1;
float cutoff = 0.f, q = 0.f, env_mod_amount = 0.f; // real-time
float display_cutoff = 0.f, display_q = 0.f, display_env_amt = 0.f; // display copies


// OLED
OledDisplay<SSD130xI2c128x64Driver> display;

// MIDI
MidiUsbHandler midi;

// Synth
Oscillator osc;
Svf       filter;
AdEnv     env;
Switch    button1;

// Envelope Stages
enum EnvStage { ENV_IDLE, ENV_ATTACK, ENV_HOLD, ENV_DECAY };
EnvStage current_stage = ENV_IDLE;
float env_out = 0.0f;

// Per Sample
void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size)
{
    float osc_out, filtered;

    // Read knobs
    float cutoff_pot 	= hw.adc.GetFloat(0); 	// A0
    float q_pot  		= hw.adc.GetFloat(1); 	// A1
	float env_amt 		= hw.adc.GetFloat(2); 	// A3
    
	// Map
	cutoff   			= fmap(cutoff_pot, 40.f, 10000.f);
	q      				= fmap(q_pot, 0.1f, 1.5f);
	env_mod_amount		= fmap(env_amt, 0.f, 4000.f); // max mod depth (Hz)

	// Make display-safe copies
	display_cutoff = cutoff;
	display_q = q;
	display_env_amt = env_mod_amount;

	// Compute final cutoff
	float mod_cutoff = cutoff + (env_out * env_mod_amount);
	mod_cutoff = fclamp(mod_cutoff, 20.f, 18000.f);
	
	// Just in case
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

// OLED Updating
static const char* noteNames[12] = {
	"C", "C#", "D", "D#", "E", "F",
	"F#", "G", "G#", "A", "A#", "B"
  };
  
  void UpdateDisplay()
  {

	char buf[32];
	char noteBuf[16];
  
	display.Fill(false);
	display.SetCursor(0,  0);
  
	// ——— Note name instead of number ———
	display.SetCursor(0, 0);
	if(current_note >= 0)
	{
		int idx    = current_note % 12;          // which of the 12 semitones
		int octave = (current_note / 12) - 1;    // MIDI 0→C–1, 60→C4
		std::snprintf(noteBuf, sizeof(noteBuf), "%s%d",
					noteNames[idx], octave);
	}
	else
	{
		std::snprintf(noteBuf, sizeof(noteBuf), "---");
	}
	std::snprintf(buf, sizeof(buf), "Note: %s", noteBuf);
	display.WriteString(buf, Font_11x18, true);

	// ——— Cutoff (int) ———
	int cut_i = int(display_cutoff + 0.5f);
	display.SetCursor(0, 20);
	std::snprintf(buf, sizeof(buf), "Cutoff: %d Hz", cut_i);
	display.WriteString(buf, Font_6x8, true);

	// ——— Q with two decimals ———
	int q_int   = int(display_q * 100.0f + 0.5f);
	int q_whole = q_int / 100;
	int q_frac  = q_int % 100;
	display.SetCursor(0, 30);
	std::snprintf(buf, sizeof(buf), "Q: %d.%02d", q_whole, q_frac);
	display.WriteString(buf, Font_6x8, true);

	// ——— Env→Cut (int) ———
	int env_i = int(display_env_amt + 0.5f);
	display.SetCursor(0, 40);
	std::snprintf(buf, sizeof(buf), "Env->Cut: %d", env_i);
	display.WriteString(buf, Font_6x8, true);

	display.Update();
  }



int main(void)
{
    hw.Configure();
    hw.Init();

    // OLED configuration
    OledDisplay<SSD130xI2c128x64Driver>::Config display_config;

    // Set up I2C peripheral and pins
    display_config.driver_config.transport_config.i2c_config.periph = I2CHandle::Config::Peripheral::I2C_1;
    display_config.driver_config.transport_config.i2c_config.pin_config.scl = hw.GetPin(11);
    display_config.driver_config.transport_config.i2c_config.pin_config.sda = hw.GetPin(12);
    display_config.driver_config.transport_config.i2c_config.speed = I2CHandle::Config::Speed::I2C_400KHZ;

    // Initialize the display
    display.Init(display_config);
    display.Fill(false);
    display.SetCursor(0, 0);
    display.WriteString("OLED Ready", Font_6x8, true);
    display.Update();


    hw.SetAudioBlockSize(4);

    // MIDI Init
    MidiUsbHandler::Config midi_cfg;
    midi_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    midi.Init(midi_cfg);

    // ADC Setup
    AdcChannelConfig adc_cfg[3];
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

	// Counter
	static uint32_t frame_counter = 0;
	
	while(1)
	{
		// MIDI Handling
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
        // Update OLED every 1000 loop cycles
        if(++frame_counter >= 1000)
        {
            frame_counter = 0;

            // Take a snapshot of the real-time values
            display_cutoff   = cutoff;
            display_q        = q;
            display_env_amt  = env_mod_amount;

            // Now safe to display
            UpdateDisplay();
        }
	}
}