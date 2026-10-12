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
#pragma once

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

class BitWriter {
public:
    explicit BitWriter(const std::string& path) : out_(path, std::ios::binary) {
        if (!out_) throw std::runtime_error("Erro ao criar " + path);
    }
    ~BitWriter() { flush(); }

    // Não se copia um BitWriter: dois objetos a escrever no mesmo ficheiro
    // duplicariam o flush.
    BitWriter(const BitWriter&) = delete;
    BitWriter& operator=(const BitWriter&) = delete;

    // Acrescenta um bit; grava o byte assim que estiverem 8 acumulados.
    void writeBit(int bit) {
        buffer_ = static_cast<uint8_t>((buffer_ << 1) | (bit & 1));
        ++count_;
        if (count_ == 8) {
            out_.put(static_cast<char>(buffer_));
            buffer_ = 0;
            count_ = 0;
        }
    }

    // Escreve os n bits menos significativos de value, do mais significativo
    // para o menos. É esta função que permite guardar amostras de 10 bits
    // sem desperdiçar espaço.
    void writeBits(uint64_t value, int n) {
        if (n < 1 || n > 64) throw std::runtime_error("BitWriter: n inválido");
        for (int i = n - 1; i >= 0; --i)
            writeBit(static_cast<int>((value >> i) & 1));
    }

    // Fecha o último byte, completando-o com zeros à direita.
    void flush() {
        if (count_ > 0) {
            out_.put(static_cast<char>(buffer_ << (8 - count_)));
            buffer_ = 0;
            count_ = 0;
        }
        out_.flush();
    }

private:
    std::ofstream out_;
    uint8_t buffer_ = 0;   // bits à espera de formar um byte
    int count_ = 0;        // quantos bits já estão em buffer_
};

class BitReader {
public:
    explicit BitReader(const std::string& path) : in_(path, std::ios::binary) {
        if (!in_) throw std::runtime_error("Erro ao abrir " + path);
    }

    BitReader(const BitReader&) = delete;
    BitReader& operator=(const BitReader&) = delete;

    // Devolve o bit seguinte, lendo um byte novo quando o atual se esgota.
    int readBit() {
        if (count_ == 0) {
            const int c = in_.get();
            if (c == std::char_traits<char>::eof())
                throw std::runtime_error("BitReader: fim de ficheiro inesperado");
            buffer_ = static_cast<uint8_t>(c);
            count_ = 8;
        }
        --count_;
        return (buffer_ >> count_) & 1;
    }

    // Lê n bits pela mesma ordem com que foram escritos.
    uint64_t readBits(int n) {
        if (n < 1 || n > 64) throw std::runtime_error("BitReader: n inválido");
        uint64_t r = 0;
        for (int i = 0; i < n; ++i)
            r = (r << 1) | static_cast<uint64_t>(readBit());
        return r;
    }

private:
    std::ifstream in_;
    uint8_t buffer_ = 0;
    int count_ = 0;        // quantos bits ainda faltam consumir de buffer_
};
