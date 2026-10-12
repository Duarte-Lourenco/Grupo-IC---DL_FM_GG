// wav_lossy_enc — codificador de áudio com perdas baseado na DCT (MDCT).
//
// Cadeia (ver common/lossy.hpp): MID/SIDE -> MDCT -> quantização uniforme com
// zona morta -> codificação aritmética adaptativa.
//
// A qualidade é controlada por um único número, o passo de quantização Δ,
// igual para todos os coeficientes. Pode ser dado diretamente (-q), ou
// procurado por bisseção para atingir um débito (-r) ou um SNR (-s).
//
// Uso: wav_lossy_enc [opções] input.wav comprimido
//   -r KBPS   débito alvo em kbit/s (omissão: 128)
//   -s DB     SNR alvo em dB (em vez de -r)
//   -q PASSO  passo de quantização fixo (em vez de -r / -s)
//   -n N      coeficientes por bloco MDCT, potência de 2 (omissão 2048)
//   -z R      arredondamento da quantização, 0 < R <= 0.5 (omissão 0.40;
//             0.5 = arredondamento normal, menos = zona morta maior)
//   -l        não usar MID/SIDE (codifica L e R)
#include "common/lossy.hpp"
#include "common/metrics.hpp"
#include "common/wav_io.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace lossy;

// Quantiza e codifica todos os coeficientes com o passo h.step.
// Devolve os bytes (cabeçalho incluído) e, em errEnergy, a soma dos erros
// quadráticos nos coeficientes (= erro no áudio, porque tudo é ortonormal).
static std::vector<uint8_t> encodeAll(const std::vector<std::vector<float>>& coef, const Header& h,
                                      double round, double& errEnergy) {
    std::vector<uint8_t> out;
    writeHeader(out, h);
    RangeEncoder rc(out);
    CoefModel model(h.N, h.channels);
    std::vector<int32_t> q(h.N);
    const double inv = 1.0 / h.step, step = h.step;
    errEnergy = 0.0;

    for (std::size_t b = 0; b < h.blocks(); ++b)
        for (int c = 0; c < h.channels; ++c) {
            const float* X = &coef[c][b * h.N];
            for (int k = 0; k < h.N; ++k) {
                q[k] = quantize(X[k], inv, round);
                const double e = X[k] - q[k] * step;
                errEnergy += e * e;
            }
            model.block(c, q.data(), [&](int ctx, int32_t v) { encodeCoef(rc, model, ctx, v); });
        }
    rc.finish();
    return out;
}

int main(int argc, char* argv[]) {
    double kbps = 128.0, snrTarget = -1.0, fixedStep = -1.0, round = 0.40;
    int N = 2048;
    bool allowMS = true;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasVal = i + 1 < argc;
        if (a == "-r" && hasVal) kbps = std::atof(argv[++i]);
        else if (a == "-s" && hasVal) snrTarget = std::atof(argv[++i]);
        else if (a == "-q" && hasVal) fixedStep = std::atof(argv[++i]);
        else if (a == "-n" && hasVal) N = std::atoi(argv[++i]);
        else if (a == "-z" && hasVal) round = std::atof(argv[++i]);
        else if (a == "-l") allowMS = false;
        else pos.push_back(a);
    }
    if (pos.size() != 2 || N < 16 || N > 32768 || (N & (N - 1)) != 0 || round <= 0.0 ||
        round > 0.5 || kbps <= 0.0) {
        std::cerr << "Uso: wav_lossy_enc [-r KBPS | -s SNR_DB | -q PASSO] [-n N] [-z R] [-l] "
                     "input.wav comprimido\n";
        return 1;
    }

    try {
        const auto t0 = std::chrono::steady_clock::now();
        const WavData in = readWav(pos[0]);

        Header h;
        h.sampleRate = static_cast<uint32_t>(in.sampleRate);
        h.channels = in.channels;
        h.bitsPerSample = in.bitsPerSample;
        h.format = static_cast<uint32_t>(in.format);
        h.frames = in.frames();
        h.N = N;
        h.midSide = allowMS && in.channels == 2;

        const auto coef = analyze(in, h);

        // energia do sinal (para o SNR alvo), no domínio original
        double sigEnergy = 0.0;
        for (const int32_t s : in.samples) sigEnergy += static_cast<double>(s) * s;
        const double seconds = static_cast<double>(h.frames) / h.sampleRate;
        auto rateOf = [&](std::size_t bytes) { return bytes * 8.0 / seconds / 1000.0; };

        double err = 0.0;
        if (fixedStep > 0.0) {
            h.step = static_cast<float>(fixedStep);
        } else {
            // Bisseção em log2(Δ): o débito desce e o erro sobe com Δ.
            double lo = -8.0, hi = 20.0;
            for (int it = 0; it < 22; ++it) {
                const double mid = 0.5 * (lo + hi);
                h.step = static_cast<float>(std::exp2(mid));
                const std::size_t bytes = encodeAll(coef, h, round, err).size();
                const bool tooGood = snrTarget > 0.0
                                         ? 10.0 * std::log10(sigEnergy / std::max(err, 1e-300)) > snrTarget
                                         : rateOf(bytes) > kbps;
                (tooGood ? lo : hi) = mid;
            }
            // fica com o lado que cumpre o alvo (débito <= alvo, ou SNR >= alvo)
            h.step = static_cast<float>(std::exp2(snrTarget > 0.0 ? lo : hi));
        }

        const std::vector<uint8_t> out = encodeAll(coef, h, round, err);
        writeFile(pos[1], out);
        const double tEnc = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        // SNR real (depois de arredondar e limitar as amostras), com metrics.hpp:
        // faz a mesma síntese que o descodificador, sem guardar o resultado
        Stats st;
        Synth synth(h);
        std::vector<std::vector<double>> Xq(h.channels, std::vector<double>(h.N));
        const double inv = 1.0 / h.step;
        for (std::size_t b = 0; b < h.blocks(); ++b) {
            for (int c = 0; c < h.channels; ++c)
                for (int k = 0; k < h.N; ++k)
                    Xq[c][k] = quantize(coef[c][b * h.N + k], inv, round) * static_cast<double>(h.step);
            synth.push(Xq, [&](std::size_t t, const int32_t* fr) {
                for (int c = 0; c < h.channels; ++c) st.add(in.at(t, c), fr[c]);
            });
        }

        std::cout << std::fixed << std::setprecision(2) << pos[0] << " -> " << pos[1] << ": N=" << N
                  << (h.midSide ? " MID/SIDE" : " L/R") << " passo=" << std::setprecision(3)
                  << h.step << std::setprecision(2) << "  " << out.size() << " bytes  "
                  << rateOf(out.size()) << " kbps  taxa " << std::setprecision(1)
                  << static_cast<double>(in.samples.size()) * in.bitsPerSample / 8.0 / out.size()
                  << ":1  SNR " << std::setprecision(2) << st.snr() << " dB  (" << tEnc << " s)\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
