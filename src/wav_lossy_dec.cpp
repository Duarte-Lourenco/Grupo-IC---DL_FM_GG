// wav_lossy_dec — descodificador do wav_lossy_enc.
//
// Lê o cabeçalho, descodifica os coeficientes quantizados com o mesmo modelo
// adaptativo do codificador, multiplica-os pelo passo, aplica a MDCT inversa
// com sobreposição e volta de MID/SIDE a L/R (ver common/lossy.hpp).
//
// Uso: wav_lossy_dec comprimido output.wav
#include "common/lossy.hpp"
#include "common/wav_io.hpp"

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
        const std::vector<uint8_t> data = readFile(argv[1]);
        const Header h = readHeader(data);

        RangeDecoder rc(data.data() + HEADER_BYTES, data.size() - HEADER_BYTES);
        CoefModel model(h.N, h.channels);
        Synth synth(h);

        WavData out;
        out.channels = h.channels;
        out.sampleRate = static_cast<int>(h.sampleRate);
        out.bitsPerSample = h.bitsPerSample;
        out.format = static_cast<int>(h.format);
        out.samples.resize(h.frames * h.channels);

        // um bloco de cada vez: descodificar, desquantizar, MDCT inversa
        std::vector<int32_t> q(h.N);
        std::vector<std::vector<double>> X(h.channels, std::vector<double>(h.N));
        for (std::size_t b = 0; b < h.blocks(); ++b) {
            for (int c = 0; c < h.channels; ++c) {
                // o modelo pede um coeficiente de cada vez, pela ordem em que
                // foram escritos, e usa os já descodificados como contexto
                model.block(c, q.data(), [&](int ctx, int32_t& v) { v = decodeCoef(rc, model, ctx); });
                for (int k = 0; k < h.N; ++k) X[c][k] = q[k] * static_cast<double>(h.step);
            }
            synth.push(X, [&](std::size_t t, const int32_t* fr) {
                for (int c = 0; c < h.channels; ++c) out.samples[t * h.channels + c] = fr[c];
            });
        }

        writeWav(argv[2], out);
        std::cout << argv[1] << " -> " << argv[2] << ": " << h.frames << " frames\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
