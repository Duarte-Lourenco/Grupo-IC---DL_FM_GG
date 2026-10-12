// lossy.hpp — partes comuns ao codificador e ao descodificador com perdas.
//
// Cadeia do codec:
//   WAV -> MID/SIDE ortonormal (se estéreo) -> MDCT (janela seno, 50 %) ->
//   quantização escalar uniforme com zona morta -> codificação aritmética
//   adaptativa com contexto -> ficheiro
//
// Todas as transformações são ortonormais, por isso o erro quadrático dos
// coeficientes é o erro quadrático do áudio. Para o SNR (critério do
// enunciado), o passo de quantização deve ser o mesmo em todos os
// coeficientes: é o que minimiza o MSE para um dado débito. Não há modelo
// psicoacústico de propósito: esse otimiza o que se ouve, não o SNR.
//
// Formato do ficheiro (little-endian)
//   "ICY1", taxa (u32), canais (u8), bits/amostra (u8), formato libsndfile (u32),
//   frames (u64), N (u16), flags (u8: bit 0 = MID/SIDE), passo Δ (f32),
//   seguido dos dados do codificador aritmético.
//   Os coeficientes vão bloco a bloco e, dentro de cada bloco, canal a canal.
#pragma once

#include "common/mdct.hpp"
#include "common/rangecoder.hpp"
#include "common/wav_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lossy {

static const uint32_t MAGIC = 0x31594349;  // "ICY1" em little-endian

struct Header {
    uint32_t sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    uint32_t format = 0;
    uint64_t frames = 0;
    int N = 2048;
    bool midSide = false;
    float step = 1.0f;

    // n.º de blocos MDCT: cada amostra tem de cair em dois blocos
    std::size_t blocks() const { return (frames + N - 1) / N + 1; }
};

// ---------------------------------------------------------------- cabeçalho

