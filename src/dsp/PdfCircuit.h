// Circuit model of a Maestro MPF-style parametric distortion filter
// (clean buffer / CD4007 CMOS drive, Moog cut/boost parametric, makeup stage).
// All voltages are small-signal deviations from the ~3.5 V bias unless noted.
// One instance processes one channel at the (oversampled) rate given to prepare().
#pragma once
#include "LinearMNA.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace pf {

struct Params
{
    bool dirty = false;
    double gain = 0.5;     // 0..1 knob rotation (A100k)
    double freq = 0.5;     // 0..1 knob rotation (dual C100k)
    double height = 0.5;   // 0..1 knob rotation (dual B10k), 0.5 = flat
    int bandwidth = 3;     // 1 (widest) .. 5 (narrowest)
};

namespace detail {
    constexpr double VCC = 8.7;                         // 9 V minus 1N5817 drop
    constexpr double VBIAS = VCC * 2.2e6 / (3.3e6 + 2.2e6);
    // TL072 output swing on this supply, relative to the bias point
    constexpr double OA_LO = 1.5 - VBIAS;
    constexpr double OA_HI = 7.3 - VBIAS;

    inline double audioTaper (double x) { x = std::clamp (x, 0.0, 1.0); return (std::pow (10.0, 2.0 * x) - 1.0) / 99.0; }

    // Op-amp output clipping with a small soft knee (volts)
    inline double opampClip (double v, double lo = OA_LO, double hi = OA_HI, double knee = 0.15)
    {
        if (v > hi - knee) return hi - knee + knee * std::tanh ((v - (hi - knee)) / knee);
        if (v < lo + knee) return lo + knee + knee * std::tanh ((v - (lo + knee)) / knee);
        return v;
    }

    // First-order section via bilinear transform: H(s) = (b0 + b1 s) / (a0 + a1 s)
    struct OnePole
    {
        double B0 = 1, B1 = 0, A1 = 0, s = 0;
        void set (double b0, double b1, double a0, double a1, double fs)
        {
            const double K = 2.0 * fs;
            const double n0 = b0 + b1 * K, n1 = b0 - b1 * K, d0 = a0 + a1 * K, d1 = a0 - a1 * K;
            B0 = n0 / d0; B1 = n1 / d0; A1 = d1 / d0;
        }
        // transposed direct form II
        double process (double x) { const double y = B0 * x + s; s = B1 * x - A1 * y; return y; }
        void reset() { s = 0; }
    };

    // CD4007 P-channel device (metal-gate CMOS, square-law model)
    constexpr double BETA = 0.32e-3, VT0 = 1.7, GAMMA = 1.0, PHI2 = 0.6, LAMBDA = 0.02;

    struct PmosI { double id, dVsg, dVsd; };
    inline PmosI pmos (double vsg, double vsd, double vt = VT0)
    {
        const double vov = vsg - vt;
        if (vov <= 0.0 || vsd <= 0.0) return { 0.0, 0.0, 0.0 };
        const double cl = 1.0 + LAMBDA * vsd;
        if (vsd >= vov)
        {
            const double i0 = 0.5 * BETA * vov * vov;
            return { i0 * cl, BETA * vov * cl, i0 * LAMBDA };
        }
        const double i0 = BETA * (vov * vsd - 0.5 * vsd * vsd);
        return { i0 * cl, BETA * vsd * cl, BETA * (vov - vsd) * cl + i0 * LAMBDA };
    }

    // 1N4148 anti-parallel pair
    constexpr double D_IS = 2.52e-9, D_NVT = 1.752 * 0.02585;
}

