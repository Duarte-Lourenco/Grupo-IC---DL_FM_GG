// wav_lossless_dec — descodificador de áudio sem perdas.
//
// Faz o inverso do wav_lossless_enc: lê o cabeçalho e, em cada bloco, lê o modo
// de canais, a ordem do preditor e o k de Rice de cada canal, descodifica os
// resíduos e soma-lhes a previsão. Guarda sempre L/R; os valores MID/SIDE de que
// o preditor precisa são recalculados a partir de L/R, o que evita manter as
// duas representações do ficheiro em memória.
//
// Uso: wav_lossless_dec comprimido output.wav
#include "common/bitstream.hpp"
#include "common/channels.hpp"
#include "common/golomb.hpp"
#include "common/wav_io.hpp"
#include "common/wav_raw.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

static const uint32_t MAGIC = 0x49434C31;  // "ICL1"

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Uso: wav_lossless_dec comprimido output.wav\n";
        return 1;
    }
    try {
        BitReader r(argv[1]);
        if (r.readBits(32) != MAGIC)
            throw std::runtime_error("ficheiro não foi produzido pelo wav_lossless_enc");

        WavData out;
        out.sampleRate = static_cast<int>(r.readBits(32));
        out.channels = static_cast<int>(r.readBits(8));
        out.bitsPerSample = static_cast<int>(r.readBits(8));
        out.format = static_cast<int>(r.readBits(32));
        const auto N = static_cast<std::size_t>(r.readBits(40));
        const auto blockSize = static_cast<std::size_t>(r.readBits(16));
        const int C = out.channels;
        if (C < 1 || C > 2 || blockSize == 0)
            throw std::runtime_error("cabeçalho inválido");

        WavEnvelope env;
        env.prefix.resize(static_cast<std::size_t>(r.readBits(32)));
        env.suffix.resize(static_cast<std::size_t>(r.readBits(32)));
        for (auto& byte : env.prefix) byte = static_cast<uint8_t>(r.readBits(8));
        for (auto& byte : env.suffix) byte = static_cast<uint8_t>(r.readBits(8));

        out.samples.assign(N * C, 0);
        std::vector<std::vector<int64_t>> blk(C);   // bloco no domínio MID/SIDE

        // Amostra L/R já reconstruída (0 antes do início do ficheiro)
        auto lrAt = [&](int c, int64_t n) -> int64_t {
            return n < 0 ? 0 : out.samples[static_cast<std::size_t>(n) * C + c];
        };
        // Amostra MID/SIDE: dentro do bloco vem do próprio bloco, antes dele
        // calcula-se a partir do L/R já escrito
        auto msAt = [&](int c, int64_t n, std::size_t ini) -> int64_t {
            if (n < 0) return 0;
            if (static_cast<std::size_t>(n) >= ini)
                return blk[c][static_cast<std::size_t>(n) - ini];
            const auto L = static_cast<int32_t>(lrAt(0, n));
            const auto R = static_cast<int32_t>(lrAt(1, n));
            return c == 0 ? midOf(L, R) : sideOf(L, R);
        };

        for (std::size_t ini = 0; ini < N; ini += blockSize) {
            const std::size_t fim = std::min(ini + blockSize, N);
            const std::size_t len = fim - ini;
            const int mode = (C == 2) ? r.readBit() : 0;
            if (mode == 1)
                for (int c = 0; c < C; ++c) blk[c].assign(len, 0);

            for (int c = 0; c < C; ++c) {
                const int order = static_cast<int>(r.readBits(2));
                const auto k = static_cast<unsigned>(r.readBits(5));

                for (std::size_t i = 0; i < len; ++i) {
                    uint64_t q = 0;                              // quociente em unário
                    while (r.readBit() == 1) ++q;
                    const uint64_t rest = k ? r.readBits(static_cast<int>(k)) : 0;
                    const int64_t res = unzigzag((q << k) | rest);
                    const auto n = static_cast<int64_t>(ini + i);

                    // previsão com as amostras anteriores no mesmo domínio
                    int64_t p = 0;
                    if (mode == 0) {
                        const int64_t a = lrAt(c, n - 1), b = lrAt(c, n - 2), d = lrAt(c, n - 3);
                        p = order == 0 ? 0 : order == 1 ? a : order == 2 ? 2 * a - b
                                                                        : 3 * a - 3 * b + d;
                        out.samples[(ini + i) * C + c] = static_cast<int32_t>(res + p);
                    } else {
                        const int64_t a = msAt(c, n - 1, ini), b = msAt(c, n - 2, ini),
                                      d = msAt(c, n - 3, ini);
                        p = order == 0 ? 0 : order == 1 ? a : order == 2 ? 2 * a - b
                                                                        : 3 * a - 3 * b + d;
                        blk[c][i] = res + p;
                    }
                }
            }

            if (mode == 1) {   // MID/SIDE -> L/R, já com os dois canais do bloco lidos
                for (std::size_t i = 0; i < len; ++i) {
                    int32_t L, R;
                    fromMidSide(static_cast<int32_t>(blk[0][i]),
                                static_cast<int32_t>(blk[1][i]), L, R);
                    out.samples[(ini + i) * C] = L;
                    out.samples[(ini + i) * C + 1] = R;
                }
            }
        }

        writeRawWav(argv[2], env, out.samples, out.bitsPerSample);
        std::cout << argv[1] << " -> " << argv[2] << ": " << N << " frames\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
