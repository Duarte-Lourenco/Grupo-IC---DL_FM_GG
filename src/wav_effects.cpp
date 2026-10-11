// wav_effects — efeitos de áudio.
// Uso: wav_effects entrada.wav saida.wav efeito [parâmetros]
//   echo      atraso_ms ganho                 eco simples
//   multiecho atraso_ms ganho                 ecos múltiplos (realimentação, |ganho| < 1)
//   am        freq_hz                         modulação de amplitude
//   vdelay    atraso_max_ms freq_hz [ganho]   atraso variável (ganho 0.7 por omissão)
//
// Equações (x = entrada, y = saída, fs = taxa de amostragem,
//           D = atraso em amostras = atraso_ms * fs / 1000):
//   echo:      y[n] = x[n] + a * x[n - D]
//   multiecho: y[n] = x[n] + a * y[n - D]      -> ecos com ganho a, a^2, a^3, ...
//                                                 em D, 2D, 3D, ... (só se extinguem se |a| < 1)
//   am:        y[n] = x[n] * cos(2*pi*f*n / fs)
//   vdelay:    y[n] = x[n] + a * x[n - D(n)],  D(n) = Dmax/2 * (1 - cos(2*pi*f*n / fs))
//              D(n) varia suavemente entre 0 e Dmax; como não é inteiro, x[n - D(n)]
//              obtém-se por interpolação linear entre as duas amostras vizinhas.
// Amostras antes do início (índice negativo) valem 0. Os canais são independentes.
//
// As contas são feitas em double e o resultado só é arredondado e limitado ao
// intervalo do ficheiro (-32768..32767 em 16 bits) no fim. O n.º de amostras
// saturadas é mostrado, para se poder baixar o ganho se forem muitas.
// A saída tem o formato e a duração do original (a cauda dos ecos depois do fim
// é cortada), por isso pode ser comparada com o wav_cmp.

#include "common/wav_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

using Signal = std::vector<double>;   // amostras intercaladas, como em WavData::samples

constexpr double kDefaultVdelayGain = 0.7;

static void usage() {
    std::cerr << "Uso: wav_effects entrada.wav saida.wav efeito [parâmetros]\n"
                 "  echo      atraso_ms ganho                 eco simples\n"
                 "  multiecho atraso_ms ganho                 ecos múltiplos (|ganho| < 1)\n"
                 "  am        freq_hz                         modulação de amplitude\n"
                 "  vdelay    atraso_max_ms freq_hz [ganho]   atraso variável (ganho 0.7)\n";
}

// Converte um argumento para double; falha se não for um número ("0.5x", "abc", "inf").
static bool parseNumber(const char* s, double& v) {
    char* end = nullptr;
    v = std::strtod(s, &end);
    return end != s && *end == '\0' && std::isfinite(v);
}

// ---------------------------------------------------------------- efeitos

static Signal echo(const WavData& x, std::size_t D, double a) {
    const int C = x.channels;
    const std::size_t N = x.frames();
    Signal y(x.samples.begin(), x.samples.end());              // y[n] = x[n] ...
    for (std::size_t n = D; n < N; ++n)
        for (int c = 0; c < C; ++c)
            y[n * C + c] += a * x.at(n - D, c);                // ... + a * x[n - D]
    return y;
}

// Igual ao echo, mas soma a SAÍDA atrasada (realimentação): cada eco gera outro.
// Usa-se o y ainda não arredondado, para o erro de arredondamento não se acumular.
static Signal multiEcho(const WavData& x, std::size_t D, double a) {
    const int C = x.channels;
    const std::size_t N = x.frames();
    Signal y(x.samples.begin(), x.samples.end());
    for (std::size_t n = D; n < N; ++n)
        for (int c = 0; c < C; ++c)
            y[n * C + c] += a * y[(n - D) * C + c];
    return y;
}

static Signal amplitudeModulation(const WavData& x, double f) {
    const int C = x.channels;
    const std::size_t N = x.frames();
    const double w = 2.0 * std::numbers::pi * f / x.sampleRate;
    Signal y(x.samples.size());
    for (std::size_t n = 0; n < N; ++n) {
        const double g = std::cos(w * static_cast<double>(n));
        for (int c = 0; c < C; ++c) y[n * C + c] = g * x.at(n, c);
    }
    return y;
}

static Signal variableDelay(const WavData& x, double maxD, double f, double a) {
    const int C = x.channels;
    const std::size_t N = x.frames();
    const double w = 2.0 * std::numbers::pi * f / x.sampleRate;
    Signal y(x.samples.begin(), x.samples.end());
    for (std::size_t n = 0; n < N; ++n) {
        const double t = static_cast<double>(n);
        const double pos = t - maxD / 2.0 * (1.0 - std::cos(w * t));   // n - D(n)
        if (pos < 0.0) continue;                                     // antes do início: 0
        // pos <= n, logo i <= N - 1; se i == N - 1 então frac == 0 e j não conta.
        const auto i = static_cast<std::size_t>(pos);
        const std::size_t j = std::min(i + 1, N - 1);
        const double frac = pos - static_cast<double>(i);
        for (int c = 0; c < C; ++c)
            y[n * C + c] += a * ((1.0 - frac) * x.at(i, c) + frac * x.at(j, c));
    }
    return y;
}

