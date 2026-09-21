// test_midside — verifica que L e R se recuperam exatamente de MID e SIDE.
// Uso: test_midside [ficheiro.wav]
//   Sem argumentos testa todos os pares de valores de 16 bits num intervalo;
//   com um ficheiro estéreo testa todas as suas amostras.

#include "common/channels.hpp"
#include "common/wav_io.hpp"

#include <iostream>

static bool check(int32_t L, int32_t R) {
    int32_t l, r;
    fromMidSide(midOf(L, R), sideOf(L, R), l, r);
    if (l != L || r != R) {
        std::cerr << "FALHOU: L=" << L << " R=" << R << " -> " << l << ", " << r << "\n";
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        WavData w = readWav(argv[1]);
        if (w.channels != 2) { std::cerr << "Precisa de um ficheiro estéreo\n"; return 1; }
        for (std::size_t n = 0; n < w.frames(); ++n)
            if (!check(w.at(n, 0), w.at(n, 1))) return 1;
        std::cout << "OK: " << w.frames() << " pares de amostras reconstruídos exatamente\n";
        return 0;
    }
    // Extremos de 16 bits e uma grelha de valores (inclui negativos e ímpares)
    for (int32_t L = -32768; L <= 32767; L += 257)
        for (int32_t R = -32768; R <= 32767; R += 263)
            if (!check(L, R)) return 1;
    for (int32_t L : {-32768, -1, 0, 1, 32767})
        for (int32_t R : {-32768, -1, 0, 1, 32767})
            if (!check(L, R)) return 1;
    std::cout << "OK: reconstrução MID/SIDE exata\n";
    return 0;
}
