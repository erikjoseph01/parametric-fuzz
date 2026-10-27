// Measures the small-signal (clean channel) response of the circuit model with
// sine probes, and prints dB values for comparison with the reference AC simulation.
// Usage: test_response <height> <freqKnob> <bandwidth> <hz> [<hz> ...]
#include "../src/dsp/PdfCircuit.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

int main (int argc, char** argv)
{
    if (argc < 5) return 1;
    pf::Params p;
    p.dirty = false;
    p.height = std::atof (argv[1]);
    p.freq = std::atof (argv[2]);
    p.bandwidth = std::atoi (argv[3]);
    const double fs = 192000.0;
    for (int a = 4; a < argc; ++a)
    {
        const double hz = std::atof (argv[a]);
        pf::PdfCircuit c;
        c.prepare (fs);
        c.setParams (p);
        const double amp = 0.001;
        const int settle = (int) (fs * 1.5), meas = (int) (fs * 0.5);
        double si = 0, co = 0;
        for (int n = 0; n < settle + meas; ++n)
        {
            const double ph = 2 * M_PI * hz * n / fs;
            const double y = c.process (amp * std::sin (ph));
            if (n >= settle) { si += y * std::sin (ph); co += y * std::cos (ph); }
        }
        const double mag = 2.0 * std::sqrt (si * si + co * co) / meas / amp;
        std::printf ("%g %.3f\n", hz, 20 * std::log10 (mag));
    }
    return 0;
}