inline void putLE(std::vector<uint8_t>& o, uint64_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

inline uint64_t getLE(const uint8_t*& p, int bytes) {
    uint64_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= static_cast<uint64_t>(*p++) << (8 * i);
    return v;
}

static const std::size_t HEADER_BYTES = 4 + 4 + 1 + 1 + 4 + 8 + 2 + 1 + 4;

inline void writeHeader(std::vector<uint8_t>& o, const Header& h) {
    putLE(o, MAGIC, 4);
    putLE(o, h.sampleRate, 4);
    putLE(o, static_cast<uint64_t>(h.channels), 1);
    putLE(o, static_cast<uint64_t>(h.bitsPerSample), 1);
    putLE(o, h.format, 4);
    putLE(o, h.frames, 8);
    putLE(o, static_cast<uint64_t>(h.N), 2);
    putLE(o, h.midSide ? 1 : 0, 1);
    uint32_t s;
    std::memcpy(&s, &h.step, 4);
    putLE(o, s, 4);
}

inline Header readHeader(const std::vector<uint8_t>& data) {
    if (data.size() < HEADER_BYTES) throw std::runtime_error("ficheiro comprimido demasiado curto");
    const uint8_t* p = data.data();
    if (getLE(p, 4) != MAGIC) throw std::runtime_error("não é um ficheiro do wav_lossy_enc");
    Header h;
    h.sampleRate = static_cast<uint32_t>(getLE(p, 4));
    h.channels = static_cast<int>(getLE(p, 1));
    h.bitsPerSample = static_cast<int>(getLE(p, 1));
    h.format = static_cast<uint32_t>(getLE(p, 4));
    h.frames = getLE(p, 8);
    h.N = static_cast<int>(getLE(p, 2));
    h.midSide = (getLE(p, 1) & 1) != 0;
    const uint32_t s = static_cast<uint32_t>(getLE(p, 4));
    std::memcpy(&h.step, &s, 4);
    if (h.channels < 1 || h.N < 16 || !(h.step > 0.0f))
        throw std::runtime_error("cabeçalho inválido");
    return h;
}

// ------------------------------------------------- canais e transformadas

// Coeficientes MDCT de todos os blocos: coef[c][b*N + k].
// O bloco b cobre as amostras [(b-1)N, (b+1)N); fora do ficheiro valem 0.
// Em estéreo usa-se MID/SIDE ortonormal (se pedido):
//   M = (L + R)/sqrt(2),  S = (L - R)/sqrt(2)
// Só se guarda um bloco de amostras de cada vez; os coeficientes ficam em
// float (metade da memória de double, erro muito abaixo do da quantização).
inline std::vector<std::vector<float>> analyze(const WavData& w, const Header& h) {
    Mdct m(h.N);
    const std::size_t B = h.blocks();
    const int64_t F = static_cast<int64_t>(h.frames);
    const double r = 1.0 / std::sqrt(2.0);
    std::vector<std::vector<float>> coef(h.channels, std::vector<float>(B * h.N));
    std::vector<double> x(2 * h.N), X(h.N);
    for (std::size_t b = 0; b < B; ++b)
        for (int c = 0; c < h.channels; ++c) {
            const int64_t t0 = static_cast<int64_t>(b) * h.N - h.N;
            for (int i = 0; i < 2 * h.N; ++i) {
                const int64_t t = t0 + i;
                if (t < 0 || t >= F) { x[i] = 0.0; continue; }
                const auto n = static_cast<std::size_t>(t);
                if (h.midSide) {
                    const double L = w.at(n, 0), R = w.at(n, 1);
                    x[i] = (c == 0 ? L + R : L - R) * r;
                } else {
                    x[i] = w.at(n, c);
                }
            }
            m.forward(x.data(), X.data());
            for (int k = 0; k < h.N; ++k) coef[c][b * h.N + k] = static_cast<float>(X[k]);
        }
    return coef;
}

// Síntese bloco a bloco: recebe os coeficientes já desquantizados de cada
// bloco (todos os canais), faz a MDCT inversa e soma com a metade guardada do
// bloco anterior. Cada bloco completa N frames, que são entregues logo a
// emit(t, frame) já em L/R, arredondadas e limitadas. Só se guarda um bloco.
class Synth {
public:
    explicit Synth(const Header& h)
        : h_(h), m_(h.N), tail_(h.channels, std::vector<double>(h.N, 0.0)),
          y_(h.channels, std::vector<double>(h.N)), tmp_(2 * h.N), frame_(h.channels) {
        lo_ = -std::ldexp(1.0, h.bitsPerSample - 1);
        hi_ = -lo_ - 1.0;
    }

    // X[c] = N coeficientes do canal c para o bloco seguinte
    template <class Emit>
    void push(const std::vector<std::vector<double>>& X, Emit&& emit) {
        for (int c = 0; c < h_.channels; ++c) {
            bool zero = true;
            for (int k = 0; k < h_.N && zero; ++k) zero = X[c][k] == 0.0;
            if (zero) std::fill(tmp_.begin(), tmp_.end(), 0.0);   // silêncio: poupa a MDCT
            else m_.inverse(X[c].data(), tmp_.data());
            for (int n = 0; n < h_.N; ++n) {
                y_[c][n] = tail_[c][n] + tmp_[n];
                tail_[c][n] = tmp_[h_.N + n];
            }
        }
        // estas N frames são as amostras [(b-1)N, bN) do ficheiro
        const int64_t t0 = static_cast<int64_t>(b_++) * h_.N - h_.N;
        const double r = 1.0 / std::sqrt(2.0);
        for (int n = 0; n < h_.N; ++n) {
            const int64_t t = t0 + n;
            if (t < 0 || t >= static_cast<int64_t>(h_.frames)) continue;
            if (h_.midSide) {
                const double M = y_[0][n], S = y_[1][n];
                frame_[0] = clip((M + S) * r);
                frame_[1] = clip((M - S) * r);
            } else {
                for (int c = 0; c < h_.channels; ++c) frame_[c] = clip(y_[c][n]);
            }
            emit(static_cast<std::size_t>(t), frame_.data());
        }
    }

private:
    int32_t clip(double v) const { return static_cast<int32_t>(std::clamp(std::round(v), lo_, hi_)); }

    const Header& h_;
    Mdct m_;
    std::vector<std::vector<double>> tail_, y_;
    std::vector<double> tmp_;
    std::vector<int32_t> frame_;
    std::size_t b_ = 0;
    double lo_, hi_;
};

// Quantização escalar uniforme com zona morta: q = sinal(x) * floor(|x|/Δ + round).
// round = 0,5 é o arredondamento normal; abaixo disso, valores perto de um
// limiar vão para o nível de menor módulo, que custa menos bits.
inline int32_t quantize(float x, double invStep, double round) {
    const int32_t m = static_cast<int32_t>(std::abs(static_cast<double>(x)) * invStep + round);
    return x < 0 ? -m : m;
}

// ---------------------------------------------- modelo dos coeficientes

// Contexto de cada coeficiente = zona de frequência x "atividade" à volta.
// A atividade junta os dois coeficientes anteriores do mesmo bloco e os
// vizinhos no bloco anterior (o espetro muda devagar de bloco para bloco).
static const int N_BANDS = 6, N_LEVELS = 8, N_CTX = N_BANDS * N_LEVELS;
static const int MAX_UNARY = 14;   // magnitudes 1..14 em unário; daí para cima, escape
static const int MAX_ESC = 32;

class CoefModel {
public:
    explicit CoefModel(int N, int channels) : N_(N), band_(N) {
        // zona de frequência em escala logarítmica, independente de N
        for (int k = 0; k < N; ++k) {
            const int f = k * 32 / N;              // 0..31
            int g = 0;
            while (g < N_BANDS - 1 && (1 << g) <= f) ++g;
            band_[k] = g;
        }
        prev_.assign(channels, std::vector<int32_t>(N, 0));
    }

    template <class Fn>
    void block(int c, int32_t* q, Fn&& codeOne) {
        std::vector<int32_t>& pv = prev_[c];
        int32_t a1 = 0, a2 = 0;
        for (int k = 0; k < N_; ++k) {
            const int32_t pm = k > 0 ? pv[k - 1] : 0, pp = k + 1 < N_ ? pv[k + 1] : 0;
            const int32_t s = 2 * a1 + a2 + pv[k] + ((pm + pp) >> 1);
            const int ctx = band_[k] * N_LEVELS + level(s);
            codeOne(ctx, q[k]);
            a2 = a1;
            a1 = std::abs(q[k]);
        }
        for (int k = 0; k < N_; ++k) pv[k] = std::abs(q[k]);
    }

    Prob zero[N_CTX];
    Prob gt[N_CTX][MAX_UNARY];
    Prob esc[N_BANDS][MAX_ESC];

private:
    static int level(int32_t s) {
        if (s <= 2) return s;               // 0, 1, 2
        if (s <= 4) return 3;
        if (s <= 7) return 4;
        if (s <= 12) return 5;
        if (s <= 20) return 6;
        return 7;
    }

    int N_;
    std::vector<int> band_;
    std::vector<std::vector<int32_t>> prev_;   // |q| do bloco anterior, por canal
};

inline void encodeCoef(RangeEncoder& rc, CoefModel& m, int ctx, int32_t v) {
    const uint32_t a = static_cast<uint32_t>(std::abs(v));
    rc.encodeBit(m.zero[ctx], a != 0);
    if (a == 0) return;
    rc.encodeDirect(v < 0, 1);
    // magnitude: "maior que 1?", "maior que 2?", ... até MAX_UNARY
    uint32_t j = 1;
    for (; j <= static_cast<uint32_t>(MAX_UNARY); ++j) {
        rc.encodeBit(m.gt[ctx][j - 1], a > j);
        if (a == j) return;
    }
    // escape: Exp-Golomb de (a - MAX_UNARY - 1) + 1, expoente com modelo
    const uint32_t e = a - MAX_UNARY;           // >= 1
    int nb = 0;
    while ((e >> (nb + 1)) != 0) ++nb;          // nb = floor(log2(e))
    const int band = ctx / N_LEVELS;
    for (int i = 0; i < nb; ++i) rc.encodeBit(m.esc[band][i], 1);
    if (nb < MAX_ESC) rc.encodeBit(m.esc[band][nb], 0);
    if (nb > 0) rc.encodeDirect(e & ((1u << nb) - 1), nb);
}

inline int32_t decodeCoef(RangeDecoder& rc, CoefModel& m, int ctx) {
    if (!rc.decodeBit(m.zero[ctx])) return 0;
    const bool neg = rc.decodeDirect(1) != 0;
    uint32_t a = 1;
    while (a <= static_cast<uint32_t>(MAX_UNARY) && rc.decodeBit(m.gt[ctx][a - 1])) ++a;
    if (a > static_cast<uint32_t>(MAX_UNARY)) {
        const int band = ctx / N_LEVELS;
        int nb = 0;
        while (nb < MAX_ESC && rc.decodeBit(m.esc[band][nb])) ++nb;
        const uint32_t low = nb > 0 ? rc.decodeDirect(nb) : 0;
        a = ((1u << nb) | low) + MAX_UNARY;
    }
    return neg ? -static_cast<int32_t>(a) : static_cast<int32_t>(a);
}

// ------------------------------------------------------------- ficheiros

inline std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Erro ao abrir " + path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

inline void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Erro ao criar " + path);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

}  // namespace lossy
