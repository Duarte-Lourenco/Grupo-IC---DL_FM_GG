// wav_lossless_enc — codificador de áudio sem perdas.
//
// Cadeia: descorrelação entre canais (L/R ou MID/SIDE, escolhida por bloco) ->
// preditor polinomial fixo de ordem 0 a 3 (escolhida por bloco e por canal) ->
// resíduos codificados com Rice (parâmetro k escolhido por bloco e por canal) ->
// escrita bit a bit.
//
// Formato do ficheiro comprimido
//   cabeçalho: "ICL1" (32 b), taxa (32 b), canais (8 b), bits/amostra (8 b),
//              formato libsndfile (32 b), n.º de frames (40 b), bloco (16 b)
//   por bloco: modo de canais (1 b, só se estéreo)
//              por canal: ordem (2 b), k (5 b), resíduos em Rice
//
// O preditor usa as amostras anteriores reais, mesmo as do bloco anterior, por
// isso não é preciso guardar amostras de arranque em cada bloco. O n.º de frames
// no cabeçalho é o que permite ao descodificador ignorar o enchimento do último
// byte.
//
// Uso: wav_lossless_enc [-b BLOCO] [-r] input.wav comprimido
//   -b BLOCO  amostras por bloco (omissão 4096)
//   -r        k rápido: estima o k da média dos resíduos, sem o refinar
#include "common/bitstream.hpp"
#include "common/channels.hpp"
#include "common/golomb.hpp"
#include "common/wav_io.hpp"
#include "common/wav_raw.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static const uint32_t MAGIC = 0x49434C31;  // "ICL1"
static const int MAX_ORDER = 3;
static const unsigned MAX_K = 31;

// Amostra do canal c no domínio dado (0 = L/R, 1 = MID/SIDE), na frame n.
// Fora do ficheiro vale 0, o que define o arranque do preditor.
static inline int64_t sampleAt(const WavData& w, int domain, int c, int64_t n) {
    if (n < 0 || static_cast<std::size_t>(n) >= w.frames()) return 0;
    const auto nn = static_cast<std::size_t>(n);
    if (domain == 0) return w.at(nn, c);
    const int32_t L = w.at(nn, 0), R = w.at(nn, 1);
    return c == 0 ? midOf(L, R) : sideOf(L, R);
}

// Resíduo do preditor de ordem "order" na frame n.
static inline int64_t residualAt(const WavData& w, int domain, int c, int64_t n, int order) {
    const int64_t x0 = sampleAt(w, domain, c, n);
    switch (order) {
        case 0: return x0;
        case 1: return x0 - sampleAt(w, domain, c, n - 1);
        case 2: return x0 - 2 * sampleAt(w, domain, c, n - 1) + sampleAt(w, domain, c, n - 2);
        default: return x0 - 3 * sampleAt(w, domain, c, n - 1)
                        + 3 * sampleAt(w, domain, c, n - 2) - sampleAt(w, domain, c, n - 3);
    }
}

// Bits que os resíduos ocupam em Rice com parâmetro k.
static uint64_t riceBits(const std::vector<uint64_t>& u, unsigned k) {
    uint64_t bits = 0;
    for (const uint64_t v : u) bits += (v >> k) + 1 + k;
    return bits;
}

// k inicial: o que torna a média dos quocientes próxima de 1 (regra do FLAC).
static unsigned kFromMean(uint64_t soma, std::size_t n) {
    if (n == 0) return 0;
    const uint64_t media = soma / n;
    unsigned k = 0;
    while (k < MAX_K && (1ull << (k + 1)) <= media + 1) ++k;
    return k;
}

