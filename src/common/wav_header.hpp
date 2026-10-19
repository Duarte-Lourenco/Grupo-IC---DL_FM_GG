// wav_header.hpp — cabeçalho WAV "canónico" para os programas que escrevem em fluxo.
//
// O WavWriterStream (wav_stream.hpp) escreve o prefixo e o sufixo que recebe num
// WavEnvelope. O codec sem perdas passa-lhe o envelope do ficheiro original; os
// programas que criam um WAV novo (wav_quant, wav_lossy_dec) usam este.
//
// É exatamente o cabeçalho que a libsndfile escrevia para WAV PCM inteiro com 1 ou 2
// canais: 44 bytes, só com os blocos "fmt " (16 bytes, formato 1 = PCM) e "data".
// Assim a saída destes programas ficou igual byte a byte à da versão com a
// biblioteca, e o tamanho do bloco "data" escreve-se logo no início, porque o
// n.º de frames já é conhecido (não é preciso voltar atrás no ficheiro).
#pragma once

#include "common/wav_raw.hpp"   // WavEnvelope

#include <cstdint>
#include <vector>

inline WavEnvelope canonicalWavEnvelope(int channels, int sampleRate, int bitsPerSample,
                                        uint64_t frames) {
    const uint32_t blockAlign = static_cast<uint32_t>(channels * (bitsPerSample / 8));
    const uint64_t dataBytes = frames * blockAlign;
    // O RIFF exige blocos de tamanho par: se "data" for ímpar, leva 1 byte de enchimento.
    const uint32_t pad = static_cast<uint32_t>(dataBytes & 1);

    WavEnvelope env;
    std::vector<uint8_t>& p = env.prefix;
    auto u16 = [&](uint32_t v) { p.push_back(v & 0xFF); p.push_back((v >> 8) & 0xFF); };
    auto u32 = [&](uint64_t v) { for (int i = 0; i < 4; ++i) p.push_back((v >> (8 * i)) & 0xFF); };
    auto tag = [&](const char* s) { p.insert(p.end(), s, s + 4); };

    tag("RIFF"); u32(36 + dataBytes + pad); tag("WAVE");
    tag("fmt "); u32(16);
    u16(1);                                                         // PCM
    u16(static_cast<uint32_t>(channels));
    u32(static_cast<uint32_t>(sampleRate));
    u32(static_cast<uint64_t>(sampleRate) * blockAlign);            // bytes por segundo
    u16(blockAlign);
    u16(static_cast<uint32_t>(bitsPerSample));
    tag("data"); u32(dataBytes);
    if (pad) env.suffix.push_back(0);
    env.dataBytes = dataBytes;
    return env;
}
