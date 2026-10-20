// bitstream.hpp — escrita e leitura de ficheiros bit a bit.
//
// Um ficheiro só guarda bytes (8 bits), mas a compressão precisa de escrever
// valores com um número arbitrário de bits (por exemplo 10). Estas classes
// tratam os dados como uma corrente contínua de bits: as amostras ficam
// coladas umas às outras e podem ficar partidas entre dois bytes.
//
// Convenção: os bits são escritos do mais significativo para o menos.
// O último byte é completado com zeros, por isso quem lê tem de saber
// quantos valores existem (essa informação vai no cabeçalho do ficheiro).
//
// Implementação: os bits passam por um acumulador de 64 bits e os bytes por um
// buffer de 64 KiB, em vez de uma chamada ao iostream por byte. Num ficheiro
// comprimido de 3,5 MiB a versão anterior fazia 3,5 milhões de chamadas a
// put()/get() em cada sentido.
#pragma once

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

class BitWriter {
public:
    explicit BitWriter(const std::string& path) : out_(path, std::ios::binary) {
        if (!out_) throw std::runtime_error("Erro ao criar " + path);
        buf_.resize(TAM);
    }
    ~BitWriter() {
        try { flush(); } catch (...) {}
    }

    BitWriter(const BitWriter&) = delete;
    BitWriter& operator=(const BitWriter&) = delete;

    // Acrescenta um bit; grava o byte assim que estiverem 8 acumulados.
    void writeBit(int bit) {
        acc_ = (acc_ << 1) | static_cast<uint64_t>(bit & 1);
        ++count_;
        if (count_ == 8) emitirByte();
    }

    // Escreve os n bits menos significativos de value, do mais significativo
    // para o menos. É esta função que permite guardar amostras de 10 bits
    // sem desperdiçar espaço.
    void writeBits(uint64_t value, int n) {
        if (n < 1 || n > 64) throw std::runtime_error("BitWriter: n inválido");
        if (n > 32) {                       // parte-se para o acumulador não transbordar
            writeBits((value >> 32) & mascara(n - 32), n - 32);
            writeBits(value & 0xFFFFFFFFull, 32);
            return;
        }
        acc_ = (acc_ << n) | (value & mascara(n));
        count_ += n;
        while (count_ >= 8) emitirByte();
    }

    // Fecha o último byte, completando-o com zeros à direita.
    void flush() {
        while (count_ >= 8) emitirByte();
        if (count_ > 0) {
            guardar(static_cast<uint8_t>((acc_ << (8 - count_)) & 0xFF));
            acc_ = 0;
            count_ = 0;
        }
        despejar();
        out_.flush();
        if (!out_) throw std::runtime_error("BitWriter: erro ao escrever");
    }

private:
    static constexpr std::size_t TAM = 64 * 1024;

    static uint64_t mascara(int n) {
        return n >= 64 ? ~0ull : ((1ull << n) - 1);
    }
    void guardar(uint8_t b) {
        buf_[n_++] = b;
        if (n_ == TAM) despejar();
    }
    void emitirByte() {
        count_ -= 8;
        guardar(static_cast<uint8_t>((acc_ >> count_) & 0xFF));
        acc_ &= mascara(count_);
    }
    void despejar() {
        if (n_) {
            out_.write(reinterpret_cast<const char*>(buf_.data()), static_cast<long>(n_));
            n_ = 0;
        }
    }

    std::ofstream out_;
    std::vector<uint8_t> buf_;
    std::size_t n_ = 0;      // bytes à espera no buffer
    uint64_t acc_ = 0;       // bits à espera de formar bytes
    int count_ = 0;          // quantos bits estão em acc_
};

class BitReader {
public:
    explicit BitReader(const std::string& path) : in_(path, std::ios::binary) {
        if (!in_) throw std::runtime_error("Erro ao abrir " + path);
        buf_.resize(TAM);
    }

    BitReader(const BitReader&) = delete;
    BitReader& operator=(const BitReader&) = delete;

    // Devolve o bit seguinte, lendo um byte novo quando o atual se esgota.
    int readBit() {
        if (count_ == 0) {
            acc_ = proximoByte();
            count_ = 8;
        }
        --count_;
        return static_cast<int>((acc_ >> count_) & 1);
    }

    // Lê n bits pela mesma ordem com que foram escritos.
    uint64_t readBits(int n) {
        if (n < 1 || n > 64) throw std::runtime_error("BitReader: n inválido");
        if (n > 32) {
            const uint64_t alto = readBits(n - 32);
            const uint64_t baixo = readBits(32);
            return (alto << 32) | baixo;
        }
        while (count_ < n) {
            acc_ = (acc_ << 8) | proximoByte();
            count_ += 8;
        }
        count_ -= n;
        const uint64_t r = (acc_ >> count_) & mascara(n);
        acc_ &= mascara(count_);
        return r;
    }

private:
    static constexpr std::size_t TAM = 64 * 1024;

    static uint64_t mascara(int n) {
        return n >= 64 ? ~0ull : ((1ull << n) - 1);
    }
    uint8_t proximoByte() {
        if (pos_ == len_) {
            in_.read(reinterpret_cast<char*>(buf_.data()), static_cast<long>(TAM));
            len_ = static_cast<std::size_t>(in_.gcount());
            pos_ = 0;
            if (len_ == 0) throw std::runtime_error("BitReader: fim de ficheiro inesperado");
        }
        return buf_[pos_++];
    }

    std::ifstream in_;
    std::vector<uint8_t> buf_;
    std::size_t pos_ = 0, len_ = 0;
    uint64_t acc_ = 0;
    int count_ = 0;
};