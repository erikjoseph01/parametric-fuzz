// Small linear circuit solver: modified nodal analysis with trapezoidal
// capacitor companions and ideal (nullor) op-amps. The system matrix is
// inverted once whenever a component value changes; each sample is then a
// cheap matrix-vector product.
#pragma once
#include <vector>
#include <cmath>
#include <cassert>

namespace pf {

class LinearMNA
{
public:
    static constexpr int GND = -1;

    int node() { return nNodes++; }

    int R (int a, int b, double ohms)  { els.push_back ({ kR, a, b, -1, ohms }); return (int) els.size() - 1; }
    int C (int a, int b, double farads) { caps.push_back (Cap { a, b, farads, 0, 0, 0, 0, {} }); return (int) caps.size() - 1; }
    // Ideal voltage source from ground to node a; returns source index.
    int V (int a) { srcs.push_back (a); return (int) srcs.size() - 1; }
    // Ideal op-amp: output o forces v(p) == v(m).
    void OA (int p, int m, int o) { oas.push_back ({ p, m, o }); }

    void setR (int idx, double ohms) { if (els[(size_t) idx].val != ohms) { els[(size_t) idx].val = ohms; dirty = true; } }

    void prepare (double sampleRate)
    {
        T = 1.0 / sampleRate;
        size = nNodes + (int) srcs.size() + (int) oas.size();
        x.assign ((size_t) size, 0.0);
        srcVal.assign (srcs.size(), 0.0);
        for (auto& c : caps) { c.v = 0; c.i = 0; }
        dirty = true;
        rebuild();
    }

    void reset() { for (auto& c : caps) { c.v = 0; c.i = 0; } std::fill (x.begin(), x.end(), 0.0); }

    void setSource (int idx, double v) { srcVal[(size_t) idx] = v; }

    void rebuildIfNeeded() { if (dirty) rebuild(); }

    void step()
    {
        // x = Minv * b, where b holds source values and capacitor history currents
        std::fill (x.begin(), x.end(), 0.0);
        for (size_t s = 0; s < srcs.size(); ++s)
            axpy (srcVal[s], srcCol[s]);
        for (auto& c : caps)
        {
            c.h = c.G * c.v + c.i;
            axpy (c.h, c.col);
        }
        for (auto& c : caps)
        {
            const double v = volt (c.a) - volt (c.b);
            c.i = c.G * v - c.h;
            c.v = v;
        }
    }

    double volt (int n) const { return n < 0 ? 0.0 : x[(size_t) n]; }

private:
    enum { kR = 0 };
    struct Elem { int type, a, b, c; double val; };
    struct Cap { int a, b; double Cval; double v = 0, i = 0, h = 0, G = 0; std::vector<double> col; };
    struct Oa { int p, m, o; };

    std::vector<Elem> els;
    std::vector<Cap> caps;
    std::vector<int> srcs;
    std::vector<Oa> oas;
    std::vector<std::vector<double>> srcCol;
    std::vector<double> x, srcVal;
    int nNodes = 0, size = 0;
    double T = 1.0 / 48000.0;
    bool dirty = true;

    void axpy (double a, const std::vector<double>& col)
    {
        if (a == 0.0) return;
        for (int r = 0; r < nNodes; ++r) x[(size_t) r] += a * col[(size_t) r];
    }

    void rebuild()
    {
        const int M = size;
        std::vector<double> A ((size_t) (M * M), 0.0);
        auto at = [&] (int r, int c) -> double& { return A[(size_t) (r * M + c)]; };
        auto stamp = [&] (int a, int b, double g)
        {
            if (a >= 0) at (a, a) += g;
            if (b >= 0) at (b, b) += g;
            if (a >= 0 && b >= 0) { at (a, b) -= g; at (b, a) -= g; }
        };
        for (auto& e : els) stamp (e.a, e.b, 1.0 / e.val);
        for (auto& c : caps) { c.G = 2.0 * c.Cval / T; stamp (c.a, c.b, c.G); }
        int k = nNodes;
        for (int s : srcs) { at (s, k) += 1; at (k, s) += 1; ++k; }
        for (auto& o : oas)
        {
            at (o.o, k) += 1;
            if (o.p >= 0) at (k, o.p) += 1;
            if (o.m >= 0) at (k, o.m) -= 1;
            ++k;
        }

        // Gauss-Jordan inversion with partial pivoting
        std::vector<double> inv ((size_t) (M * M), 0.0);
        for (int i = 0; i < M; ++i) inv[(size_t) (i * M + i)] = 1.0;
        for (int col = 0; col < M; ++col)
        {
            int piv = col;
            for (int r = col + 1; r < M; ++r)
                if (std::abs (at (r, col)) > std::abs (at (piv, col))) piv = r;
            if (piv != col)
                for (int c = 0; c < M; ++c)
                {
                    std::swap (at (col, c), at (piv, c));
                    std::swap (inv[(size_t) (col * M + c)], inv[(size_t) (piv * M + c)]);
                }
            const double d = at (col, col);
            assert (std::abs (d) > 1e-300);
            for (int c = 0; c < M; ++c) { at (col, c) /= d; inv[(size_t) (col * M + c)] /= d; }
            for (int r = 0; r < M; ++r)
            {
                if (r == col) continue;
                const double f = at (r, col);
                if (f == 0.0) continue;
                for (int c = 0; c < M; ++c)
                {
                    at (r, c) -= f * at (col, c);
                    inv[(size_t) (r * M + c)] -= f * inv[(size_t) (col * M + c)];
                }
            }
        }
        auto column = [&] (int j) { std::vector<double> v ((size_t) M); for (int r = 0; r < M; ++r) v[(size_t) r] = inv[(size_t) (r * M + j)]; return v; };

        srcCol.clear();
        for (size_t s = 0; s < srcs.size(); ++s) srcCol.push_back (column (nNodes + (int) s));
        for (auto& c : caps)
        {
            c.col.assign ((size_t) M, 0.0);
            if (c.a >= 0) { auto ca = column (c.a); for (int r = 0; r < M; ++r) c.col[(size_t) r] += ca[(size_t) r]; }
            if (c.b >= 0) { auto cb = column (c.b); for (int r = 0; r < M; ++r) c.col[(size_t) r] -= cb[(size_t) r]; }
        }
        dirty = false;
    }
};

} // namespace pf
