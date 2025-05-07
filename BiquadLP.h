#pragma once
#include <cmath>

constexpr float PI = 3.14159265358979323846f;

class BiquadLP {
public:
    void Init(float sample_rate, float cutoff_hz = 1000.f, float q = 0.707f) {
        sr     = sample_rate;
        cutoff = cutoff_hz;
        Q      = q;
        z1 = z2 = 0.f;
        updateCoeffs();
    }

    // Cutoff 
    void SetFreq(float cutoff_hz) {
        cutoff = cutoff_hz;
        updateCoeffs();
    }

    // Res
    void SetRes(float q) {
        Q = q;
        updateCoeffs();
    }

    // Process one sample
    float Process(float in) {
        // Direct‑Form II Transposed
        float out = b0*in + z1;
        z1        = b1*in - a1*out + z2;
        z2        = b2*in - a2*out;
        return out;
    }

private:
    // Recompute all a/b coefficients whenever cutoff or Q changes
    void updateCoeffs() {
        float K    = std::tan(PI * cutoff / sr);
        float norm = 1.f / (1.f + K/Q + K*K);
        b0 = K*K * norm;
        b1 = 2.f * b0;
        b2 = b0;
        a1 = 2.f * (K*K - 1.f) * norm;
        a2 = (1.f - K/Q + K*K) * norm;
    }

    // internal state
    float sr        = 48000.f;
    float cutoff    = 1000.f;
    float Q         = 0.707f;
    float b0, b1, b2, a1, a2;
    float z1 = 0.f, z2 = 0.f;
};