class PdfCircuit
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        buildFollowerTable();
        buildFilter();
        buildOutput();
        filt.prepare (fs);
        outc.prepare (fs);
        updateLinear (true);
        reset();
    }

    void reset()
    {
        inHpf.reset(); cleanAmp.reset(); gainAmp.reset(); dirtyHpf.reset(); outHpf.reset();
        filt.reset(); outc.reset();
        initStageC();
    }

    void setParams (const Params& p)
    {
        const bool changed = p.gain != prm.gain || p.freq != prm.freq || p.height != prm.height
                          || p.bandwidth != prm.bandwidth;
        prm = p;
        if (changed) updateLinear (false);
    }

    // input: volts at the input jack. returns volts at the output jack.
    double process (double vin)
    {
        const double v = inHpf.process (vin);

        double sel;
        if (prm.dirty)
        {
            const double g = detail::opampClip (gainAmp.process (v));     // IC1.2
            const double vs = follower (detail::VBIAS + g);                // CD4007 pair 1 (source follower)
            const double y = stageC (vs);                                  // CD4007 pair 2 + 1N4148 clipper
            sel = dirtyHpf.process ((y - yBias) * (5.6e3 / 27.6e3));       // R12/R13 divider, C7
        }
        else
        {
            sel = detail::opampClip (cleanAmp.process (v));                // IC1.1
        }

        sel = detail::opampClip (sel);                                     // IC2.2 buffer
        filt.setSource (srcF, sel);
        filt.step();
        const double o1 = detail::opampClip (filt.volt (nO1));            // IC2.1 out

        outc.setSource (srcO, o1);
        outc.step();
        const double o3 = detail::opampClip (outc.volt (nO3));            // IC3.1 out
        return outHpf.process (o3);                                        // R39, C16, R41
    }

