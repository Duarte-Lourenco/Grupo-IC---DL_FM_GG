// wav_lossless_dec — descodificador de áudio sem perdas.
//
// Faz o inverso do wav_lossless_enc: lê o cabeçalho e, em cada bloco, lê o
// modo de canais, a ordem do preditor e o k de Rice de cada partição,
// descodifica os resíduos e soma-lhes a previsão.
//
// Tal como o codificador, trabalha em FLUXO: em memória está apenas o bloco
// em curso e as três amostras anteriores de que o preditor depende, e o WAV é
// escrito à medida que cada bloco é reconstruído. Guarda L/R; os valores
// MID/SIDE de que o preditor precisa são recalculados a partir de L/R.
//
// Aceita a marca "ICL3" (número de partições indicado em cada bloco e canal)
// e a antiga "ICL1" (uma só partição por bloco).
//
// Uso: wav_lossless_dec comprimido output.wav
#include "common/bitstream.hpp"
#include "common/channels.hpp"
#include "common/golomb.hpp"
#include "common/wav_stream.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

static const uint32_t MAGIC1 = 0x49434C31;  // "ICL1"
static const uint32_t MAGIC3 = 0x49434C33;  // "ICL3" — particoes por bloco
static const int HIST = 3;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Uso: wav_lossless_dec comprimido output.wav\n";
        return 1;
    }
    try {
        BitReader r(argv[1]);
        const uint64_t magic = r.readBits(32);
        if (magic != MAGIC1 && magic != MAGIC3)
            throw std::runtime_error("ficheiro não foi produzido pelo wav_lossless_enc");

        const int sampleRate = static_cast<int>(r.readBits(32));
        const int C = static_cast<int>(r.readBits(8));
        const int bitsPerSample = static_cast<int>(r.readBits(8));
        const int format = static_cast<int>(r.readBits(32));
        const auto N = static_cast<std::size_t>(r.readBits(40));
        const auto blockSize = static_cast<std::size_t>(r.readBits(16));
        const auto prefixSize = static_cast<std::size_t>(r.readBits(32));
        const auto suffixSize = static_cast<std::size_t>(r.readBits(32));
        (void)sampleRate;
        (void)format;
        if (C < 1 || C > 2 || blockSize == 0 || bitsPerSample % 8 != 0 ||
            bitsPerSample < 8 || bitsPerSample > 32)
            throw std::runtime_error("cabeçalho inválido");

        WavEnvelope env;
        env.prefix.resize(prefixSize);
        for (auto& b : env.prefix) b = static_cast<uint8_t>(r.readBits(8));
        env.suffix.resize(suffixSize);
        for (auto& b : env.suffix) b = static_cast<uint8_t>(r.readBits(8));

        const bool porBloco = (magic == MAGIC3);

        WavWriterStream out(argv[2], env, bitsPerSample);

        // Janela: [HIST frames de histórico | bloco em curso]
        std::vector<int32_t> win(
            (static_cast<std::size_t>(HIST) + blockSize) * static_cast<std::size_t>(C), 0);
        std::vector<std::vector<int64_t>> blk(C);   // bloco no domínio MID/SIDE
        std::vector<std::size_t> lim;
        std::size_t ini = 0;

        // Amostra L/R já reconstruída (0 antes do início do ficheiro)
        auto lrAt = [&](int c, int64_t n) -> int64_t {
            if (n < 0) return 0;
            const int64_t i = n - static_cast<int64_t>(ini) + HIST;
            return win[static_cast<std::size_t>(i) * static_cast<std::size_t>(C)
                       + static_cast<std::size_t>(c)];
        };
        // Amostra MID/SIDE: dentro do bloco vem do próprio bloco, antes dele
        // calcula-se a partir do L/R do histórico
        auto msAt = [&](int c, int64_t n) -> int64_t {
            if (n < 0) return 0;
            if (static_cast<std::size_t>(n) >= ini)
                return blk[c][static_cast<std::size_t>(n) - ini];
            const auto L = static_cast<int32_t>(lrAt(0, n));
            const auto R = static_cast<int32_t>(lrAt(1, n));
            return c == 0 ? midOf(L, R) : sideOf(L, R);
        };

        for (; ini < N; ini += blockSize) {
            const std::size_t fim = std::min(ini + blockSize, N);
            const std::size_t len = fim - ini;
            const int mode = (C == 2) ? r.readBit() : 0;
            if (mode == 1)
                for (int c = 0; c < C; ++c) blk[c].assign(len, 0);

            for (int c = 0; c < C; ++c) {
                const int order = static_cast<int>(r.readBits(2));
                // o número de partições deste canal vem no próprio bloco
                const int nPart = porBloco ? (1 << static_cast<int>(r.readBits(3))) : 1;
                lim.resize(static_cast<std::size_t>(nPart) + 1);
                for (int q = 0; q <= nPart; ++q)
                    lim[static_cast<std::size_t>(q)] =
                        len * static_cast<std::size_t>(q) / static_cast<std::size_t>(nPart);
                for (int q = 0; q < nPart; ++q) {
                    const auto k = static_cast<unsigned>(r.readBits(5));
                    for (std::size_t i = lim[static_cast<std::size_t>(q)];
                         i < lim[static_cast<std::size_t>(q) + 1]; ++i) {
                        uint64_t qq = 0;                         // quociente em unário
                        while (r.readBit() == 1) ++qq;
                        const uint64_t rest = k ? r.readBits(static_cast<int>(k)) : 0;
                        const int64_t res = unzigzag((qq << k) | rest);
                        const auto n = static_cast<int64_t>(ini + i);

                        // previsão com as amostras anteriores no mesmo domínio
                        int64_t p = 0;
                        if (mode == 0) {
                            const int64_t a = lrAt(c, n - 1), b = lrAt(c, n - 2),
                                          d = lrAt(c, n - 3);
                            p = order == 0 ? 0 : order == 1 ? a : order == 2 ? 2 * a - b
                                                                            : 3 * a - 3 * b + d;
                            win[(static_cast<std::size_t>(HIST) + i) * static_cast<std::size_t>(C)
                                + static_cast<std::size_t>(c)] = static_cast<int32_t>(res + p);
                        } else {
                            const int64_t a = msAt(c, n - 1), b = msAt(c, n - 2),
                                          d = msAt(c, n - 3);
                            p = order == 0 ? 0 : order == 1 ? a : order == 2 ? 2 * a - b
                                                                            : 3 * a - 3 * b + d;
                            blk[c][i] = res + p;
                        }
                    }
                }
            }

            if (mode == 1) {   // MID/SIDE -> L/R, com os dois canais já lidos
                for (std::size_t i = 0; i < len; ++i) {
                    int32_t L, R;
                    fromMidSide(static_cast<int32_t>(blk[0][i]),
                                static_cast<int32_t>(blk[1][i]), L, R);
                    win[(static_cast<std::size_t>(HIST) + i) * static_cast<std::size_t>(C)] = L;
                    win[(static_cast<std::size_t>(HIST) + i) * static_cast<std::size_t>(C) + 1] = R;
                }
            }

            // escreve o bloco e passa as últimas HIST frames para o histórico
            out.writeSamples(win.data() + static_cast<std::size_t>(HIST)
                                 * static_cast<std::size_t>(C),
                             len * static_cast<std::size_t>(C));
            if (len >= static_cast<std::size_t>(HIST))
                std::memmove(win.data(), win.data() + len * static_cast<std::size_t>(C),
                             static_cast<std::size_t>(HIST) * static_cast<std::size_t>(C)
                                 * sizeof(int32_t));
        }

        out.finish(env);
        std::cout << argv[1] << " -> " << argv[2] << ": " << N << " frames"
                  << (porBloco ? ", particoes adaptativas" : "") << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}