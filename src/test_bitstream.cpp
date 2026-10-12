// test_bitstream — verifica a escrita e leitura bit a bit.
#include "common/bitstream.hpp"

#include <cstdio>
#include <iostream>
#include <vector>

int main() {
    bool ok = true;

    // 1) Ida e volta com larguras variadas, incluindo valores não alinhados a byte
    {
        const char* f = "/tmp/test_bitstream.bin";
        const std::vector<std::pair<uint64_t, int>> vals = {
            {0, 1}, {1, 1}, {5, 3}, {0b10110, 5}, {1023, 10}, {0, 10},
            {123456, 20}, {0xFFFFFFFFull, 32}, {1ull << 63, 64}};
        {
            BitWriter w(f);
            w.writeBit(1);
            w.writeBit(0);
            for (auto [v, n] : vals) w.writeBits(v, n);
        }   // o destrutor faz flush
        BitReader r(f);
        if (r.readBit() != 1 || r.readBit() != 0) {
            std::cout << "FALHOU: bits soltos\n";
            ok = false;
        }
        for (auto [v, n] : vals) {
            const uint64_t g = r.readBits(n);
            if (g != v) {
                std::cout << "FALHOU: esperado " << v << " em " << n
                          << " bits, veio " << g << "\n";
                ok = false;
                break;
            }
        }
        std::remove(f);
    }

    // 2) Empacotamento: 1000 amostras de 10 bits = 10000 bits = 1250 bytes
    {
        const char* f = "/tmp/test_bitstream_10.bin";
        {
            BitWriter w(f);
            for (int i = 0; i < 1000; ++i) w.writeBits(i % 1024, 10);
        }
        std::ifstream in(f, std::ios::binary | std::ios::ate);
        const auto bytes = static_cast<long long>(in.tellg());
        in.close();
        std::cout << "1000 amostras de 10 bits ocupam " << bytes
                  << " bytes (esperado 1250; em 16 bits seriam 2000)\n";
        if (bytes != 1250) ok = false;

        BitReader r(f);
        for (int i = 0; i < 1000; ++i) {
            if (r.readBits(10) != static_cast<uint64_t>(i % 1024)) {
                std::cout << "FALHOU: releitura das amostras de 10 bits\n";
                ok = false;
                break;
            }
        }
        std::remove(f);
    }

    std::cout << (ok ? "OK: bitstream\n" : "FALHOU: bitstream\n");
    return ok ? 0 : 1;
}
