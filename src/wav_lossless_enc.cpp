// wav_lossless_enc — codificador de áudio sem perdas.
//
// Cadeia: descorrelação entre canais (L/R ou MID/SIDE, escolhida por bloco) ->
// preditor polinomial fixo de ordem 0 a 3 (escolhida por bloco e por canal) ->
// resíduos codificados com Rice -> escrita bit a bit.
//
// Nenhum parâmetro da codificação é fixado à mão: o modo de canais, a ordem do
// preditor, o número de partições de Rice e o k de cada partição são todos
// escolhidos por minimização do custo real em bits do bloco em causa.
//
// O ficheiro é processado em FLUXO: em memória está apenas um lote de blocos
// mais as três amostras anteriores de que o preditor de ordem 3 depende, pelo
// que a memória não cresce com o tamanho do ficheiro.
//
//   [ h0 h1 h2 | bloco 0 | bloco 1 | ... ]
//     histórico   lote lido de uma vez
//
// Formato do ficheiro comprimido
//   cabeçalho: marca (32 b), taxa (32 b), canais (8 b), bits/amostra (8 b),
//              formato (32 b), n.º de frames (40 b), bloco (16 b),
//              tamanho do prefixo (32 b), tamanho do sufixo (32 b),
//              bytes do prefixo e do sufixo do contentor WAV
//   por bloco: modo de canais (1 b, só se estéreo)
//              por canal: ordem (2 b), expoente das partições (3 b) e,
//                         por partição, k (5 b) + resíduos
//
// A marca "ICL3" indica partições escolhidas bloco a bloco e canal a canal.
// A marca antiga "ICL1", com uma só partição, continua a ser lida.
// O n.º de frames no cabeçalho é o que permite ao descodificador ignorar o
// enchimento do último byte. Os bytes do contentor em volta do bloco "data"
// são guardados para a saída ser igual ao original byte a byte, metadados
// incluídos, o que o flac não faz.
//
// Uso: wav_lossless_enc [-b BLOCO] [-r] [--threads N] input.wav comprimido
//   -b BLOCO     amostras por bloco (omissão 4096)
//   -r           k rápido: estima o k da média dos resíduos, sem o refinar
//   --threads N  decide N blocos em paralelo (omissão: 4, ou os núcleos que
//                houver); o ficheiro produzido é o mesmo qualquer que seja N
#include "common/bitstream.hpp"
#include "common/channels.hpp"
#include "common/golomb.hpp"
#include "common/wav_stream.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static const uint32_t MAGIC1 = 0x49434C31;  // "ICL1" — 1 partição por bloco
static const uint32_t MAGIC3 = 0x49434C33;  // "ICL3" — partições escolhidas por bloco
static const int MAX_ORDER = 3;
static const unsigned MAX_K = 31;
static const int HIST = 3;                  // amostras de histórico do preditor
static const std::size_t MIN_PART = 16;     // amostras mínimas por partição
static const int MAX_PART_POW = 7;          // no máximo 2^7 = 128 partições

// Janela sobre o lote em memória. "base" é a frame absoluta que está em
// buf[HIST]; as HIST frames anteriores são o histórico do bloco anterior.
struct Window {
    int channels = 0;
    int64_t base = 0;
    int64_t total = 0;
    const int32_t* buf = nullptr;

    int32_t raw(int64_t n, int c) const {
        const int64_t i = n - base + HIST;
        return buf[static_cast<std::size_t>(i) * static_cast<std::size_t>(channels)
                   + static_cast<std::size_t>(c)];
    }
};

// Amostra do canal c no domínio dado (0 = L/R, 1 = MID/SIDE), na frame n.
// Fora do ficheiro vale 0, o que define o arranque do preditor.
static inline int64_t sampleAt(const Window& w, int domain, int c, int64_t n) {
    if (n < 0 || n >= w.total) return 0;
    if (domain == 0) return w.raw(n, c);
    const int32_t L = w.raw(n, 0), R = w.raw(n, 1);
    return c == 0 ? midOf(L, R) : sideOf(L, R);
}

