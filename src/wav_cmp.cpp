// wav_cmp — compara um ficheiro WAV com o original.
// Uso: wav_cmp original.wav modificado.wav
//
// Para cada canal (L, R, ...) e, se o áudio for estéreo, para o MID = (L+R)/2,
// imprime:
//   MSE  = média de (x - y)^2          (norma L2)
//   Linf = máximo de |x - y|           (norma L-infinito)
//   SNR  = 10 * log10( soma(x^2) / soma((x - y)^2) )   em dB
// onde x = original e y = modificado.
//
// Os dois ficheiros são lidos em fluxo, um bloco de frames de cada vez, e as somas
// são acumuladas pela mesma ordem de sempre (amostra a amostra, por canal). Assim a
// memória não depende do tamanho dos ficheiros e os resultados são os mesmos, até ao
// último algarismo, da versão que carregava os ficheiros inteiros.

#include "common/channels.hpp"
#include "common/metrics.hpp"
#include "common/wav_stream.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Uso: wav_cmp original.wav modificado.wav\n";
        return 1;
    }

    try {
        WavReader orig(argv[1]);
        WavReader mod(argv[2]);

        if (orig.channels != mod.channels) {
            std::cerr << "Erro: n.º de canais diferente (" << orig.channels << " vs "
                      << mod.channels << ")\n";
            return 1;
        }
        if (orig.frames != mod.frames) {
            std::cerr << "Erro: n.º de frames diferente (" << orig.frames << " vs "
                      << mod.frames << ")\n";
            return 1;
        }
        if (orig.sampleRate != mod.sampleRate)
            std::cerr << "Aviso: taxas de amostragem diferentes (" << orig.sampleRate << " vs "
                      << mod.sampleRate << ")\n";
        if (orig.bitsPerSample != mod.bitsPerSample)
            std::cerr << "Aviso: bits por amostra diferentes (" << orig.bitsPerSample << " vs "
                      << mod.bitsPerSample << ")\n";

        const int C = orig.channels;

        std::cout << "frames: " << orig.frames << "   canais: " << C << "   taxa: " << orig.sampleRate
                  << " Hz   bits: " << orig.bitsPerSample << "\n";

        // Um Stats por canal e, em estéreo, um para o MID (convenção >> 1 do grupo)
        std::vector<Stats> ch(static_cast<std::size_t>(C));
        Stats mid;

        constexpr std::size_t BLOCO = 16384;                          // frames por leitura
        std::vector<int32_t> x(BLOCO * static_cast<std::size_t>(C)), y(x.size());
        uint64_t falta = orig.frames;
        while (falta > 0) {
            const auto pedir = static_cast<std::size_t>(std::min<uint64_t>(falta, BLOCO));
            const std::size_t n = std::min(orig.readFrames(x.data(), pedir),
                                           mod.readFrames(y.data(), pedir));
            if (n == 0) break;
            for (std::size_t i = 0; i < n; ++i) {
                const int32_t* a = &x[i * static_cast<std::size_t>(C)];
                const int32_t* b = &y[i * static_cast<std::size_t>(C)];
                for (int c = 0; c < C; ++c) ch[static_cast<std::size_t>(c)].add(a[c], b[c]);
                if (C == 2) mid.add(midOf(a[0], a[1]), midOf(b[0], b[1]));
            }
            falta -= n;
        }

        const std::string names[] = {"L", "R"};
        for (int c = 0; c < C; ++c)
            ch[static_cast<std::size_t>(c)].print(C == 2 ? names[c] : "canal" + std::to_string(c));
        if (C == 2) mid.print("MID");
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
