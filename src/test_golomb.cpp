// test_golomb — codifica valores, descodifica-os e compara, para vários m.
#include "common/golomb.hpp"

#include <cstdio>
#include <iostream>
#include <vector>

int main() {
    const char* f = "/tmp/test_golomb.bin";
    const std::vector<int64_t> vals = {0, 1, -1, 2, -2, 7, -8, 100, -1000, 32767, -32768};
    for (uint32_t m : {1u, 2u, 4u, 8u, 16u, 64u, 1024u}) {
        { BitWriter w(f); Golomb g(m); for (auto v : vals) g.encode(w, v); }
        BitReader r(f);
        Golomb g(m);
        for (auto v : vals) {
            const int64_t d = g.decode(r);
            if (d != v) {
                std::cout << "FALHOU: m=" << m << " valor=" << v
                          << " descodificado=" << d << "\n";
                std::remove(f);
                return 1;
            }
        }
    }
    std::remove(f);
    std::cout << "OK: golomb\n";
    return 0;
}