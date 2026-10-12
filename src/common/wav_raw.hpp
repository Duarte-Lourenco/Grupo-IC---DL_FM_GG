// wav_raw.hpp — preservação byte a byte do ficheiro WAV original.
//
// A libsndfile escreve sempre um cabeçalho mínimo, por isso um WAV reescrito
// perde os blocos extra do original (metadados LIST, id3, enchimento) e deixa
// de ser idêntico byte a byte, ainda que as amostras sejam as mesmas. Para o
// codec sem perdas satisfazer "output.wav igual a input.wav", guardamos no
// ficheiro comprimido tudo o que está antes e depois dos dados, e na
// descodificação escrevemos esses bytes tal e qual.
#pragma once

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

struct WavEnvelope {
    std::vector<uint8_t> prefix;   // bytes antes das amostras (RIFF, fmt, ...)
    std::vector<uint8_t> suffix;   // bytes depois das amostras
    uint64_t dataBytes = 0;        // tamanho do bloco "data"
};

// Lê o ficheiro, encontra o bloco "data" e separa o que está à volta dele.
inline WavEnvelope readEnvelope(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Erro ao abrir " + path);
    const std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
    auto u32 = [&](std::size_t p) {
        return static_cast<uint32_t>(b[p]) | (static_cast<uint32_t>(b[p + 1]) << 8) |
               (static_cast<uint32_t>(b[p + 2]) << 16) | (static_cast<uint32_t>(b[p + 3]) << 24);
    };
    if (b.size() < 44 || b[0] != 'R' || b[1] != 'I' || b[2] != 'F' || b[3] != 'F')
        throw std::runtime_error(path + ": não é um ficheiro RIFF/WAV");

    std::size_t p = 12;
    while (p + 8 <= b.size()) {
        const uint32_t sz = u32(p + 4);
        const bool isData = b[p] == 'd' && b[p + 1] == 'a' && b[p + 2] == 't' && b[p + 3] == 'a';
        if (isData) {
            WavEnvelope e;
            const std::size_t ini = p + 8;
            const std::size_t fim = std::min(ini + sz, b.size());
            e.prefix.assign(b.begin(), b.begin() + static_cast<long>(ini));
            e.suffix.assign(b.begin() + static_cast<long>(fim), b.end());
            e.dataBytes = fim - ini;
            return e;
        }
        p += 8 + sz + (sz & 1);   // os blocos têm tamanho par
    }
    throw std::runtime_error(path + ": bloco 'data' não encontrado");
}

// Escreve prefixo + amostras (PCM little-endian) + sufixo.
// As amostras vêm com o valor real do ficheiro; em 8 bits o WAV é sem sinal.
inline void writeRawWav(const std::string& path, const WavEnvelope& e,
                        const std::vector<int32_t>& samples, int bitsPerSample) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Erro ao criar " + path);
    f.write(reinterpret_cast<const char*>(e.prefix.data()),
            static_cast<long>(e.prefix.size()));

    const int bytes = bitsPerSample / 8;
    std::vector<uint8_t> buf(samples.size() * static_cast<std::size_t>(bytes));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        uint32_t v;
        if (bitsPerSample == 8) v = static_cast<uint32_t>(samples[i] + 128) & 0xFF;
        else v = static_cast<uint32_t>(samples[i]);
        for (int k = 0; k < bytes; ++k)
            buf[i * static_cast<std::size_t>(bytes) + static_cast<std::size_t>(k)] =
                static_cast<uint8_t>((v >> (8 * k)) & 0xFF);
    }
    f.write(reinterpret_cast<const char*>(buf.data()), static_cast<long>(buf.size()));
    f.write(reinterpret_cast<const char*>(e.suffix.data()),
            static_cast<long>(e.suffix.size()));
    if (!f) throw std::runtime_error("Erro ao escrever " + path);
}