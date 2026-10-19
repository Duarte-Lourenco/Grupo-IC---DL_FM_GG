// rangecoder.hpp — codificador aritmético binário adaptativo (estilo LZMA).
//
// Porquê aritmético e não Rice no codec com perdas: depois de quantizados, a
// maior parte dos coeficientes da MDCT é 0 (sobretudo nas altas frequências).
// Um código de Rice gasta no mínimo 1 bit por valor, mesmo que seja 0; o
// codificador aritmético gasta -log2(p) bits, que pode ser 0,01 bit quando o
// modelo já sabe que ali quase tudo é 0.
//
// Cada decisão binária tem uma probabilidade (Prob) que se adapta sozinha aos
// dados: o descodificador faz as mesmas atualizações, por isso não é preciso
// guardar nenhuma tabela no ficheiro.
#pragma once

#include <cstdint>
#include <istream>
#include <stdexcept>
#include <vector>

// Probabilidade de o bit ser 0, em 11 bits (0..2048). Começa em 1/2.
struct Prob {
    uint16_t p = 1024;
};

static constexpr int RC_PROB_BITS = 11;
static constexpr int RC_ADAPT = 4;          // velocidade de adaptação (maior = mais lenta)
static constexpr uint32_t RC_TOP = 1u << 24;

// Os bytes vão para o vetor out. Os bytes que já lá estão nunca mais mudam (o
// transporte só pode alterar os que ainda estão em cache_), por isso quem usa o
// codificador pode escrevê-los no ficheiro e esvaziar o vetor quando quiser: a
// memória fica limitada, qualquer que seja o tamanho do ficheiro.
class RangeEncoder {
public:
    explicit RangeEncoder(std::vector<uint8_t>& out) : out_(out) {}

    void encodeBit(Prob& pr, int bit) {
        const uint32_t bound = (range_ >> RC_PROB_BITS) * pr.p;
        if (bit == 0) {
            range_ = bound;
            pr.p += ((1u << RC_PROB_BITS) - pr.p) >> RC_ADAPT;
        } else {
            low_ += bound;
            range_ -= bound;
            pr.p -= pr.p >> RC_ADAPT;
        }
        while (range_ < RC_TOP) {
            range_ <<= 8;
            shiftLow();
        }
    }

    // Bits sem modelo (probabilidade 1/2), para o sinal e para a parte baixa
    // de valores grandes, que são praticamente aleatórios.
    void encodeDirect(uint32_t value, int n) {
        for (int i = n - 1; i >= 0; --i) {
            range_ >>= 1;
            if ((value >> i) & 1) low_ += range_;
            while (range_ < RC_TOP) {
                range_ <<= 8;
                shiftLow();
            }
        }
    }

    void finish() {
        for (int i = 0; i < 5; ++i) shiftLow();
    }

private:
    // Envia o byte de cima de low_, tratando o "transporte" (carry) que pode
    // ainda alterar bytes já decididos (guardados em cache_ / pending_).
    void shiftLow() {
        if (static_cast<uint32_t>(low_) < 0xFF000000u || (low_ >> 32) != 0) {
            const uint8_t carry = static_cast<uint8_t>(low_ >> 32);
            uint8_t temp = cache_;
            do {
                out_.push_back(static_cast<uint8_t>(temp + carry));
                temp = 0xFF;
            } while (--pending_ != 0);
            cache_ = static_cast<uint8_t>(low_ >> 24);
        }
        ++pending_;
        low_ = (low_ & 0x00FFFFFFu) << 8;
    }

    std::vector<uint8_t>& out_;
    uint64_t low_ = 0;
    uint32_t range_ = 0xFFFFFFFFu;
    uint8_t cache_ = 0;
    uint64_t pending_ = 1;
};

// Lê os bytes de um fluxo (ficheiro), com um buffer de 64 KiB: a memória não
// depende do tamanho do ficheiro comprimido.
class RangeDecoder {
public:
    explicit RangeDecoder(std::istream& in) : in_(in), buf_(64 * 1024) {
        for (int i = 0; i < 5; ++i) code_ = (code_ << 8) | nextByte();
    }

    int decodeBit(Prob& pr) {
        const uint32_t bound = (range_ >> RC_PROB_BITS) * pr.p;
        int bit;
        if (code_ < bound) {
            range_ = bound;
            pr.p += ((1u << RC_PROB_BITS) - pr.p) >> RC_ADAPT;
            bit = 0;
        } else {
            code_ -= bound;
            range_ -= bound;
            pr.p -= pr.p >> RC_ADAPT;
            bit = 1;
        }
        while (range_ < RC_TOP) {
            range_ <<= 8;
            code_ = (code_ << 8) | nextByte();
        }
        return bit;
    }

    uint32_t decodeDirect(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            range_ >>= 1;
            int bit = 0;
            if (code_ >= range_) {
                code_ -= range_;
                bit = 1;
            }
            v = (v << 1) | static_cast<uint32_t>(bit);
            while (range_ < RC_TOP) {
                range_ <<= 8;
                code_ = (code_ << 8) | nextByte();
            }
        }
        return v;
    }

private:
    uint8_t nextByte() {
        if (pos_ == len_) {
            in_.read(reinterpret_cast<char*>(buf_.data()), static_cast<std::streamsize>(buf_.size()));
            len_ = static_cast<std::size_t>(in_.gcount());
            pos_ = 0;
            if (len_ == 0) throw std::runtime_error("RangeDecoder: fim de dados inesperado");
        }
        return buf_[pos_++];
    }

    std::istream& in_;
    std::vector<uint8_t> buf_;
    std::size_t pos_ = 0, len_ = 0;
    uint32_t code_ = 0;
    uint32_t range_ = 0xFFFFFFFFu;
};
