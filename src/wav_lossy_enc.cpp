// wav_lossy_enc — codificador de áudio com perdas baseado na DCT (MDCT).
//
// Cadeia (ver common/lossy.hpp): MID/SIDE -> MDCT -> quantização uniforme com
// zona morta -> codificação aritmética adaptativa.
//
// A qualidade é controlada por um único número, o passo de quantização Δ,
// igual para todos os coeficientes. Pode ser dado diretamente (-q), ou
// procurado para atingir um débito (-r) ou um SNR (-s).
//
// Tudo é feito em fluxo, um bloco de cada vez, sem guardar o ficheiro nem os
// coeficientes: a memória não depende do tamanho do ficheiro. Por isso a procura
// de Δ não pode codificar 22 vezes coeficientes guardados, como na 1.ª versão.
// Em vez disso:
//   1. uma passagem pelo ficheiro guarda só os coeficientes de uma amostra de
//      blocos espalhados pela música (memória fixa) e, com eles, uma bisseção
//      barata dá um Δ aproximado e a inclinação débito/Δ;
//   2. passagens completas (ler, MDCT, quantizar, codificar) afinam Δ pelo método
//      da secante, com um intervalo que garante a convergência, até o débito
//      ficar abaixo do alvo e a menos de 0,05 % dele (ou o SNR acima do alvo e a
//      menos de 0,005 dB). Costumam bastar 2 a 4 passagens.
// Cada passagem completa escreve o resultado num ficheiro temporário, que passa a
// ser a saída quando é a melhor solução encontrada até ali.
//
// Uso: wav_lossy_enc [opções] input.wav comprimido
//   -r KBPS   débito alvo em kbit/s (omissão: 128)
//   -s DB     SNR alvo em dB (em vez de -r)
//   -q PASSO  passo de quantização fixo (em vez de -r / -s): uma só passagem
//   -n N      coeficientes por bloco MDCT, potência de 2 (omissão 2048)
//   -z R      arredondamento da quantização, 0 < R <= 0.5 (omissão 0.40;
//             0.5 = arredondamento normal, menos = zona morta maior)
//   -l        não usar MID/SIDE (codifica L e R)
#include "common/lossy.hpp"
#include "common/metrics.hpp"
#include "common/wav_stream.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace lossy;

// Tolerâncias da procura: débito entre (1 - TOL_RATE) * alvo e o alvo; SNR entre
// o alvo e o alvo + TOL_SNR. Perder 0,05 % de débito custa cerca de 0,005 dB.
static const double TOL_RATE = 0.0005, TOL_SNR = 0.005;
static const int MAX_PASSES = 16;
// Amostra para a estimativa inicial: SEG segmentos de SEG_LEN blocos seguidos
static const std::size_t SEG = 12, SEG_LEN = 4;

struct PassResult {
    uint64_t bytes = 0;
    double errEnergy = 0.0;   // soma dos erros quadráticos nos coeficientes
    double sigEnergy = 0.0;   // soma de x^2 no domínio original
    Stats st;                 // erro real, depois da síntese (só se pedido)
};

// Bloco passado à thread da síntese: coeficientes desquantizados e as frames
// originais que a síntese desse bloco vai completar.
struct SynthItem {
    std::vector<std::vector<double>> Xq;
    std::vector<int32_t> orig;
};