// ---------------------------------------------------------------- validação

static bool fail(const std::string& msg) {
    std::cerr << "Erro: " << msg << "\n";
    return false;
}

// Atraso em ms -> amostras (pode não ser inteiro).
static double delayInSamples(double ms, int fs) { return ms * fs / 1000.0; }

// Valida os parâmetros, aplica o efeito a x e guarda o resultado em y.
// Escreve um resumo em stdout; em caso de erro escreve a mensagem e devolve false.
static bool applyEffect(const WavData& x, const std::string& effect,
                        const std::vector<double>& p, Signal& y) {
    const double N = static_cast<double>(x.frames());
    const double nyquist = x.sampleRate / 2.0;

    if (effect == "echo" || effect == "multiecho") {
        if (p.size() != 2) return fail(effect + " precisa de: atraso_ms ganho");
        const double D = std::round(delayInSamples(p[0], x.sampleRate));
        if (D < 1.0 || D >= N) return fail("o atraso tem de ser >= 1 amostra e menor que o ficheiro");
        if (effect == "multiecho" && std::abs(p[1]) >= 1.0)
            return fail("multiecho precisa de |ganho| < 1 (senão os ecos nunca se extinguem)");
        const auto Ds = static_cast<std::size_t>(D);
        y = effect == "echo" ? echo(x, Ds, p[1]) : multiEcho(x, Ds, p[1]);
        std::cout << effect << ": D = " << Ds << " amostras (" << p[0] << " ms), ganho = " << p[1] << "\n";
    } else if (effect == "am") {
        if (p.size() != 1) return fail("am precisa de: freq_hz");
        if (p[0] <= 0.0 || p[0] >= nyquist) return fail("freq_hz tem de estar entre 0 e fs/2");
        y = amplitudeModulation(x, p[0]);
        std::cout << "am: f = " << p[0] << " Hz\n";
    } else if (effect == "vdelay") {
        if (p.size() != 2 && p.size() != 3) return fail("vdelay precisa de: atraso_max_ms freq_hz [ganho]");
        const double maxD = delayInSamples(p[0], x.sampleRate);
        const double a = p.size() == 3 ? p[2] : kDefaultVdelayGain;
        if (maxD <= 0.0 || maxD >= N) return fail("o atraso máximo tem de ser > 0 e menor que o ficheiro");
        if (p[1] <= 0.0 || p[1] >= nyquist) return fail("freq_hz tem de estar entre 0 e fs/2");
        y = variableDelay(x, maxD, p[1], a);
        std::cout << "vdelay: D(n) entre 0 e " << maxD << " amostras (" << p[0] << " ms), f = "
                  << p[1] << " Hz, ganho = " << a << "\n";
    } else {
        usage();
        return fail("efeito desconhecido '" + effect + "'");
    }
    return true;
}

// Arredonda y para inteiros e limita-os ao intervalo do ficheiro (2^(bits-1) - 1 .. -2^(bits-1)),
// escrevendo em out.samples. Devolve o n.º de amostras saturadas.
static std::size_t saturate(const Signal& y, WavData& out) {
    const double hi = static_cast<double>((int64_t{1} << (out.bitsPerSample - 1)) - 1);
    const double lo = -hi - 1.0;
    std::size_t clipped = 0;
    for (std::size_t i = 0; i < y.size(); ++i) {
        double v = std::round(y[i]);
        if (v > hi) { v = hi; ++clipped; }
        else if (v < lo) { v = lo; ++clipped; }
        out.samples[i] = static_cast<int32_t>(v);
    }
    return clipped;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        usage();
        return 0;
    }
    if (argc < 4) {
        usage();
        return 1;
    }
    const std::string inPath = argv[1], outPath = argv[2], effect = argv[3];

    std::vector<double> params;
    for (int i = 4; i < argc; ++i) {
        double v = 0.0;
        if (!parseNumber(argv[i], v)) {
            std::cerr << "Erro: parâmetro inválido '" << argv[i] << "'\n";
            return 1;
        }
        params.push_back(v);
    }

    try {
        const WavData in = readWav(inPath);
        Signal y;
        if (!applyEffect(in, effect, params, y)) return 1;

        WavData out = in;               // mesmo formato; as amostras são substituídas
        const std::size_t clipped = saturate(y, out);
        writeWav(outPath, out);

        const double pct = y.empty() ? 0.0 : 100.0 * static_cast<double>(clipped) / static_cast<double>(y.size());

        std::cout << inPath << " -> " << outPath << ": " << out.frames() << " frames, "
                  << clipped << " amostras saturadas (" << std::fixed << std::setprecision(3)
                  << pct << " %)\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
