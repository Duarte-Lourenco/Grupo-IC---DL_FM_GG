// metrics.hpp — medidas de erro entre um sinal original e uma versão modificada.
//
// Partilhado pelo wav_cmp e pelo codec com perdas (que mostra o SNR obtido).
//   MSE  = média de (x - y)^2          (norma L2)
//   Linf = máximo de |x - y|           (norma L-infinito)
//   SNR  = 10 * log10( soma(x^2) / soma((x - y)^2) )   em dB
// onde x = original e y = modificado.
#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

// Acumula as estatísticas de um "canal" (L, R ou MID).
struct Stats {
    double sumSig = 0.0;   // soma de x^2       (energia do sinal)
    double sumErr = 0.0;   // soma de (x-y)^2   (energia do erro)
    int64_t maxAbs = 0;    // max |x - y|
    std::size_t n = 0;

    void add(int64_t x, int64_t y) {
        const int64_t e = x - y;
        // double chega: para 16 bits os quadrados cabem sempre; para 32 bits
        // evita-se o overflow de int64 ao somar milhões de amostras.
        sumSig += static_cast<double>(x) * static_cast<double>(x);
        sumErr += static_cast<double>(e) * static_cast<double>(e);
        const int64_t a = e < 0 ? -e : e;
        if (a > maxAbs) maxAbs = a;
        ++n;
    }

    double mse() const { return n ? sumErr / static_cast<double>(n) : 0.0; }

    // SNR em dB: +inf se não há erro, -inf se o original é silêncio mas há erro.
    double snr() const {
        if (sumErr == 0.0) return std::numeric_limits<double>::infinity();
        if (sumSig == 0.0) return -std::numeric_limits<double>::infinity();
        return 10.0 * std::log10(sumSig / sumErr);
    }

    void print(const std::string& name) const {
        std::cout << std::left << std::setw(6) << name << std::right
                  << "MSE = " << std::setw(14) << std::fixed << std::setprecision(4) << mse()
                  << "   Linf = " << std::setw(8) << maxAbs << "   SNR = ";
        const double s = snr();
        if (std::isinf(s)) std::cout << (s > 0 ? "inf" : "-inf");
        else std::cout << std::setprecision(4) << s;
        std::cout << " dB\n";
    }
};