// Uma passagem completa pelo ficheiro com o passo h.step. Se out não for nulo,
// escreve o ficheiro comprimido; se measure, faz a síntese e mede o erro real.
// A síntese corre numa segunda thread, ao mesmo tempo que a análise e a
// codificação dos blocos seguintes.
static PassResult runPass(const std::string& inPath, const Header& h, double round,
                          const std::string* out, bool measure) {
    PassResult r;
    WavReader in(inPath);
    Analyzer an(in, h);
    std::ofstream f;
    if (out) {
        f.open(*out, std::ios::binary);
        if (!f) throw std::runtime_error("Erro ao criar " + *out);
    }

    std::vector<uint8_t> buf;
    writeHeader(buf, h);
    RangeEncoder rc(buf);
    CoefModel model(h.N, h.channels);
    const double inv = 1.0 / h.step, step = h.step;
    std::vector<std::vector<float>> coef(h.channels, std::vector<float>(h.N));
    std::vector<int32_t> q(h.N);
    const std::size_t half = static_cast<std::size_t>(h.N) * h.channels;

    // thread da síntese: MDCT inversa, L/R e comparação com o original
    const SynthItem modelo{std::vector<std::vector<double>>(h.channels, std::vector<double>(h.N)),
                           std::vector<int32_t>(half)};
    BlockPipe<SynthItem> pipe(4, modelo);
    Stats st;
    std::unique_ptr<Consumer<SynthItem>> synthThread;
    if (measure)
        synthThread = std::make_unique<Consumer<SynthItem>>(pipe, [&] {
            Synth synth(h);
            for (std::size_t b = 0; SynthItem* it = pipe.front(); ++b) {
                const int64_t t0 = static_cast<int64_t>(b) * h.N - h.N;   // 1.ª frame completa
                synth.push(it->Xq, [&](std::size_t t, const int32_t* fr) {
                    const int32_t* o = &it->orig[static_cast<std::size_t>(static_cast<int64_t>(t) - t0) * h.channels];
                    for (int c = 0; c < h.channels; ++c) st.add(o[c], fr[c]);
                });
                pipe.pop();
            }
        });

    // Os bytes já emitidos pelo codificador aritmético são definitivos: vão para
    // o ficheiro (ou só são contados) assim que o buffer cresce.
    auto drain = [&](std::size_t limiar) {
        if (buf.size() < limiar) return;
        if (out) f.write(reinterpret_cast<const char*>(buf.data()), static_cast<long>(buf.size()));
        r.bytes += buf.size();
        buf.clear();
    };

    while (an.next(coef)) {
        SynthItem* it = measure ? &pipe.slot() : nullptr;
        for (int c = 0; c < h.channels; ++c) {
            for (int k = 0; k < h.N; ++k) {
                q[k] = quantize(coef[c][k], inv, round);
                const double e = coef[c][k] - q[k] * step;
                r.errEnergy += e * e;
                if (it) it->Xq[c][k] = q[k] * step;
            }
            model.block(c, q.data(), [&](int ctx, int32_t v) { encodeCoef(rc, model, ctx, v); });
        }
        if (it) {
            // frames [(b-1)N, bN): a primeira metade da janela atual da análise
            std::copy(an.firstHalf(), an.firstHalf() + half, it->orig.begin());
            pipe.push();
        }
        drain(64 * 1024);
    }
    rc.finish();
    drain(0);
    r.sigEnergy = an.signalEnergy();
    if (synthThread) {
        synthThread->join();
        r.st = st;
    }
    if (out) {
        f.flush();
        if (!f) throw std::runtime_error("Erro ao escrever " + *out);
    }
    return r;
}

// Coeficientes de uma amostra de blocos (SEG segmentos de SEG_LEN blocos seguidos,
// espalhados pelo ficheiro), para estimar Δ sem guardar o ficheiro todo.
struct Sample {
    std::vector<std::vector<float>> coef;   // [canal][bloco da amostra * N + k]
    std::size_t blocks = 0;                 // n.º de blocos guardados
    double sigEnergy = 0.0;                 // energia do ficheiro todo
};

static Sample takeSample(const std::string& inPath, const Header& h) {
    Sample s;
    const std::size_t B = h.blocks();
    std::vector<char> keep(B, 0);
    if (B <= SEG * SEG_LEN) {
        std::fill(keep.begin(), keep.end(), 1);
    } else {
        for (std::size_t i = 0; i < SEG; ++i) {
            const std::size_t ini = i * (B - SEG_LEN) / (SEG - 1);
            for (std::size_t j = 0; j < SEG_LEN; ++j) keep[ini + j] = 1;
        }
    }
    for (char k : keep) s.blocks += static_cast<std::size_t>(k);
    s.coef.assign(h.channels, std::vector<float>(s.blocks * h.N));

    WavReader in(inPath);
    Analyzer an(in, h);
    std::vector<std::vector<float>> coef(h.channels, std::vector<float>(h.N));
    std::size_t j = 0;
    for (std::size_t b = 0; an.next(coef); ++b)
        if (keep[b]) {
            for (int c = 0; c < h.channels; ++c)
                std::copy(coef[c].begin(), coef[c].end(), s.coef[c].begin() + static_cast<long>(j * h.N));
            ++j;
        }
    s.sigEnergy = an.signalEnergy();
    return s;
}

