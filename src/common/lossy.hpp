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
//
// Tudo funciona em fluxo, um bloco de cada vez: a análise lê o WAV aos poucos
// (Analyzer), a síntese entrega as frames à medida que ficam completas (Synth) e o
// codificador aritmético lê e escreve o ficheiro comprimido com um buffer. A memória
// não depende do tamanho do ficheiro.
#pragma once

#include "common/mdct.hpp"
#include "common/rangecoder.hpp"
#include "common/wav_stream.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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

inline Header readHeader(std::istream& in) {
    uint8_t data[HEADER_BYTES];
    in.read(reinterpret_cast<char*>(data), HEADER_BYTES);
    if (static_cast<std::size_t>(in.gcount()) < HEADER_BYTES)
        throw std::runtime_error("ficheiro comprimido demasiado curto");
    const uint8_t* p = data;
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

// Análise em fluxo: entrega, um bloco de cada vez, os N coeficientes MDCT de cada
// canal. O bloco b cobre as amostras [(b-1)N, (b+1)N); fora do ficheiro valem 0.
// Só se guarda a janela atual de 2N frames: a cada bloco, a segunda metade passa a
// primeira e lêem-se N frames novas do ficheiro.
// Em estéreo usa-se MID/SIDE ortonormal (se pedido):
//   M = (L + R)/sqrt(2),  S = (L - R)/sqrt(2)
// Os coeficientes saem arredondados para float, como na versão que os guardava a
// todos, para o resultado ser o mesmo bit a bit.
class Analyzer {
public:
    Analyzer(WavReader& in, const Header& h)
        : in_(in), h_(h), m_(h.N), win_(2 * static_cast<std::size_t>(h.N) * h.channels, 0),
          x_(2 * h.N), X_(h.N) {
        readHalf();                                   // bloco 0: [-N, N), metade de cima
    }

    // Calcula os coeficientes do bloco seguinte; devolve false quando acabam.
    bool next(std::vector<std::vector<float>>& coef) {
        if (b_ == h_.blocks()) return false;
        if (b_ > 0) {                                 // avança meia janela
            const std::size_t half = static_cast<std::size_t>(h_.N) * h_.channels;
            std::copy(win_.begin() + static_cast<long>(half), win_.end(), win_.begin());
            readHalf();
        }
        const double r = 1.0 / std::sqrt(2.0);
        const int C = h_.channels;
        for (int c = 0; c < C; ++c) {
            for (int i = 0; i < 2 * h_.N; ++i) {
                const int32_t* f = &win_[static_cast<std::size_t>(i) * C];
                if (h_.midSide) {
                    const double L = f[0], R = f[1];
                    x_[i] = (c == 0 ? L + R : L - R) * r;
                } else {
                    x_[i] = f[c];
                }
            }
            m_.forward(x_.data(), X_.data());
            for (int k = 0; k < h_.N; ++k) coef[c][k] = static_cast<float>(X_[k]);
        }
        ++b_;
        return true;
    }

    // Frames originais [(b-1)N, bN) do último bloco calculado: são as que a síntese
    // entrega a seguir, e o codificador compara-as para medir o SNR real.
    const int32_t* firstHalf() const { return win_.data(); }

    // Soma de x^2 de todas as amostras lidas (para o SNR alvo)
    double signalEnergy() const { return sig_; }

private:
    // Lê N frames para a segunda metade da janela (zeros depois do fim do ficheiro).
    void readHalf() {
        const std::size_t half = static_cast<std::size_t>(h_.N) * h_.channels;
        int32_t* dst = win_.data() + half;
        std::size_t lidas = 0;
        if (lidosTotal_ < h_.frames) {
            const auto pedir = static_cast<std::size_t>(
                std::min<uint64_t>(static_cast<uint64_t>(h_.N), h_.frames - lidosTotal_));
            lidas = in_.readFrames(dst, pedir);
            lidosTotal_ += lidas;
        }
        const std::size_t amostras = lidas * h_.channels;
        for (std::size_t i = 0; i < amostras; ++i) sig_ += static_cast<double>(dst[i]) * dst[i];
        std::fill(dst + amostras, dst + half, 0);
    }

    WavReader& in_;
    const Header& h_;
    Mdct m_;
    std::vector<int32_t> win_;          // 2N frames intercaladas
    std::vector<double> x_, X_;
    std::size_t b_ = 0;
    uint64_t lidosTotal_ = 0;
    double sig_ = 0.0;
};

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

// Fila de blocos entre duas threads, para a síntese (MDCT inversa, L/R e escrita ou
// medida do erro) correr ao mesmo tempo que a análise e a codificação. Tem SLOTS
// lugares reutilizados: o produtor espera quando estão todos cheios, o consumidor
// quando estão todos vazios. Cada thread processa os blocos sempre pela mesma
// ordem, por isso o resultado é exatamente o mesmo da versão sem threads.
template <class Item>
class BlockPipe {
public:
    BlockPipe(std::size_t slots, const Item& modelo) : slots_(slots, modelo) {}

    // Produtor: lugar livre para preencher; depois push().
    Item& slot() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return head_ - tail_ < slots_.size() || closed_; });
        return slots_[head_ % slots_.size()];
    }
    void push() {
        { std::lock_guard<std::mutex> lk(m_); ++head_; }
        cv_.notify_all();
    }
    // Consumidor: próximo bloco, ou nullptr quando o produtor fechou e acabou.
    Item* front() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return head_ > tail_ || closed_; });
        return head_ > tail_ ? &slots_[tail_ % slots_.size()] : nullptr;
    }
    void pop() {
        { std::lock_guard<std::mutex> lk(m_); ++tail_; }
        cv_.notify_all();
    }
    void close() {
        { std::lock_guard<std::mutex> lk(m_); closed_ = true; }
        cv_.notify_all();
    }

private:
    std::vector<Item> slots_;
    std::size_t head_ = 0, tail_ = 0;
    bool closed_ = false;
    std::mutex m_;
    std::condition_variable cv_;
};

// Corre fn() numa thread e garante que ela termina (fechando a fila) antes de sair
// do âmbito, mesmo que o produtor lance uma exceção. join() devolve a exceção
// que a thread tenha lançado.
template <class Item>
class Consumer {
public:
    template <class Fn>
    Consumer(BlockPipe<Item>& pipe, Fn fn) : pipe_(pipe) {
        t_ = std::thread([this, fn]() mutable {
            try {
                fn();
            } catch (...) {
                err_ = std::current_exception();
                pipe_.close();   // o produtor deixa de esperar por lugares livres
            }
        });
    }
    ~Consumer() { stop(); }
    void join() {
        stop();
        if (err_) std::rethrow_exception(err_);
    }

private:
    void stop() {
        if (t_.joinable()) { pipe_.close(); t_.join(); }
    }
    BlockPipe<Item>& pipe_;
    std::thread t_;
    std::exception_ptr err_;
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

}  // namespace lossy
