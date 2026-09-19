// wav_hist — histograma de um canal de um ficheiro WAV.
//
// Uso: wav_hist [-k K] [-o saida.csv] entrada.wav canal
//   canal : 0, 1, ... (índice do canal; 0 = L, 1 = R), "mid" ou "side"
//   -k K  : bins com 2^K valores (por omissão K = 0, um bin por valor)
//   -o    : ficheiro CSV de saída (por omissão, stdout)
//
// Saída CSV:  valor,contagem   (valor = limite inferior do bin)
// O gráfico é feito com scripts/plot_hist.py.

#include "common/channels.hpp"
#include "common/wav_io.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

static void usage() {
    std::cerr << "Uso: wav_hist [-k K] [-o saida.csv] entrada.wav canal\n"
                 "  canal: 0, 1, ... | mid | side\n"
                 "  -k K : bins de 2^K valores (0 <= K <= 16, omissão 0)\n";
}

int main(int argc, char* argv[]) {
    int k = 0;
    std::string outPath, inPath, chanArg;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-k" && i + 1 < argc) k = std::atoi(argv[++i]);
        else if (a == "-o" && i + 1 < argc) outPath = argv[++i];
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (inPath.empty()) inPath = a;
        else if (chanArg.empty()) chanArg = a;
        else { usage(); return 1; }
    }
    if (inPath.empty() || chanArg.empty() || k < 0 || k > 16) { usage(); return 1; }

    WavData w;
    try { w = readWav(inPath); }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }

    enum { CH, MID, SIDE } mode = CH;
    int ch = 0;
    if (chanArg == "mid") mode = MID;
    else if (chanArg == "side") mode = SIDE;
    else {
        ch = std::atoi(chanArg.c_str());
        if (ch < 0 || ch >= w.channels) {
            std::cerr << "Canal inválido: o ficheiro tem " << w.channels << " canal(is)\n";
            return 1;
        }
    }
    if (mode != CH && w.channels != 2) {
        std::cerr << "mid/side só estão definidos para áudio estéreo\n";
        return 1;
    }

    // Histograma. Um std::map mantém os valores ordenados e funciona para
    // qualquer n.º de bits; para 16 bits tem no máximo 65536 entradas.
    std::map<int32_t, uint64_t> hist;
    const std::size_t N = w.frames();
    for (std::size_t n = 0; n < N; ++n) {
        int32_t v;
        switch (mode) {
            case MID:  v = midOf(w.at(n, 0), w.at(n, 1)); break;
            case SIDE: v = halfSideOf(w.at(n, 0), w.at(n, 1)); break;
            default:   v = w.at(n, ch);
        }
        // Bin: floor(v / 2^k) * 2^k  (>> arredonda para -inf, também nos negativos)
        int32_t bin = static_cast<int32_t>(static_cast<uint32_t>(v >> k) << k);
        ++hist[bin];
    }

    std::ofstream file;
    std::ostream* out = &std::cout;
    if (!outPath.empty()) {
        file.open(outPath);
        if (!file) { std::cerr << "Erro ao criar " << outPath << "\n"; return 1; }
        out = &file;
    }
    *out << "valor,contagem\n";
    for (const auto& [v, c] : hist) *out << v << ',' << c << '\n';

    std::cerr << inPath << ": " << w.channels << " canais, " << w.sampleRate << " Hz, "
              << w.bitsPerSample << " bits, " << N << " amostras/canal; "
              << hist.size() << " bins (2^" << k << " valores cada)\n";
    return 0;
}