private:
    double fs = 192000.0;
    Params prm;

    detail::OnePole inHpf, cleanAmp, gainAmp, dirtyHpf, outHpf;

    // ---------------- parametric filter (IC2.1 + IC4 network) ----------------
    LinearMNA filt;
    int srcF = 0, nO1 = 0, rPW = 0, rWN = 0, rWA = 0, rGH = 0, rKS = 0, rOS = 0;

    void buildFilter()
    {
        filt = LinearMNA();
        auto& c = filt;
        const int vin = c.node(), P = c.node(), N = c.node(), o1 = c.node(), W = c.node(), A = c.node(),
                  G = c.node(), H = c.node(), K = c.node(), o41 = c.node(), S = c.node(), I = c.node(),
                  o42 = c.node(), M = c.node(), J = c.node();
        const int g = LinearMNA::GND;
        srcF = c.V (vin);
        c.R (vin, P, 2200);                 // R18
        c.R (o1, N, 2200);                  // R25
        c.OA (P, N, o1);                    // IC2.1
        rPW = c.R (P, W, 5000);             // HEIGHT.2 (B10k)
        rWN = c.R (W, N, 5000);
        rWA = c.R (W, A, 330);              // bandwidth series R (R16..R23)
        c.R (A, G, 43e3);                   // R20
        c.R (G, H, 1e6);                    // R21
        rGH = c.R (G, H, 20e3);             // bandwidth shunt R (R24..R29)
        c.C (H, g, 10e-6);                  // C8
        c.OA (G, K, o41);                   // IC4.1
        c.C (K, o41, 1e-9);                 // C14
        rKS = c.R (K, S, 1600 + 15e3);      // R32 + FREQ.2 (C100k)
        rOS = c.R (o41, S, 1600 + 15e3);    // R35 + FREQ.1 (C100k)
        c.C (S, g, 390e-9);                 // C11
        c.R (o41, I, 22e3);                 // R37
        c.R (I, o42, 22e3);                 // R42
        c.OA (M, I, o42);                   // IC4.2
        c.R (o42, A, 3300);                 // R44
        c.R (A, M, 15e3);                   // R43
        c.C (M, g, 100e-12);                // C18
        c.R (M, J, 22e3);                   // R40
        c.C (J, g, 10e-6);                  // C17
        nO1 = o1;
    }

    // ---------------- output stage (R28/C9 low-pass, IC3.1 makeup) ----------------
    LinearMNA outc;
    int srcO = 0, nO3 = 0, rFb = 0;

    void buildOutput()
    {
        outc = LinearMNA();
        auto& c = outc;
        const int vin = c.node(), n28 = c.node(), n30 = c.node(), n31 = c.node(), p3 = c.node(),
                  q = c.node(), o3 = c.node(), r = c.node();
        const int g = LinearMNA::GND;
        srcO = c.V (vin);
        c.R (vin, n28, 4700);               // R28
        c.C (n28, g, 33e-9);                // C9
        c.R (n28, n30, 68e3);               // R30
        c.R (n30, n31, 33e3);               // R31
        c.C (n31, g, 10e-6);                // C10
        c.C (n30, p3, 33e-9);               // C12
        c.R (p3, g, 3.3e6 * 2.2e6 / 5.5e6); // R33 || R34
        c.OA (p3, q, o3);                   // IC3.1
        c.C (q, o3, 250e-12);               // C15
        rFb = c.R (q, o3, 330 + 5000);      // R38 + HEIGHT.1 (B10k)
        c.R (q, r, 330);                    // R36
        c.C (r, g, 10e-6);                  // C13
        nO3 = o3;
    }

    void updateLinear (bool force)
    {
        using namespace detail;
        const double h = std::clamp (prm.height, 0.0, 1.0);
        filt.setR (rPW, std::max (10e3 * h, 0.5));
        filt.setR (rWN, std::max (10e3 * (1.0 - h), 0.5));
        static constexpr std::array<double, 5> series { 680, 470, 330, 150, 1 };
        static constexpr std::array<double, 5> shunt  { 1e12, 100e3, 20e3, 10e3, 4.7e3 };
        const int bw = std::clamp (prm.bandwidth, 1, 5) - 1;
        filt.setR (rWA, series[(size_t) bw]);
        filt.setR (rGH, shunt[(size_t) bw]);
        const double rf = 100e3 * audioTaper (1.0 - prm.freq);   // CW = higher frequency
        filt.setR (rKS, 1600 + rf + 1.0);
        filt.setR (rOS, 1600 + rf + 1.0);
        outc.setR (rFb, 330 + 10e3 * (1.0 - h) + 0.5);
        filt.rebuildIfNeeded();
        outc.rebuildIfNeeded();

        // Fixed first-order stages
        inHpf.set (0, 33e-9 * 1.32e6, 1, 33e-9 * 1.32e6, fs);                     // C1 into bias network
        cleanAmp.set (1, (10e3 + 6.2e3) * 10e-6, 1, 10e3 * 10e-6, fs);           // IC1.1: 1 + R6/(R4 + 1/sC2)
        const double rg = 100e3 * audioTaper (prm.gain) + 1.0;
        gainAmp.set (1, (2.7e3 + rg) * 10e-6, 1, 2.7e3 * 10e-6, fs);             // IC1.2: 1 + Rg/(R5 + 1/sC3)
        dirtyHpf.set (0, 100e-9 * 1.32e6, 1, 100e-9 * 1.325e6, fs);               // C7
        outHpf.set (0, 100e3 * 10e-6, 1, 110e3 * 10e-6, fs);                      // R39, C16, R41
        (void) force;
    }

    // ---------------- CD4007 pair 1: P-channel source follower ----------------
    // Drain to ground, source to VCC via R8 (33k), bulk at VCC (body effect).
    static constexpr int kTab = 4096;
    double tabLo = 0, tabStep = 1;
    std::vector<double> tab;

    void buildFollowerTable()
    {
        using namespace detail;
        tabLo = -0.7; const double hi = VCC + 0.7;
        tabStep = (hi - tabLo) / (kTab - 1);
        tab.resize (kTab);
        for (int k = 0; k < kTab; ++k)
        {
            const double vg = tabLo + k * tabStep;
            auto f = [&] (double vs)
            {
                const double vsb = std::max (VCC - vs, 0.0);
                const double vt = VT0 + GAMMA * (std::sqrt (PHI2 + vsb) - std::sqrt (PHI2));
                const double vov = vs - vg - vt;
                const double id = vov > 0 ? 0.5 * BETA * vov * vov : 0.0;
                return (VCC - vs) / 33e3 - id;
            };
            double lo = std::min (vg, VCC), hi2 = VCC;
            for (int it = 0; it < 80; ++it) { const double m = 0.5 * (lo + hi2); (f (m) > 0 ? lo : hi2) = m; }
            tab[(size_t) k] = 0.5 * (lo + hi2);
        }
    }

    double follower (double vg) const
    {
        double pos = (vg - tabLo) / tabStep;
        pos = std::clamp (pos, 0.0, (double) kTab - 1.001);
        const int i = (int) pos; const double fr = pos - i;
        return tab[(size_t) i] + fr * (tab[(size_t) i + 1] - tab[(size_t) i]);
    }

    // ---------------- CD4007 pair 2: P-channel common source + diode clipper ----------------
    // Unknowns: X (gate), Y (drain), Vd (R11/diode node), E (diode/C6 node). Absolute volts.
    double X = 0, Y = 0, Vd = 0, E = 0, yBias = 0;
    double c4v = 0, c4i = 0, c6v = 0, c6i = 0;
    static constexpr double R9 = 2.2e6, R10 = 4.7e6, R11 = 2200, RL = 27.6e3;

    void initStageC()
    {
        using namespace detail;
        auto xOf = [] (double y) { return (VCC / R9 + y / R10) / (1.0 / R9 + 1.0 / R10); };
        double lo = 0, hi = VCC;
        for (int it = 0; it < 100; ++it)
        {
            const double y = 0.5 * (lo + hi), x = xOf (y);
            (pmos (VCC - x, VCC - y).id - y / RL > 0 ? lo : hi) = y;
        }
        Y = 0.5 * (lo + hi); X = xOf (Y); Vd = Y; E = Y; yBias = Y;
        const double vs0 = follower (VBIAS);
        c4v = vs0 - X; c4i = 0; c6v = E; c6i = 0;
    }

    double stageC (double vs)
    {
        using namespace detail;
        const double G4 = 2.0 * 33e-9 * fs, G6 = 2.0 * 2.2e-6 * fs;
        const double h4 = G4 * c4v + c4i, h6 = G6 * c6v + c6i;

        double x[4] = { X, Y, Vd, E };
        for (int it = 0; it < 12; ++it)
        {
            const auto m = pmos (VCC - x[0], VCC - x[1]);
            const double vdd = std::clamp (x[2] - x[3], -1.2, 1.2);
            const double ex = std::exp (vdd / D_NVT), emx = 1.0 / ex;
            const double idio = D_IS * (ex - emx), gd = D_IS * (ex + emx) / D_NVT + 1e-12;

            double F[4] = {
                G4 * (vs - x[0]) - h4 + (VCC - x[0]) / R9 + (x[1] - x[0]) / R10,
                m.id + (x[0] - x[1]) / R10 - x[1] / RL - (x[1] - x[2]) / R11,
                (x[1] - x[2]) / R11 - idio,
                idio - (G6 * x[3] - h6)
            };
            double J[4][4] = {
                { -G4 - 1 / R9 - 1 / R10, 1 / R10, 0, 0 },
                { -m.dVsg + 1 / R10, -m.dVsd - 1 / R10 - 1 / RL - 1 / R11, 1 / R11, 0 },
                { 0, 1 / R11, -1 / R11 - gd, gd },
                { 0, 0, gd, -gd - G6 }
            };
            double dx[4];
            if (! solve4 (J, F, dx)) break;
            double maxd = 0;
            for (int k = 0; k < 4; ++k)
            {
                const double d = std::clamp (-dx[k], -0.3, 0.3);
                x[k] += d; maxd = std::max (maxd, std::abs (d));
            }
            if (maxd < 1e-9) break;
        }
        X = x[0]; Y = x[1]; Vd = x[2]; E = x[3];
        c4i = G4 * (vs - X) - h4; c4v = vs - X;
        c6i = G6 * E - h6;        c6v = E;
        return Y;
    }

    static bool solve4 (double J[4][4], const double F[4], double out[4])
    {
        double a[4][5];
        for (int r = 0; r < 4; ++r) { for (int c = 0; c < 4; ++c) a[r][c] = J[r][c]; a[r][4] = F[r]; }
        for (int c = 0; c < 4; ++c)
        {
            int p = c;
            for (int r = c + 1; r < 4; ++r) if (std::abs (a[r][c]) > std::abs (a[p][c])) p = r;
            if (std::abs (a[p][c]) < 1e-300) return false;
            if (p != c) for (int k = 0; k < 5; ++k) std::swap (a[c][k], a[p][k]);
            for (int r = c + 1; r < 4; ++r)
            {
                const double f = a[r][c] / a[c][c];
                for (int k = c; k < 5; ++k) a[r][k] -= f * a[c][k];
            }
        }
        for (int r = 3; r >= 0; --r)
        {
            double s = a[r][4];
            for (int c = r + 1; c < 4; ++c) s -= a[r][c] * out[c];
            out[r] = s / a[r][r];
        }
        return true;
    }
};

} // namespace pf
