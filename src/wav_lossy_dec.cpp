// wav_lossy_dec — descodificador do wav_lossy_enc.
//
// Lê o cabeçalho, descodifica os coeficientes quantizados com o mesmo modelo
// adaptativo do codificador, multiplica-os pelo passo, aplica a MDCT inversa
// com sobreposição e volta de MID/SIDE a L/R (ver common/lossy.hpp).
//
// Tudo em fluxo: lê o ficheiro comprimido com um buffer, descodifica um bloco de
// cada vez e escreve no WAV as frames assim que ficam completas. A memória não
// depende do tamanho do ficheiro. O WAV de saída tem o cabeçalho canónico de 44
// bytes (common/wav_header.hpp), igual ao que a libsndfile escrevia.
//
// Uso: wav_lossy_dec comprimido output.wav
#include "common/lossy.hpp"
#include "common/wav_header.hpp"
#include "common/wav_stream.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace lossy;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Uso: wav_lossy_dec comprimido output.wav\n";
        return 1;
    }

    try {
        std::ifstream in(argv[1], std::ios::binary);
        if (!in) throw std::runtime_error(std::string("Erro ao abrir ") + argv[1]);
        const Header h = readHeader(in);
        if (h.bitsPerSample % 8 != 0 || h.bitsPerSample < 8 || h.bitsPerSample > 32)
            throw std::runtime_error("cabeçalho inválido");

        RangeDecoder rc(in);
        CoefModel model(h.N, h.channels);
        const WavEnvelope env =
            canonicalWavEnvelope(h.channels, static_cast<int>(h.sampleRate), h.bitsPerSample, h.frames);
        WavWriterStream out(argv[2], env, h.bitsPerSample);

        // Thread da síntese: MDCT inversa, L/R e escrita do WAV, ao mesmo tempo que
        // esta thread descodifica os blocos seguintes.
        using Item = std::vector<std::vector<double>>;
        BlockPipe<Item> pipe(4, Item(h.channels, std::vector<double>(h.N)));
        Consumer<Item> synthThread(pipe, [&] {
            Synth synth(h);
            std::vector<int32_t> frames;
            frames.reserve(static_cast<std::size_t>(h.N) * h.channels);
            while (Item* X = pipe.front()) {
                frames.clear();
                synth.push(*X, [&](std::size_t, const int32_t* fr) {
                    frames.insert(frames.end(), fr, fr + h.channels);
                });
                out.writeSamples(frames.data(), frames.size());
                pipe.pop();
            }
        });

        // um bloco de cada vez: descodificar e desquantizar
        std::vector<int32_t> q(h.N);
        for (std::size_t b = 0; b < h.blocks(); ++b) {
            Item& X = pipe.slot();
            for (int c = 0; c < h.channels; ++c) {
                // o modelo pede um coeficiente de cada vez, pela ordem em que
                // foram escritos, e usa os já descodificados como contexto
                model.block(c, q.data(), [&](int ctx, int32_t& v) { v = decodeCoef(rc, model, ctx); });
                for (int k = 0; k < h.N; ++k) X[c][k] = q[k] * static_cast<double>(h.step);
            }
            pipe.push();
        }
        synthThread.join();
        out.finish(env);
        std::cout << argv[1] << " -> " << argv[2] << ": " << h.frames << " frames\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
