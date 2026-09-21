// channels.hpp — canais MID e SIDE.
//
// CONVENÇÃO DO GRUPO: toda a divisão inteira por 2 é feita com >> 1,
// ou seja, arredonda para -infinito (como o // do Python).
// Em C++, "/" arredonda para zero: -3 / 2 == -1, mas -3 >> 1 == -2.
// Usar SEMPRE estas funções para não haver versões diferentes no código.
#pragma once

#include <cstdint>

// MID = (L + R) / 2
inline int32_t midOf(int32_t L, int32_t R) {
    return static_cast<int32_t>((static_cast<int64_t>(L) + R) >> 1);
}

// Diferença média = (L - R) / 2   (o histograma pedido no enunciado)
inline int32_t halfSideOf(int32_t L, int32_t R) {
    return static_cast<int32_t>((static_cast<int64_t>(L) - R) >> 1);
}

// SIDE = L - R   (sem divisão: é o que se guarda num codec)
inline int32_t sideOf(int32_t L, int32_t R) {
    return static_cast<int32_t>(static_cast<int64_t>(L) - R);
}

// Reconstrução exata a partir de MID e SIDE (nota de rodapé 2 do enunciado).
// L + R e L - R têm a mesma paridade, logo o bit perdido em MID é o bit
// menos significativo de SIDE:  L + R = 2*MID + (SIDE & 1).
inline void fromMidSide(int32_t mid, int32_t side, int32_t& L, int32_t& R) {
    int64_t sum = 2 * static_cast<int64_t>(mid) + (side & 1);
    L = static_cast<int32_t>((sum + side) >> 1);
    R = static_cast<int32_t>(L - static_cast<int64_t>(side));
}