int main(int argc, char* argv[]) {
    int blockSize = 4096;
    bool fastK = false;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-b") == 0 && i + 1 < argc) blockSize = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-r") == 0) fastK = true;
        else pos.push_back(argv[i]);
    }
    if (pos.size() != 2 || blockSize < 16 || blockSize > 65535) {
        std::cerr << "Uso: wav_lossless_enc [-b BLOCO] [-r] input.wav comprimido\n";
        return 1;
    }

    try {
        const WavData in = readWav(pos[0]);
        const WavEnvelope env = readEnvelope(pos[0]);   // cabeçalho e cauda originais
        const int C = in.channels;
        const std::size_t N = in.frames();
        const int nModes = (C == 2) ? 2 : 1;

        BitWriter w(pos[1]);
        w.writeBits(MAGIC, 32);
        w.writeBits(static_cast<uint64_t>(in.sampleRate), 32);
        w.writeBits(static_cast<uint64_t>(C), 8);
        w.writeBits(static_cast<uint64_t>(in.bitsPerSample), 8);
        w.writeBits(static_cast<uint64_t>(in.format), 32);
        w.writeBits(static_cast<uint64_t>(N), 40);
        w.writeBits(static_cast<uint64_t>(blockSize), 16);

        // Envelope do WAV original, para a saída ser igual byte a byte
        w.writeBits(env.prefix.size(), 32);
        w.writeBits(env.suffix.size(), 32);
        for (const uint8_t byte : env.prefix) w.writeBits(byte, 8);
        for (const uint8_t byte : env.suffix) w.writeBits(byte, 8);

        // Buffers reutilizados em todos os blocos (nada cresce com o ficheiro)
        std::vector<uint64_t> u(blockSize), bestU(blockSize);
        std::vector<std::vector<uint64_t>> chosenU(C);

        for (std::size_t ini = 0; ini < N; ini += static_cast<std::size_t>(blockSize)) {
            const std::size_t fim = std::min(ini + static_cast<std::size_t>(blockSize), N);
            const std::size_t len = fim - ini;

            int bestMode = 0, bestOrder[2] = {0, 0};
            unsigned bestK[2] = {0, 0};
            uint64_t bestTotal = UINT64_MAX;

            for (int mode = 0; mode < nModes; ++mode) {
                uint64_t total = 0;
                int ord[2] = {0, 0};
                unsigned kk[2] = {0, 0};
                std::vector<std::vector<uint64_t>> us(C);

                for (int c = 0; c < C; ++c) {
                    uint64_t bestBits = UINT64_MAX;
                    for (int o = 0; o <= MAX_ORDER; ++o) {
                        u.resize(len);
                        uint64_t soma = 0;
                        for (std::size_t i = 0; i < len; ++i) {
                            u[i] = zigzag(residualAt(in, mode, c, static_cast<int64_t>(ini + i), o));
                            soma += u[i];
                        }
                        // k inicial pela média e, se não for o modo rápido,
                        // refinado nos vizinhos
                        const unsigned k0 = kFromMean(soma, len);
                        const unsigned lo = (fastK || k0 == 0) ? k0 : k0 - 1;
                        const unsigned hi = fastK ? k0 : std::min(k0 + 1, MAX_K);
                        for (unsigned k = lo; k <= hi; ++k) {
                            const uint64_t bits = riceBits(u, k) + 7;  // 2 ordem + 5 k
                            if (bits < bestBits) {
                                bestBits = bits;
                                ord[c] = o;
                                kk[c] = k;
                                bestU = u;
                            }
                        }
                    }
                    total += bestBits;
                    us[c] = bestU;
                }

                if (total < bestTotal) {
                    bestTotal = total;
                    bestMode = mode;
                    for (int c = 0; c < C; ++c) {
                        bestOrder[c] = ord[c];
                        bestK[c] = kk[c];
                        chosenU[c] = us[c];
                    }
                }
            }

            if (C == 2) w.writeBit(bestMode);
            for (int c = 0; c < C; ++c) {
                w.writeBits(static_cast<uint64_t>(bestOrder[c]), 2);
                w.writeBits(bestK[c], 5);
                const unsigned k = bestK[c];
                for (const uint64_t v : chosenU[c]) {   // Rice directo sobre o zigzag
                    const uint64_t q = v >> k;
                    for (uint64_t j = 0; j < q; ++j) w.writeBit(1);
                    w.writeBit(0);
                    if (k > 0) w.writeBits(v & ((1ull << k) - 1), static_cast<int>(k));
                }
            }
        }
        w.flush();
        std::cout << pos[0] << " -> " << pos[1] << ": " << N << " frames, bloco " << blockSize
                  << (fastK ? ", k rapido" : "") << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
