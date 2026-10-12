// golomb.hpp — códigos de Golomb/Rice para inteiros com sinal.
//
// Valores pequenos ocupam poucos bits, valores grandes ocupam mais. Depois da
// predição, os resíduos de áudio são quase todos pequenos, por isso o ganho é
// grande. Esta versão exige m potência de 2 (código de Rice): a divisão e o
// resto passam a ser um deslocamento e uma máscara.
//
// Código de um valor: quociente em unário (q uns + um zero) seguido do resto
// em binário com log2(m) bits. É um código prefixo, por isso o descodificador
// não precisa de saber antecipadamente quantos bits o valor ocupou.
#pragma once

#include "common/bitstream.hpp"

#include <cstdint>
#include <stdexcept>

// Inteiro com sinal -> sem sinal: 0,-1,1,-2,2,... -> 0,1,2,3,4,...
inline uint64_t zigzag(int64_t v) {
    return v >= 0 ? 2 * static_cast<uint64_t>(v)
                  : 2 * static_cast<uint64_t>(-v) - 1;
}
inline int64_t unzigzag(uint64_t u) {
    return (u & 1) ? -static_cast<int64_t>((u + 1) / 2)
                   :  static_cast<int64_t>(u / 2);
}

class Golomb {
public:
    explicit Golomb(uint32_t m) : m_(m) {
        if (m == 0 || (m & (m - 1)) != 0)
            throw std::runtime_error("Golomb: m tem de ser potência de 2");
        while ((1u << bits_) < m_) ++bits_;        // bits_ = log2(m)
    }

    void encode(BitWriter& w, int64_t value) const {
        const uint64_t u = zigzag(value);
        const uint64_t q = u >> bits_;             // quociente
        const uint64_t r = u & (m_ - 1);           // resto
        for (uint64_t i = 0; i < q; ++i) w.writeBit(1);
        w.writeBit(0);                             // fim do unário
        if (bits_ > 0) w.writeBits(r, static_cast<int>(bits_));
    }

    int64_t decode(BitReader& r) const {
        uint64_t q = 0;
        while (r.readBit() == 1) ++q;
        const uint64_t resto = bits_ > 0 ? r.readBits(static_cast<int>(bits_)) : 0;
        return unzigzag(q * m_ + resto);
    }

    uint32_t m() const { return m_; }

    // m sugerido para um bloco, a partir da média dos valores em zigzag:
    // a potência de 2 mais próxima dessa média (mínimo 1).
    static uint32_t bestM(double mediaZigzag) {
        uint32_t m = 1;
        while (m < 1u << 30 && static_cast<double>(m) * 2.0 <= mediaZigzag) m <<= 1;
        return m;
    }

private:
    uint32_t m_;
    unsigned bits_ = 0;
};