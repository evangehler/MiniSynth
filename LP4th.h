#pragma once
#include "BiquadLP.h"

struct LP4th {
    void Init(float sample_rate, float cutoff_hz = 1000.f, float q = 0.707f) {
        section1.Init(sample_rate, cutoff_hz, q);
        section2.Init(sample_rate, cutoff_hz, q);
    }
    void SetFreq(float cutoff_hz) {
        section1.SetFreq(cutoff_hz);
        section2.SetFreq(cutoff_hz);
    }
    void SetRes(float q) {
        section1.SetRes(q);
        section2.SetRes(q);
    }
    float Process(float in) {
        return section2.Process(section1.Process(in));
    }

  private:
    BiquadLP section1, section2;
};
