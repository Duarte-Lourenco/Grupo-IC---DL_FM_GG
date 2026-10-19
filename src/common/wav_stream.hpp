// wav_stream.hpp — leitura e escrita de WAV em fluxo, para o codec sem perdas.
//
// Porque existe, em vez de se usar o wav_io.hpp e o wav_raw.hpp:
//
//   * o readWav carrega o ficheiro todo num vetor de int32_t, que é o dobro do
//     tamanho do WAV, e o readEnvelope lê o ficheiro outra vez para encontrar o
//     bloco "data". Juntos, faziam a memória do codec crescer com o tamanho do
//     ficheiro (22 MiB num ficheiro de 3,4 MiB; 171 MiB num de 50 MiB).
//
//   * o codec só precisa de um bloco de amostras de cada vez, mais as três
//     amostras anteriores de que o preditor de ordem 3 depende. Lendo e
//     escrevendo em fluxo, a memória deixa de depender do ficheiro.
//
//   * não usar a libsndfile aqui evita carregar no processo o libFLAC, o
//     libvorbis, o libopus e o libmpg123, que ela arrasta (cerca de 0,9 MiB de
//     memória residente) e que o codec não precisa. Para PCM inteiro num
//     contentor RIFF, o trabalho é percorrer blocos e converter bytes.
//
// O WavReader foi validado contra a libsndfile: nos sete ficheiros de teste e
// em ficheiros construídos de propósito com 8, 16, 24 e 32 bits, mono e
// estéreo, o ficheiro comprimido sai byte a byte igual com os dois leitores.
//
// Os restantes programas (wav_hist, wav_cmp, wav_quant, wav_effects) continuam
// a usar o wav_io.hpp, onde ter o ficheiro todo em memória não incomoda.
#pragma once

#include "common/wav_raw.hpp"   // WavEnvelope

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Valores de formato da libsndfile, reproduzidos para o cabeçalho do ficheiro
// comprimido ficar igual ao que a versão com a biblioteca escrevia.
inline int sfFormatFor(int bitsPerSample) {
    switch (bitsPerSample) {
        case 8:  return 0x010000 | 0x0005;   // SF_FORMAT_WAV | SF_FORMAT_PCM_U8
        case 16: return 0x010000 | 0x0002;   // SF_FORMAT_WAV | SF_FORMAT_PCM_16
        case 24: return 0x010000 | 0x0003;
        case 32: return 0x010000 | 0x0004;
        default: return 0;
    }
}

// Igual ao readEnvelope do wav_raw.hpp, mas salta o bloco "data" com seekg em
// vez de o ler. O resultado é idêntico, byte a byte.
inline WavEnvelope readEnvelopeStream(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Erro ao abrir " + path);
    f.seekg(0, std::ios::end);
    const auto fileSize = static_cast<uint64_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    if (fileSize < 44) throw std::runtime_error(path + ": não é um ficheiro RIFF/WAV");

    auto readInto = [&](std::vector<uint8_t>& dst, uint64_t n) {
        const std::size_t old = dst.size();
        dst.resize(old + static_cast<std::size_t>(n));
        f.read(reinterpret_cast<char*>(dst.data() + old), static_cast<long>(n));
        if (!f) throw std::runtime_error(path + ": ficheiro truncado");
    };
    auto u32 = [](const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    };

    WavEnvelope e;
    readInto(e.prefix, 12);
    if (e.prefix[0] != 'R' || e.prefix[1] != 'I' || e.prefix[2] != 'F' || e.prefix[3] != 'F')
        throw std::runtime_error(path + ": não é um ficheiro RIFF/WAV");

    uint64_t p = 12;
    while (p + 8 <= fileSize) {
        uint8_t ch[8];
        f.read(reinterpret_cast<char*>(ch), 8);
        if (!f) break;
        const uint32_t sz = u32(ch + 4);
        e.prefix.insert(e.prefix.end(), ch, ch + 8);
        if (std::memcmp(ch, "data", 4) == 0) {
            const uint64_t ini = p + 8;
            const uint64_t fim = std::min(ini + sz, fileSize);
            e.dataBytes = fim - ini;
            f.seekg(static_cast<long>(fim), std::ios::beg);   // salta as amostras
            readInto(e.suffix, fileSize - fim);
            return e;
        }
        const uint64_t salto = static_cast<uint64_t>(sz) + (sz & 1);
        readInto(e.prefix, std::min(salto, fileSize - (p + 8)));
        p += 8 + salto;
    }
    throw std::runtime_error(path + ": bloco 'data' não encontrado");
}

