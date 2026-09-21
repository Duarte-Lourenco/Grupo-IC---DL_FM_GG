// wav_cmp — compara um ficheiro WAV com o original.
// Uso: wav_cmp original.wav modificado.wav
//
// Para cada canal (L, R, ...) e, se o áudio for estéreo, para o MID = (L+R)/2,
// imprime:
//   MSE  = média de (x - y)^2          (norma L2)
//   Linf = máximo de |x - y|           (norma L-infinito)
//   SNR  = 10 * log10( soma(x^2) / soma((x - y)^2) )   em dB
// onde x = original e y = modificado.

#include "common/channels.hpp"
#include "common/wav_io.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
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

    void print(const std::string& name) const {
        const double mse = n ? sumErr / static_cast<double>(n) : 0.0;
        std::cout << std::left << std::setw(6) << name << std::right
                  << "MSE = " << std::setw(14) << std::fixed << std::setprecision(4) << mse
                  << "   Linf = " << std::setw(8) << maxAbs << "   SNR = ";
        if (sumErr == 0.0) {
            std::cout << "inf";                       // ficheiros idênticos
        } else if (sumSig == 0.0) {
            std::cout << "-inf";                      // original em silêncio, mas com erro
        } else {
            std::cout << std::setprecision(4) << 10.0 * std::log10(sumSig / sumErr);
        }
        std::cout << " dB\n";
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Uso: wav_cmp original.wav modificado.wav\n";
        return 1;
    }

    try {
        const WavData orig = readWav(argv[1]);
        const WavData mod = readWav(argv[2]);

        if (orig.channels != mod.channels) {
            std::cerr << "Erro: n.º de canais diferente (" << orig.channels << " vs "
                      << mod.channels << ")\n";
            return 1;
        }
        if (orig.frames() != mod.frames()) {
            std::cerr << "Erro: n.º de frames diferente (" << orig.frames() << " vs "
                      << mod.frames() << ")\n";
            return 1;
        }
        if (orig.sampleRate != mod.sampleRate)
            std::cerr << "Aviso: taxas de amostragem diferentes (" << orig.sampleRate << " vs "
                      << mod.sampleRate << ")\n";
        if (orig.bitsPerSample != mod.bitsPerSample)
            std::cerr << "Aviso: bits por amostra diferentes (" << orig.bitsPerSample << " vs "
                      << mod.bitsPerSample << ")\n";

        const int C = orig.channels;
        const std::size_t N = orig.frames();

        std::cout << "frames: " << N << "   canais: " << C << "   taxa: " << orig.sampleRate
                  << " Hz   bits: " << orig.bitsPerSample << "\n";

        // Um Stats por canal
        std::string names[] = {"L", "R"};
        for (int c = 0; c < C; ++c) {
            Stats s;
            for (std::size_t n = 0; n < N; ++n) s.add(orig.at(n, c), mod.at(n, c));
            s.print(C == 2 ? names[c] : "canal" + std::to_string(c));
        }

        // MID só faz sentido em estéreo (usa a convenção >> 1 do grupo)
        if (C == 2) {
            Stats m;
            for (std::size_t n = 0; n < N; ++n)
                m.add(midOf(orig.at(n, 0), orig.at(n, 1)), midOf(mod.at(n, 0), mod.at(n, 1)));
            m.print("MID");
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
