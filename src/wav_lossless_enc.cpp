// wav_lossless_enc — codificador de áudio sem perdas.
// Uso: wav_lossless_enc [-b TAMANHO_BLOCO] input.wav comprimido
#include "common/bitstream.hpp"
#include "common/channels.hpp"
#include "common/golomb.hpp"
#include "common/wav_io.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static const uint32_t MAGIC = 0x49434C31;  // "ICL1"
static const int MAX_ORDER = 3;

// Resíduo do preditor polinomial fixo de ordem "order" na posição n.
// Amostras antes do início valem 0.
static inline int64_t residual(const std::vector<int64_t>& x, std::size_t n, int order) {
    auto at = [&](std::size_t i) -> int64_t { return i < x.size() ? x[i] : 0; };
    const int64_t x0 = x[n];
    const int64_t x1 = n >= 1 ? at(n - 1) : 0;
    const int64_t x2 = n >= 2 ? at(n - 2) : 0;
    const int64_t x3 = n >= 3 ? at(n - 3) : 0;
    switch (order) {
        case 0: return x0;
        case 1: return x0 - x1;
        case 2: return x0 - 2 * x1 + x2;
        default: return x0 - 3 * x1 + 3 * x2 - x3;
    }
}

// Custo em bits de codificar [ini, fim) com Rice de parâmetro k.
static uint64_t costRice(const std::vector<int64_t>& x, std::size_t ini, std::size_t fim,
                         int order, unsigned k) {
    uint64_t bits = 0;
    for (std::size_t n = ini; n < fim; ++n) {
        const uint64_t u = zigzag(residual(x, n, order));
        bits += (u >> k) + 1 + k;
    }
    return bits;
}

int main(int argc, char* argv[]) {
    int blockSize = 4096;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-b") == 0 && i + 1 < argc) blockSize = std::atoi(argv[++i]);
        else pos.push_back(argv[i]);
    }
    if (pos.size() != 2 || blockSize < 16 || blockSize > 65535) {
        std::cerr << "Uso: wav_lossless_enc [-b TAMANHO_BLOCO] input.wav comprimido\n";
        return 1;
    }

    try {
        const WavData in = readWav(pos[0]);
        const int C = in.channels;
        const std::size_t N = in.frames();

        // Canais em int64: LR e, se estéreo, MID/SIDE
        std::vector<std::vector<int64_t>> lr(C, std::vector<int64_t>(N));
        for (std::size_t n = 0; n < N; ++n)
            for (int c = 0; c < C; ++c) lr[c][n] = in.at(n, c);

        std::vector<std::vector<int64_t>> ms;
        if (C == 2) {
            ms.assign(2, std::vector<int64_t>(N));
            for (std::size_t n = 0; n < N; ++n) {
                ms[0][n] = midOf(static_cast<int32_t>(lr[0][n]), static_cast<int32_t>(lr[1][n]));
                ms[1][n] = sideOf(static_cast<int32_t>(lr[0][n]), static_cast<int32_t>(lr[1][n]));
            }
        }

        BitWriter w(pos[1]);
        w.writeBits(MAGIC, 32);
        w.writeBits(static_cast<uint64_t>(in.sampleRate), 32);
        w.writeBits(static_cast<uint64_t>(C), 8);
        w.writeBits(static_cast<uint64_t>(in.bitsPerSample), 8);
        w.writeBits(static_cast<uint64_t>(in.format), 32);
        w.writeBits(static_cast<uint64_t>(N), 40);
        w.writeBits(static_cast<uint64_t>(blockSize), 16);

        for (std::size_t ini = 0; ini < N; ini += blockSize) {
            const std::size_t fim = std::min(ini + static_cast<std::size_t>(blockSize), N);

            // Escolhe modo de canais, ordem e k que minimizam os bits do bloco
            int bestMode = 0, bestOrder[2] = {0, 0};
            unsigned bestK[2] = {0, 0};
            uint64_t bestBits = UINT64_MAX;
            for (int mode = 0; mode < (C == 2 ? 2 : 1); ++mode) {
                const auto& ch = (mode == 0) ? lr : ms;
                uint64_t total = 0;
                int ord[2] = {0, 0};
                unsigned kk[2] = {0, 0};
                for (int c = 0; c < C; ++c) {
                    uint64_t best = UINT64_MAX;
                    for (int o = 0; o <= MAX_ORDER; ++o)
                        for (unsigned k = 0; k < 24; ++k) {
                            const uint64_t b = costRice(ch[c], ini, fim, o, k);
                            if (b < best) { best = b; ord[c] = o; kk[c] = k; }
                        }
                    total += best;
                }
                if (total < bestBits) {
                    bestBits = total;
                    bestMode = mode;
                    for (int c = 0; c < C; ++c) { bestOrder[c] = ord[c]; bestK[c] = kk[c]; }
                }
            }

            const auto& ch = (bestMode == 0) ? lr : ms;
            if (C == 2) w.writeBit(bestMode);
            for (int c = 0; c < C; ++c) {
                w.writeBits(static_cast<uint64_t>(bestOrder[c]), 2);
                w.writeBits(bestK[c], 5);
                const Golomb g(1u << bestK[c]);
                for (std::size_t n = ini; n < fim; ++n)
                    g.encode(w, residual(ch[c], n, bestOrder[c]));
            }
        }
        w.flush();
        std::cout << pos[0] << " -> " << pos[1] << ": " << N << " frames, bloco "
                  << blockSize << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}