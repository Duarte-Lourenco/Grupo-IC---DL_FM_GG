// wav_quant — quantização escalar uniforme (reduz o n.º de bits por amostra).
// Uso: wav_quant -b BITS [-t] entrada.wav saida.wav
//   -b BITS : n.º de bits que sobram por amostra (1 <= BITS <= bits do ficheiro)
//   -t      : reconstrói no início do intervalo (truncar); por omissão usa o
//             ponto médio do intervalo, que dá menor erro.
//
// O ficheiro de saída mantém o formato do original (p. ex. 16 bits): só passa a
// haver 2^BITS valores diferentes. É assim que se pode medir o erro com o wav_cmp.
//
// Quantizador uniforme com passo D = 2^d, onde d = bits do ficheiro - BITS:
//   índice        q = floor(s / D) = s >> d        (>> arredonda para -infinito,
//                                                   como a convenção do grupo)
//   reconstrução  y = q * D              (truncar)
//                 y = q * D + D / 2      (ponto médio)
// Nos dois casos y fica dentro do mesmo intervalo de s, por isso nunca sai do
// intervalo de valores do ficheiro (não é preciso limitar/saturar).

#include "common/wav_io.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

static void usage() {
    std::cerr << "Uso: wav_quant -b BITS [-t] entrada.wav saida.wav\n"
                 "  -b BITS : bits por amostra na saída (1 <= BITS <= bits do ficheiro)\n"
                 "  -t      : truncar em vez de usar o ponto médio do intervalo\n";
}

int main(int argc, char* argv[]) {
    int bits = 0;
    bool truncate = false;
    std::string inPath, outPath;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-b" && i + 1 < argc) {
            char* end = nullptr;
            bits = static_cast<int>(std::strtol(argv[++i], &end, 10));
            if (*end != '\0') bits = 0;   // "8x" ou "abc" -> inválido
        } else if (a == "-t") {
            truncate = true;
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (inPath.empty()) {
            inPath = a;
        } else if (outPath.empty()) {
            outPath = a;
        } else {
            usage();
            return 1;
        }
    }
    if (bits == 0 || inPath.empty() || outPath.empty()) {
        usage();
        return 1;
    }

    try {
        WavData w = readWav(inPath);

        if (bits < 1 || bits > w.bitsPerSample) {
            std::cerr << "Erro: BITS tem de estar entre 1 e " << w.bitsPerSample
                      << " (bits por amostra do ficheiro)\n";
            return 1;
        }

        const int d = w.bitsPerSample - bits;                        // bits descartados
        const int64_t half = (truncate || d == 0) ? 0 : (int64_t{1} << (d - 1));

        for (auto& s : w.samples) {
            const int64_t q = static_cast<int64_t>(s) >> d;          // índice do intervalo
            s = static_cast<int32_t>((q << d) + half);               // valor reconstruído
        }

        writeWav(outPath, w);

        std::cout << inPath << ": " << w.bitsPerSample << " -> " << bits << " bits ("
                  << (int64_t{1} << bits) << " níveis, passo " << (int64_t{1} << d) << ", "
                  << (truncate ? "truncar" : "ponto médio") << ")\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