// Leitor de WAV em fluxo. Devolve as amostras com o valor real do ficheiro
// (igual ao que o readWav do wav_io.hpp dá depois da correção de escala).
class WavReader {
public:
    explicit WavReader(const std::string& path) : f_(path, std::ios::binary) {
        if (!f_) throw std::runtime_error("Erro ao abrir " + path);
        f_.seekg(0, std::ios::end);
        fileSize_ = static_cast<uint64_t>(f_.tellg());
        f_.seekg(0, std::ios::beg);

        uint8_t h[12];
        f_.read(reinterpret_cast<char*>(h), 12);
        if (!f_ || std::memcmp(h, "RIFF", 4) != 0 || std::memcmp(h + 8, "WAVE", 4) != 0)
            throw std::runtime_error(path + ": não é um ficheiro RIFF/WAV");

        uint64_t p = 12;
        bool temFmt = false;
        while (p + 8 <= fileSize_) {
            uint8_t ch[8];
            f_.seekg(static_cast<long>(p), std::ios::beg);
            f_.read(reinterpret_cast<char*>(ch), 8);
            if (!f_) break;
            const uint32_t sz = u32(ch + 4);
            if (std::memcmp(ch, "fmt ", 4) == 0 && sz >= 16) {
                uint8_t fmt[16];
                f_.read(reinterpret_cast<char*>(fmt), 16);
                const auto audioFmt = static_cast<uint16_t>(fmt[0] | (fmt[1] << 8));
                // 1 = PCM, 0xFFFE = WAVE_FORMAT_EXTENSIBLE (PCM com mais campos)
                if (audioFmt != 1 && audioFmt != 0xFFFE)
                    throw std::runtime_error(path + ": só PCM inteiro é suportado");
                channels = static_cast<int>(fmt[2] | (fmt[3] << 8));
                sampleRate = static_cast<int>(u32(fmt + 4));
                bitsPerSample = static_cast<int>(fmt[14] | (fmt[15] << 8));
                temFmt = true;
            } else if (std::memcmp(ch, "data", 4) == 0) {
                if (!temFmt) throw std::runtime_error(path + ": bloco 'data' antes do 'fmt '");
                dataIni_ = p + 8;
                dataBytes_ = std::min<uint64_t>(sz, fileSize_ - dataIni_);
                break;
            }
            p += 8 + sz + (sz & 1);
        }
        if (dataIni_ == 0) throw std::runtime_error(path + ": bloco 'data' não encontrado");
        if (channels < 1 || channels > 2)
            throw std::runtime_error(path + ": só mono ou estéreo é suportado");
        if (bitsPerSample % 8 != 0 || bitsPerSample < 8 || bitsPerSample > 32)
            throw std::runtime_error(path + ": bits por amostra não suportados");

        bytes_ = bitsPerSample / 8;
        format = sfFormatFor(bitsPerSample);
        const auto frameBytes = static_cast<uint64_t>(bytes_) * static_cast<uint64_t>(channels);
        frames = frameBytes ? dataBytes_ / frameBytes : 0;
        f_.seekg(static_cast<long>(dataIni_), std::ios::beg);
    }