// Resíduo do preditor de ordem "order" na frame n.
static inline int64_t residualAt(const Window& w, int domain, int c, int64_t n, int order) {
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
static inline uint64_t riceBits(const uint64_t* u, std::size_t n, unsigned k) {
    uint64_t bits = 0;
    for (std::size_t i = 0; i < n; ++i) bits += (u[i] >> k) + 1 + k;
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

// Melhor k de uma partição e o custo em bits dos seus resíduos.
static inline void bestKFor(const uint64_t* u, std::size_t n, bool fastK,
                            unsigned& kOut, uint64_t& bitsOut) {
    if (n == 0) { kOut = 0; bitsOut = 0; return; }
    uint64_t soma = 0;
    for (std::size_t i = 0; i < n; ++i) soma += u[i];
    const unsigned k0 = kFromMean(soma, n);
    const unsigned lo = (fastK || k0 == 0) ? k0 : k0 - 1;
    const unsigned hi = fastK ? k0 : std::min(k0 + 1, MAX_K);
    uint64_t best = UINT64_MAX;
    unsigned bk = k0;
    for (unsigned k = lo; k <= hi; ++k) {
        const uint64_t b = riceBits(u, n, k);
        if (b < best) { best = b; bk = k; }
    }
    kOut = bk;
    bitsOut = best;
}

// Limites das partições de um bloco de "len" amostras.
static void limites(std::size_t len, int nPart, std::vector<std::size_t>& lim) {
    lim.resize(static_cast<std::size_t>(nPart) + 1);
    for (int j = 0; j <= nPart; ++j)
        lim[static_cast<std::size_t>(j)] =
            len * static_cast<std::size_t>(j) / static_cast<std::size_t>(nPart);
}

struct BlockResult {
    std::size_t ini = 0, len = 0;
    int mode = 0;
    int order[2] = {0, 0};
    int partPow[2] = {0, 0};           // expoente das partições, por canal
    std::vector<unsigned> k[2];        // um k por partição
    std::vector<uint64_t> u[2];
};

// Maior expoente de partição admissível num bloco de "len" amostras.
static int maxPartPow(std::size_t len) {
    int p = 0;
    while (p < MAX_PART_POW &&
           len / static_cast<std::size_t>(1 << (p + 1)) >= MIN_PART) ++p;
    return p;
}

// Custo em bits dos resíduos com 2^p partições, e os k correspondentes.
static uint64_t custoCom(const std::vector<uint64_t>& u, std::size_t len, int p,
                         bool fastK, std::vector<unsigned>& kOut,
                         std::vector<std::size_t>& lim) {
    const int nPart = 1 << p;
    limites(len, nPart, lim);
    kOut.resize(static_cast<std::size_t>(nPart));
    uint64_t bits = 0;
    for (int j = 0; j < nPart; ++j) {
        const std::size_t a = lim[static_cast<std::size_t>(j)];
        const std::size_t b = lim[static_cast<std::size_t>(j) + 1];
        uint64_t pb = 0;
        bestKFor(u.data() + a, b - a, fastK, kOut[static_cast<std::size_t>(j)], pb);
        bits += 5 + pb;                 // 5 bits do k + os resíduos da partição
    }
    return bits;
}

// Escolhe modo de canais, ordem e k de cada partição por minimização do custo
// real em bits, e guarda os resíduos escolhidos.
// Escolhe, por minimização do custo real em bits: o modo de canais do bloco,
// a ordem do preditor de cada canal, e o número de partições de Rice de cada
// canal com os respetivos k. Nenhum destes valores é fixado à mão.
//
// Em duas fases, para o custo não explodir. A primeira decide o modo e a ordem
// com um número de partições de referência; a segunda, já com os resíduos
// vencedores guardados, experimenta todos os números de partições admissíveis.
// A ordem e o modo quase não dependem do número de partições, e a fase 2
// trabalha sobre resíduos já calculados, por isso é barata.
static void decideBlock(const Window& win, int C, int nModes, bool fastK,
                        BlockResult& res) {
    const std::size_t len = res.len;
    const auto ini = static_cast<int64_t>(res.ini);
    const int pMax = maxPartPow(len);
    const int pRef = pMax < 3 ? pMax : 3;      // referência da fase 1
    std::vector<uint64_t> u(len);
    std::vector<unsigned> kk, kBest;
    std::vector<std::size_t> lim;
    uint64_t bestTotal = UINT64_MAX;

    // ---- fase 1: modo de canais e ordem do preditor
    for (int mode = 0; mode < nModes; ++mode) {
        uint64_t total = 0;
        int ord[2] = {0, 0};
        std::vector<uint64_t> us[2];

        for (int c = 0; c < C; ++c) {
            uint64_t bestBits = UINT64_MAX;
            for (int o = 0; o <= MAX_ORDER; ++o) {
                for (std::size_t i = 0; i < len; ++i)
                    u[i] = zigzag(residualAt(win, mode, c, ini + static_cast<int64_t>(i), o));
                const uint64_t bits = 2 + 3 + custoCom(u, len, pRef, fastK, kk, lim);
                if (bits < bestBits) {
                    bestBits = bits;
                    ord[c] = o;
                    us[c] = u;
                }
            }
            total += bestBits;
        }

        if (total < bestTotal) {
            bestTotal = total;
            res.mode = mode;
            for (int c = 0; c < C; ++c) {
                res.order[c] = ord[c];
                res.u[c] = us[c];
            }
        }
    }

    // ---- fase 2: número de partições de cada canal, sobre os resíduos já escolhidos
    for (int c = 0; c < C; ++c) {
        uint64_t bestBits = UINT64_MAX;
        for (int p = 0; p <= pMax; ++p) {
            const uint64_t bits = custoCom(res.u[c], len, p, fastK, kk, lim);
            if (bits < bestBits) {
                bestBits = bits;
                res.partPow[c] = p;
                kBest = kk;
            }
        }
        res.k[c] = kBest;
    }
}

int main(int argc, char* argv[]) {
    // 4 fios de execução é o ponto em que o ganho deixa de compensar a
    // memória: medido num Ryzen 7 6800H, 1 fio leva 0,65 s, 2 levam 0,44 s
    // (1,5x) e 4 levam 0,29 s (2,2x) por mais 104 KiB; 8 levam 0,23 s (2,8x)
    // mas custam 1 MiB. Em máquinas com menos núcleos usa-se o que houver.
    const unsigned nucleos = std::thread::hardware_concurrency();
    int blockSize = 4096;
    int nThreads = static_cast<int>(std::min(4u, nucleos ? nucleos : 1u));
    bool fastK = false;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-b" && i + 1 < argc) blockSize = std::atoi(argv[++i]);
        else if (a == "-r") fastK = true;
        else if (a == "--threads" && i + 1 < argc) nThreads = std::atoi(argv[++i]);
        else pos.push_back(a);
    }
    if (pos.size() != 2 || blockSize < 16 || blockSize > 65535 || nThreads < 1 ||
        nThreads > 64) {
        std::cerr << "Uso: wav_lossless_enc [-b BLOCO] [-r] [--threads N]"
                     " input.wav comprimido\n";
        return 1;
    }

    try {
        WavReader in(pos[0]);
        const WavEnvelope env = readEnvelopeStream(pos[0]);
        const int C = in.channels;
        const auto N = static_cast<std::size_t>(in.frames);
        const int nModes = (C == 2) ? 2 : 1;

        BitWriter w(pos[1]);
        w.writeBits(MAGIC3, 32);
        w.writeBits(static_cast<uint64_t>(in.sampleRate), 32);
        w.writeBits(static_cast<uint64_t>(C), 8);
        w.writeBits(static_cast<uint64_t>(in.bitsPerSample), 8);
        w.writeBits(static_cast<uint64_t>(in.format), 32);
        w.writeBits(static_cast<uint64_t>(N), 40);
        w.writeBits(static_cast<uint64_t>(blockSize), 16);
        w.writeBits(static_cast<uint64_t>(env.prefix.size()), 32);
        w.writeBits(static_cast<uint64_t>(env.suffix.size()), 32);
        for (const uint8_t b : env.prefix) w.writeBits(b, 8);
        for (const uint8_t b : env.suffix) w.writeBits(b, 8);

        // Lote: HIST frames de histórico + nThreads blocos
        const auto lote = static_cast<std::size_t>(nThreads);
        std::vector<int32_t> buf(
            (static_cast<std::size_t>(HIST) + lote * static_cast<std::size_t>(blockSize))
                * static_cast<std::size_t>(C), 0);
        std::vector<BlockResult> res(lote);
        for (auto& r : res) {
            r.k[0].reserve(1 << MAX_PART_POW);
            r.k[1].reserve(1 << MAX_PART_POW);
        }
        std::vector<std::size_t> lim;

        std::size_t ini = 0;
        while (ini < N) {
            const std::size_t querLer =
                std::min(lote * static_cast<std::size_t>(blockSize), N - ini);
            int32_t* dst = buf.data() + static_cast<std::size_t>(HIST)
                                            * static_cast<std::size_t>(C);
            const std::size_t got = in.readFrames(dst, querLer);
            if (got != querLer) throw std::runtime_error("leitura incompleta do ficheiro WAV");

            Window win;
            win.channels = C;
            win.total = static_cast<int64_t>(N);
            win.base = static_cast<int64_t>(ini);
            win.buf = buf.data();

            std::size_t nb = 0;
            for (std::size_t off = 0; off < got; off += static_cast<std::size_t>(blockSize)) {
                res[nb].ini = ini + off;
                res[nb].len = std::min(static_cast<std::size_t>(blockSize), got - off);
                ++nb;
            }

            // A decisão de cada bloco só depende das suas amostras e das 3
            // anteriores, que são amostras de entrada e já estão lidas. Por
            // isso os blocos do lote podem ser decididos em paralelo sem que o
            // resultado mude: a escrita é sempre feita pela ordem dos blocos.
            if (nThreads == 1 || nb == 1) {
                for (std::size_t j = 0; j < nb; ++j)
                    decideBlock(win, C, nModes, fastK, res[j]);
            } else {
                std::vector<std::thread> th;
                th.reserve(nb);
                for (std::size_t j = 0; j < nb; ++j)
                    th.emplace_back([&, j] { decideBlock(win, C, nModes, fastK, res[j]); });
                for (auto& t : th) t.join();
            }

            for (std::size_t j = 0; j < nb; ++j) {
                const BlockResult& b = res[j];
                if (C == 2) w.writeBit(b.mode);
                for (int c = 0; c < C; ++c) {
                    w.writeBits(static_cast<uint64_t>(b.order[c]), 2);
                    w.writeBits(static_cast<uint64_t>(b.partPow[c]), 3);
                    const int nPart = 1 << b.partPow[c];
                    limites(b.len, nPart, lim);
                    for (int q = 0; q < nPart; ++q) {
                        const unsigned k = b.k[c][static_cast<std::size_t>(q)];
                        w.writeBits(k, 5);
                        for (std::size_t i = lim[static_cast<std::size_t>(q)];
                             i < lim[static_cast<std::size_t>(q) + 1]; ++i) {
                            const uint64_t v = b.u[c][i];
                            const uint64_t qq = v >> k;   // Rice directo sobre o zigzag
                            for (uint64_t z = 0; z < qq; ++z) w.writeBit(1);
                            w.writeBit(0);
                            if (k > 0) w.writeBits(v & ((1ull << k) - 1), static_cast<int>(k));
                        }
                    }
                }
            }

            // Histórico do lote seguinte: as últimas HIST frames deste
            if (got >= static_cast<std::size_t>(HIST))
                std::memmove(buf.data(), buf.data() + got * static_cast<std::size_t>(C),
                             static_cast<std::size_t>(HIST) * static_cast<std::size_t>(C)
                                 * sizeof(int32_t));
            ini += got;
        }
        w.flush();
        std::cout << pos[0] << " -> " << pos[1] << ": " << N << " frames, bloco " << blockSize
                  << ", particoes adaptativas, " << nThreads << " thread"
                  << (nThreads == 1 ? "" : "s") << (fastK ? ", k rapido" : "") << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}