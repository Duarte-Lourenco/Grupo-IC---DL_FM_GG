// wav_io.hpp — leitura e escrita de ficheiros WAV (PCM inteiro) com libsndfile.
//
// As amostras são guardadas como inteiros com o valor "real" do ficheiro
// (p. ex. -32768..32767 para 16 bits), intercaladas: L0 R0 L1 R1 ...
#pragma once

#include <sndfile.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

struct WavData {
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;       // 8, 16, 24 ou 32
    int format = 0;              // formato libsndfile original (para reescrever igual)
    std::vector<int32_t> samples;  // intercaladas

    std::size_t frames() const { return channels ? samples.size() / channels : 0; }

    // Amostra n do canal c
    int32_t at(std::size_t n, int c) const { return samples[n * channels + c]; }
};

inline int bitsFromFormat(int format) {
    switch (format & SF_FORMAT_SUBMASK) {
        case SF_FORMAT_PCM_S8:
        case SF_FORMAT_PCM_U8: return 8;
        case SF_FORMAT_PCM_16: return 16;
        case SF_FORMAT_PCM_24: return 24;
        case SF_FORMAT_PCM_32: return 32;
        default: return 0;
    }
}

inline WavData readWav(const std::string& path) {
    SF_INFO info{};
    SNDFILE* f = sf_open(path.c_str(), SFM_READ, &info);
    if (!f) throw std::runtime_error("Erro ao abrir '" + path + "': " + sf_strerror(nullptr));

    WavData w;
    w.channels = info.channels;
    w.sampleRate = info.samplerate;
    w.format = info.format;
    w.bitsPerSample = bitsFromFormat(info.format);
    if (w.bitsPerSample == 0) {
        sf_close(f);
        throw std::runtime_error("Formato não suportado (só PCM inteiro): " + path);
    }

    // libsndfile devolve int alinhado à esquerda em 32 bits; deslocamos para
    // obter o valor original. O >> em inteiros negativos é aritmético (C++20).
    w.samples.resize(static_cast<std::size_t>(info.frames) * info.channels);
    sf_count_t got = sf_readf_int(f, w.samples.data(), info.frames);
    sf_close(f);
    w.samples.resize(static_cast<std::size_t>(got) * info.channels);

    const int shift = 32 - w.bitsPerSample;
    if (shift > 0)
        for (auto& s : w.samples) s >>= shift;
    return w;
}

inline void writeWav(const std::string& path, const WavData& w) {
    SF_INFO info{};
    info.channels = w.channels;
    info.samplerate = w.sampleRate;
    info.format = w.format ? w.format : (SF_FORMAT_WAV | SF_FORMAT_PCM_16);

    SNDFILE* f = sf_open(path.c_str(), SFM_WRITE, &info);
    if (!f) throw std::runtime_error("Erro ao criar '" + path + "': " + sf_strerror(nullptr));

    const int shift = 32 - w.bitsPerSample;
    std::vector<int32_t> buf(w.samples);
    if (shift > 0)
        for (auto& s : buf) s = static_cast<int32_t>(static_cast<uint32_t>(s) << shift);

    sf_writef_int(f, buf.data(), static_cast<sf_count_t>(w.frames()));
    sf_close(f);
}