    // Lê n frames para dst (intercaladas) e devolve quantas leu.
    std::size_t readFrames(int32_t* dst, std::size_t n) {
        const std::size_t total = n * static_cast<std::size_t>(channels);
        raw_.resize(total * static_cast<std::size_t>(bytes_));
        f_.read(reinterpret_cast<char*>(raw_.data()), static_cast<long>(raw_.size()));
        const auto lidos = static_cast<std::size_t>(f_.gcount());
        const std::size_t amostras = lidos / static_cast<std::size_t>(bytes_);
        for (std::size_t i = 0; i < amostras; ++i) {
            const uint8_t* q = raw_.data() + i * static_cast<std::size_t>(bytes_);
            switch (bytes_) {
                case 1:   // o WAV de 8 bits é sem sinal
                    dst[i] = static_cast<int32_t>(q[0]) - 128;
                    break;
                case 2:
                    dst[i] = static_cast<int16_t>(q[0] | (q[1] << 8));
                    break;
                case 3: {   // 24 bits: extensão de sinal a partir do bit 23
                    int32_t t = q[0] | (q[1] << 8) | (q[2] << 16);
                    if (t & 0x800000) t |= static_cast<int32_t>(0xFF000000u);
                    dst[i] = t;
                    break;
                }
                default:
                    dst[i] = static_cast<int32_t>(static_cast<uint32_t>(q[0]) |
                                                  (static_cast<uint32_t>(q[1]) << 8) |
                                                  (static_cast<uint32_t>(q[2]) << 16) |
                                                  (static_cast<uint32_t>(q[3]) << 24));
            }
        }
        return channels ? amostras / static_cast<std::size_t>(channels) : 0;
    }

    int channels = 0, bitsPerSample = 0, sampleRate = 0, format = 0;
    uint64_t frames = 0;

private:
    static uint32_t u32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    std::ifstream f_;
    std::vector<uint8_t> raw_;
    uint64_t fileSize_ = 0, dataIni_ = 0, dataBytes_ = 0;
    int bytes_ = 0;
};

// Escritor em fluxo: prefixo, depois cada bloco de frames à medida que é
// reconstruído, depois o sufixo. Em vez de construir o bloco "data" inteiro em
// bytes, como o writeRawWav faz.
class WavWriterStream {
public:
    WavWriterStream(const std::string& path, const WavEnvelope& env, int bitsPerSample,
                    std::size_t streamBuf = 256 * 1024)
        : sbuf_(streamBuf), bytes_(bitsPerSample / 8), bits_(bitsPerSample) {
        f_.rdbuf()->pubsetbuf(sbuf_.data(), static_cast<long>(sbuf_.size()));
        f_.open(path, std::ios::binary);
        if (!f_) throw std::runtime_error("Erro ao criar " + path);
        f_.write(reinterpret_cast<const char*>(env.prefix.data()),
                 static_cast<long>(env.prefix.size()));
    }

    void writeSamples(const int32_t* s, std::size_t count) {
        buf_.resize(count * static_cast<std::size_t>(bytes_));
        for (std::size_t i = 0; i < count; ++i) {
            const uint32_t v = (bits_ == 8) ? (static_cast<uint32_t>(s[i] + 128) & 0xFF)
                                            :  static_cast<uint32_t>(s[i]);
            uint8_t* q = buf_.data() + i * static_cast<std::size_t>(bytes_);
            for (int k = 0; k < bytes_; ++k) q[k] = static_cast<uint8_t>((v >> (8 * k)) & 0xFF);
        }
        f_.write(reinterpret_cast<const char*>(buf_.data()), static_cast<long>(buf_.size()));
    }

    void finish(const WavEnvelope& env) {
        f_.write(reinterpret_cast<const char*>(env.suffix.data()),
                 static_cast<long>(env.suffix.size()));
        f_.flush();
        if (!f_) throw std::runtime_error("Erro ao escrever o ficheiro WAV");
    }

private:
    std::vector<char> sbuf_;
    std::ofstream f_;
    int bytes_, bits_;
    std::vector<uint8_t> buf_;
};