// Codifica só a amostra (em memória) e devolve os bytes e o erro.
static void encodeSample(const Sample& s, const Header& h, double round, double& bytes,
                         double& err, double& energy) {
    std::vector<uint8_t> out;
    RangeEncoder rc(out);
    CoefModel model(h.N, h.channels);
    std::vector<int32_t> q(h.N);
    const double inv = 1.0 / h.step, step = h.step;
    err = energy = 0.0;
    for (std::size_t b = 0; b < s.blocks; ++b)
        for (int c = 0; c < h.channels; ++c) {
            const float* X = &s.coef[c][b * h.N];
            for (int k = 0; k < h.N; ++k) {
                q[k] = quantize(X[k], inv, round);
                const double e = X[k] - q[k] * step;
                err += e * e;
                energy += static_cast<double>(X[k]) * X[k];
            }
            model.block(c, q.data(), [&](int ctx, int32_t v) { encodeCoef(rc, model, ctx, v); });
        }
    rc.finish();
    bytes = static_cast<double>(out.size());
}

static double snrDb(double sig, double err) {
    return 10.0 * std::log10(sig / std::max(err, 1e-300));
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
        Header h;
        {
            const WavReader info(pos[0]);
            h.sampleRate = static_cast<uint32_t>(info.sampleRate);
            h.channels = info.channels;
            h.bitsPerSample = info.bitsPerSample;
            h.format = static_cast<uint32_t>(info.format);
            h.frames = info.frames;
        }
        h.N = N;
        h.midSide = allowMS && h.channels == 2;

        const double seconds = std::max(static_cast<double>(h.frames) / h.sampleRate, 1e-9);
        auto rateOf = [&](double bytes) { return bytes * 8.0 / seconds / 1000.0; };
        const std::string tmp = pos[1] + ".tmp";

        PassResult best;
        int passes = 0;
        if (fixedStep > 0.0) {
            h.step = static_cast<float>(fixedStep);
            best = runPass(pos[0], h, round, &pos[1], true);
            passes = 1;
        } else {
            const bool bySnr = snrTarget > 0.0;
            // g(x) é o que se quer acertar, em função de x = log2(Δ); desce com x.
            //   débito: g = log(kbps), quer-se g <= alvo e o mais perto possível
            //   SNR:    g = SNR (dB),  quer-se g >= alvo e o mais perto possível
            const double alvo = bySnr ? snrTarget : std::log(kbps);
            const double tol = bySnr ? TOL_SNR : -std::log(1.0 - TOL_RATE);
            const double mira = bySnr ? alvo + tol / 2 : alvo - tol / 2;   // aponta para dentro
            auto ok = [&](double g) { return bySnr ? g >= alvo : g <= alvo; };
            auto close = [&](double g) { return bySnr ? g <= alvo + tol : g >= alvo - tol; };

            // ---- 1. estimativa na amostra (bisseção barata, em memória)
            const Sample s = takeSample(pos[0], h);
            const double scale = static_cast<double>(h.blocks()) / static_cast<double>(s.blocks);
            auto gSample = [&](double x) {
                h.step = static_cast<float>(std::exp2(x));
                double bytes, err, energy;
                encodeSample(s, h, round, bytes, err, energy);
                return bySnr ? snrDb(energy, err) : std::log(rateOf(bytes * scale));
            };
            // Intervalo de procura de x = log2(Δ): de 2^-8 até 2^(bits+8), passo com que
            // todos os coeficientes já ficam a zero (2^24 em 16 bits, 2^32 em 24 bits)
            const double xMin = -8.0, xMax = h.bitsPerSample + 8.0;
            double lo = xMin, hi = xMax;
            for (int it = 0; it < 12; ++it) {
                const double mid = 0.5 * (lo + hi);
                (gSample(mid) > mira ? lo : hi) = mid;
            }
            double x = 0.5 * (lo + hi);
            const double slope = (gSample(x + 0.25) - gSample(x - 0.25)) / 0.5;   // dg/dx

            // ---- 2. passagens completas, pela secante, dentro de um intervalo
            // xOk: o melhor x que cumpre o alvo; xBad: o mais próximo que não cumpre
            double xOk = std::numeric_limits<double>::quiet_NaN(), xBad = xOk;
            double xPrev = xOk, gPrev = 0.0;
            for (passes = 1; passes <= MAX_PASSES; ++passes) {
                h.step = static_cast<float>(std::exp2(x));
                x = std::log2(static_cast<double>(h.step));   // o x que o float representa
                PassResult r = runPass(pos[0], h, round, &tmp, true);
                const double g = bySnr ? snrDb(r.sigEnergy, r.errEnergy) : std::log(rateOf(r.bytes));
                if (ok(g)) {
                    // melhor solução: o ok com x mais pequeno (débito) ou maior (SNR)
                    if (std::isnan(xOk) || (bySnr ? x > xOk : x < xOk)) {
                        xOk = x; best = r;
                        if (std::rename(tmp.c_str(), pos[1].c_str()) != 0)
                            throw std::runtime_error("Erro ao criar " + pos[1]);
                    }
                    if (close(g)) break;
                } else if (std::isnan(xBad) || (bySnr ? x < xBad : x > xBad)) {
                    xBad = x;
                }
                // próximo x: secante com o ponto anterior (ou a inclinação da amostra)
                double sl = slope;
                if (!std::isnan(xPrev) && x != xPrev && g != gPrev) sl = (g - gPrev) / (x - xPrev);
                if (!(sl < 0.0)) sl = slope < 0.0 ? slope : -1.0;
                double xn = x + (mira - g) / sl;
                // tem de ficar estritamente entre o melhor ok e o melhor não-ok
                if (!std::isnan(xOk) && !std::isnan(xBad)) {
                    const double a = std::min(xOk, xBad), b = std::max(xOk, xBad);
                    if (!(xn > a && xn < b)) xn = 0.5 * (a + b);
                    if (b - a < 1e-7) break;   // já não há floats entre os dois
                }
                // se já está no limite do intervalo, o alvo não é atingível (p. ex.
                // silêncio, ou um ficheiro tão curto que só o cabeçalho passa o débito)
                xn = std::clamp(xn, xMin, xMax);
                if (xn == x) break;
                xPrev = x; gPrev = g; x = xn;
            }
            std::remove(tmp.c_str());
            if (passes > MAX_PASSES) passes = MAX_PASSES;
            if (std::isnan(xOk)) {
                // nenhum passo cumpre o alvo: fica o que mais se aproxima, com aviso
                h.step = static_cast<float>(std::exp2(xBad));
                best = runPass(pos[0], h, round, &pos[1], true);
                ++passes;
                std::cerr << "Aviso: o alvo pedido não é atingível com este ficheiro; "
                             "foi usado o passo que mais se aproxima\n";
            } else {
                h.step = static_cast<float>(std::exp2(xOk));
            }
        }
        const double tEnc = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        const double bytes = static_cast<double>(best.bytes);
        std::cout << std::fixed << std::setprecision(2) << pos[0] << " -> " << pos[1] << ": N=" << N
                  << (h.midSide ? " MID/SIDE" : " L/R") << " passo=" << std::setprecision(3)
                  << h.step << std::setprecision(2) << "  " << best.bytes << " bytes  "
                  << rateOf(bytes) << " kbps  taxa " << std::setprecision(1)
                  << static_cast<double>(h.frames) * h.channels * h.bitsPerSample / 8.0 / bytes
                  << ":1  SNR " << std::setprecision(2) << best.st.snr() << " dB  (" << tEnc
                  << " s, " << passes << (passes == 1 ? " passagem" : " passagens") << ")\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
