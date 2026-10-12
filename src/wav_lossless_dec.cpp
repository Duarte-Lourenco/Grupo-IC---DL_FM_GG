// wav_lossless_dec — descodificador de áudio sem perdas.
// Uso: wav_lossless_dec comprimido output.wav
#include "common/bitstream.hpp"
#include "common/channels.hpp"
#include "common/golomb.hpp"
#include "common/wav_io.hpp"

#include <iostream>
#include <vector>

static const uint32_t MAGIC = 0x49434C31;  // "ICL1"

// Previsão na posição n a partir das amostras já reconstruídas.
static inline int64_t predict(const std::vector<int64_t>& x, std::size_t n, int order) {
    const int64_t x1 = n >= 1 ? x[n - 1] : 0;
    const int64_t x2 = n >= 2 ? x[n - 2] : 0;
    const int64_t x3 = n >= 3 ? x[n - 3] : 0;
    switch (order) {
        case 0: return 0;
        case 1: return x1;
        case 2: return 2 * x1 - x2;
        default: return 3 * x1 - 3 * x2 + x3;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Uso: wav_lossless_dec comprimido output.wav\n";
        return 1;
    }
    try {
        BitReader r(argv[1]);
        if (r.readBits(32) != MAGIC) throw std::runtime_error("ficheiro não é do nosso codec");

        WavData out;
        out.sampleRate = static_cast<int>(r.readBits(32));
        out.channels = static_cast<int>(r.readBits(8));
        out.bitsPerSample = static_cast<int>(r.readBits(8));
        out.format = static_cast<int>(r.readBits(32));
        const std::size_t N = static_cast<std::size_t>(r.readBits(40));
        const std::size_t blockSize = static_cast<std::size_t>(r.readBits(16));

        const int C = out.channels;
        // Mantemos as duas representações completas, porque o preditor de um
        // bloco em MID/SIDE usa amostras anteriores nesse mesmo domínio, mesmo
        // que o bloco anterior tenha sido codificado em L/R (e vice-versa).
        std::vector<std::vector<int64_t>> lr(C, std::vector<int64_t>(N));
        std::vector<std::vector<int64_t>> ms;
        if (C == 2) ms.assign(2, std::vector<int64_t>(N));

        for (std::size_t ini = 0; ini < N; ini += blockSize) {
            const std::size_t fim = std::min(ini + blockSize, N);
            const int mode = (C == 2) ? r.readBit() : 0;
            auto& ch = (mode == 0) ? lr : ms;
            for (int c = 0; c < C; ++c) {
                const int order = static_cast<int>(r.readBits(2));
                const unsigned k = static_cast<unsigned>(r.readBits(5));
                const Golomb g(1u << k);
                for (std::size_t n = ini; n < fim; ++n)
                    ch[c][n] = g.decode(r) + predict(ch[c], n, order);
            }
            if (C == 2) {   // preencher a outra representação neste bloco
                for (std::size_t n = ini; n < fim; ++n) {
                    if (mode == 0) {
                        const auto L = static_cast<int32_t>(lr[0][n]);
                        const auto R = static_cast<int32_t>(lr[1][n]);
                        ms[0][n] = midOf(L, R);
                        ms[1][n] = sideOf(L, R);
                    } else {
                        int32_t L, R;
                        fromMidSide(static_cast<int32_t>(ms[0][n]),
                                    static_cast<int32_t>(ms[1][n]), L, R);
                        lr[0][n] = L;
                        lr[1][n] = R;
                    }
                }
            }
        }

        out.samples.resize(N * C);
        for (std::size_t n = 0; n < N; ++n)
            for (int c = 0; c < C; ++c)
                out.samples[n * C + c] = static_cast<int32_t>(lr[c][n]);

        writeWav(argv[2], out);
        std::cout << argv[1] << " -> " << argv[2] << ": " << N << " frames\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}