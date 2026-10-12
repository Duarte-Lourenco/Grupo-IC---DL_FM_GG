// test_mdct — verifica a MDCT rápida.
//   1. DCT-IV rápida igual à definição direta (O(N^2));
//   2. ortonormalidade: energia dos coeficientes = energia do sinal;
//   3. reconstrução perfeita com sobreposição de 50% (TDAC).
#include "common/mdct.hpp"

#include <cmath>
#include <iostream>
#include <random>
#include <vector>

int main() {
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> dist(-1000.0, 1000.0);
    const double pi = std::acos(-1.0);
    bool ok = true;

    for (int N : {16, 64, 256, 1024, 2048}) {
        Mdct m(N);

        // 1. DCT-IV rápida vs direta
        std::vector<double> u(N), fast(N);
        for (auto& v : u) v = dist(rng);
        m.dct4(u.data(), fast.data());
        double errDct = 0.0;
        for (int k = 0; k < N; ++k) {
            double s = 0.0;
            for (int n = 0; n < N; ++n) s += u[n] * std::cos(pi / N * (n + 0.5) * (k + 0.5));
            s *= std::sqrt(2.0 / N);
            errDct = std::max(errDct, std::abs(s - fast[k]));
        }

        // 2 e 3. sinal de 8 blocos, com zeros no início e no fim
        const int B = 8, L = (B + 1) * N;
        std::vector<double> x(L, 0.0), y(L, 0.0), X(N), tmp(2 * N);
        for (int n = N; n < L - N; ++n) x[n] = dist(rng);
        double eSig = 0.0, eCoef = 0.0;
        for (double v : x) eSig += v * v;
        for (int b = 0; b < B; ++b) {
            m.forward(&x[b * N], X.data());
            for (double v : X) eCoef += v * v;
            m.inverse(X.data(), tmp.data());
            for (int n = 0; n < 2 * N; ++n) y[b * N + n] += tmp[n];
        }
        double errRec = 0.0;
        for (int n = 0; n < L; ++n) errRec = std::max(errRec, std::abs(x[n] - y[n]));
        const double errEnergy = std::abs(eSig - eCoef) / eSig;

        const bool okN = errDct < 1e-6 && errRec < 1e-6 && errEnergy < 1e-9;
        std::cout << "N=" << N << "  erro DCT-IV=" << errDct << "  erro reconstrução=" << errRec
                  << "  erro energia=" << errEnergy << (okN ? "" : "  <-- FALHOU") << "\n";
        ok = ok && okN;
    }
    std::cout << (ok ? "OK: mdct\n" : "FALHOU: mdct\n");
    return ok ? 0 : 1;
}
