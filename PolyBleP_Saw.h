#pragma once

class PolyBleP_Saw {
public:
    void Init(float sample_rate) {
        sr    = sample_rate;
        phase = 0.f;
        amp   = 1.f; 
    }

    // Frequency in Hz
    void SetFreq(float f) {
        freq = f;
        incr = freq / sr;
    }

    // Amplitude
    void SetAmp(float a) {
        amp = a;
    }

    // Call each sample; returns a band‑limited saw
    float Process() {
        phase += incr;
        if(phase >= 1.f) phase -= 1.f;

        float t     = phase;
        float value = 2.f * t - 1.f;      // naive saw

        // subtract out the discontinuity
        value -= polyBleP(t, incr);

        // apply amplitude
        return value * amp;
    }

private:
    // PolyBLEP correction core (magic)
    float polyBleP(float t, float dt) {
        if(t < dt) {
            float x = t/dt;
            return x + x - x*x - 1.f;
        }
        else if(t > 1.f - dt) {
            float x = (t - 1.f)/dt;
            return x*x + x + x + 1.f;
        }
        return 0.f;
    }

    float sr     = 48000.f;  // sample rate
    float freq   = 440.f;    // current frequency
    float incr   = freq/sr;  // phase increment
    float phase  = 0.f;      // 0…1
    float amp    = 1.f;      // output amplitude
};
