// Renders demo WAVs from a synthetic plucked-string DI through the model.
// Usage: render_demo <out.wav> <dirty 0/1> <gain> <freq> <height> <bw> [inputPeakVolts]
#include "../src/dsp/PdfCircuit.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <vector>
#include <random>
#include <chrono>

static std::vector<double> pluckDI (double fs, double peak)
{
    // simple Karplus-Strong riff, E2-A2-D3 power-chord-ish notes
    const double notes[] = { 82.41, 82.41, 110.0, 98.0, 82.41, 123.47, 110.0, 82.41 };
    const double dur = 0.5;
    std::vector<double> out ((size_t) (fs * dur * 8 + fs), 0.0);
    std::mt19937 rng (1);
    std::uniform_real_distribution<double> u (-1, 1);
    for (int k = 0; k < 8; ++k)
        for (double mult : { 1.0, 1.4983 })
        {
            const int L = (int) (fs / (notes[k] * mult));
            std::vector<double> buf ((size_t) L);
            for (auto& b : buf) b = u (rng);
            const size_t start = (size_t) (k * dur * fs);
            double prev = 0;
            for (size_t n = 0; n < (size_t) (fs * dur * 1.3) && start + n < out.size(); ++n)
            {
                const size_t i = n % (size_t) L;
                const double y = buf[i];
                buf[i] = 0.996 * 0.5 * (y + prev); prev = y;
                out[start + n] += y * (n < (size_t) (fs * dur) ? 1.0 : 0.2);
            }
        }
    double m = 0; for (double v : out) m = std::max (m, std::abs (v));
    for (auto& v : out) v *= peak / m;
    return out;
}

static void writeWav (const char* path, const std::vector<float>& d, int fs)
{
    FILE* f = std::fopen (path, "wb");
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + (uint32_t) d.size() * 2); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (1); w32 ((uint32_t) fs); w32 ((uint32_t) fs * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 ((uint32_t) d.size() * 2);
    for (float v : d) { const int16_t s = (int16_t) std::lround (std::clamp (v, -1.f, 1.f) * 32767); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    if (argc < 7) return 1;
    pf::Params p;
    p.dirty = std::atoi (argv[2]) != 0;
    p.gain = std::atof (argv[3]); p.freq = std::atof (argv[4]); p.height = std::atof (argv[5]); p.bandwidth = std::atoi (argv[6]);
    const double peak = argc > 7 ? std::atof (argv[7]) : 0.3;
    const double fs = 192000.0;
    auto in = pluckDI (fs, peak);
    pf::PdfCircuit c; c.prepare (fs); c.setParams (p);
    for (int n = 0; n < (int) fs; ++n) c.process (0.0); // settle
    std::vector<double> y (in.size());
    auto t0 = std::chrono::steady_clock::now();
    double mx = 0; bool bad = false;
    for (size_t n = 0; n < in.size(); ++n) { y[n] = c.process (in[n]); if (! std::isfinite (y[n])) bad = true; mx = std::max (mx, std::abs (y[n])); }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    // crude 4:1 decimation with a windowed-sinc low-pass
    const int taps = 127; std::vector<double> h (taps);
    for (int k = 0; k < taps; ++k) { const double m = k - (taps - 1) / 2.0; const double fc = 0.22;
        h[(size_t) k] = (m == 0 ? 2 * fc : std::sin (2 * M_PI * fc * m) / (M_PI * m)) * (0.42 - 0.5 * std::cos (2 * M_PI * k / (taps - 1)) + 0.08 * std::cos (4 * M_PI * k / (taps - 1))); }
    std::vector<float> out;
    for (size_t n = taps; n < y.size(); n += 4) { double s = 0; for (int k = 0; k < taps; ++k) s += h[(size_t) k] * y[n - (size_t) k]; out.push_back ((float) (s / std::max (mx, 1e-9) * 0.7)); }
    writeWav (argv[1], out, 48000);
    std::printf ("%s: peak out %.3f V (%.1f dBFS @1V), finite=%d, realtime x%.1f (per channel at 4x OS of 48k)\n",
                 argv[1], mx, 20 * std::log10 (mx), ! bad, (in.size() / fs) / secs);
    return bad ? 2 : 0;
}
