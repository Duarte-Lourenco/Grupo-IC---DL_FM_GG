// mdct.hpp — MDCT (DCT modificada) rápida, usada pelo codec com perdas.
//
// Porquê a MDCT e não a DCT em blocos separados: com blocos independentes o
// erro de quantização cria descontinuidades nas fronteiras (estalidos). A MDCT
// usa janelas de 2N amostras sobrepostas em 50%, mas produz só N coeficientes
// por bloco (amostragem crítica: tantos coeficientes como amostras), e o
// "aliasing" de cada bloco é cancelado pelo bloco seguinte (TDAC). É a
// transformada usada no MP3, AAC, Vorbis e Opus.
//
// A MDCT é uma DCT-IV aplicada a uma versão "dobrada" do bloco janelado:
//   bloco janelado z = (a, b, c, d), cada parte com N/2 amostras
//   u = (-c' - d, a - b')                (' = ordem invertida)
//   X = DCT-IV(u)
// e a inversa desdobra: y = (u2, -u2', -u1', -u1) com u = DCT-IV(X).
//
// Com a janela seno, w[n]^2 + w[n+N]^2 = 1, e a escala sqrt(2/N), a
// transformada é ortonormal: o erro (quadrático) introduzido nos coeficientes
// é igual ao erro que aparece no áudio. É isto que permite escolher o passo de
// quantização a pensar diretamente no SNR.
//
// A DCT-IV de N pontos calcula-se com uma FFT complexa de N/2 pontos, por isso
// o custo é O(N log N) por bloco em vez de O(N^2).
#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <vector>

class Mdct {
public:
    // N = n.º de coeficientes por bloco (potência de 2, >= 16).
    explicit Mdct(int N) : N_(N), M_(N / 2) {
        if (N < 16 || (N & (N - 1)) != 0)
            throw std::runtime_error("Mdct: N tem de ser potência de 2 e >= 16");
        const double pi = std::acos(-1.0);

        window_.resize(2 * N_);
        for (int n = 0; n < 2 * N_; ++n) window_[n] = std::sin(pi * (n + 0.5) / (2.0 * N_));

        pre_.resize(M_);
        post_.resize(M_);
        const double scale = std::sqrt(2.0 / N_);
        for (int n = 0; n < M_; ++n) {
            pre_[n] = std::polar(1.0, -pi * n / N_);
            post_[n] = std::polar(scale, -pi * (n + 0.25) / N_);
        }

        // Tabela da FFT: inversão de bits e fatores de rotação
        rev_.resize(M_);
        int lg = 0;
        while ((1 << lg) < M_) ++lg;
        for (int i = 0; i < M_; ++i) {
            int r = 0;
            for (int b = 0; b < lg; ++b)
                if (i & (1 << b)) r |= 1 << (lg - 1 - b);
            rev_[i] = r;
        }
        tw_.resize(M_ / 2 > 0 ? M_ / 2 : 1);
        for (int i = 0; i < M_ / 2; ++i) tw_[i] = std::polar(1.0, -2.0 * pi * i / M_);

        u_.resize(N_);
        buf_.resize(M_);
    }

    int size() const { return N_; }
    const std::vector<double>& window() const { return window_; }

    // x: 2N amostras (sem janela)  ->  X: N coeficientes
    void forward(const double* x, double* X) {
        const int h = N_ / 2;
        // janela + dobragem
        for (int n = 0; n < h; ++n) {
            const double a = x[n] * window_[n];
            const double br = x[N_ - 1 - n] * window_[N_ - 1 - n];
            const double cr = x[N_ + h - 1 - n] * window_[N_ + h - 1 - n];
            const double d = x[N_ + h + n] * window_[N_ + h + n];
            u_[n] = -cr - d;
            u_[h + n] = a - br;
        }
        dct4(u_.data(), X);
    }

    // X: N coeficientes  ->  y: 2N amostras já com janela, para somar
    // (overlap-add) com a segunda metade do bloco anterior.
    void inverse(const double* X, double* y) {
        dct4(X, u_.data());
        const int h = N_ / 2;
        for (int n = 0; n < h; ++n) {
            const double u1 = u_[n], u2 = u_[h + n];
            const double u1r = u_[h - 1 - n], u2r = u_[N_ - 1 - n];
            y[n] = u2;
            y[h + n] = -u2r;
            y[N_ + n] = -u1r;
            y[N_ + h + n] = -u1;
        }
        for (int n = 0; n < 2 * N_; ++n) y[n] *= window_[n];
    }

    // DCT-IV ortonormal (é a sua própria inversa):
    //   X[k] = sqrt(2/N) * soma_n u[n] cos(pi/N (n + 1/2)(k + 1/2))
    // Junta as amostras pares e ímpares (invertidas) num sinal complexo de N/2
    // pontos, roda, faz a FFT e roda outra vez.
    void dct4(const double* in, double* out) {
        for (int n = 0; n < M_; ++n)
            buf_[rev_[n]] = std::complex<double>(in[2 * n], in[N_ - 1 - 2 * n]) * pre_[n];
        fft();
        for (int k = 0; k < M_; ++k) {
            const std::complex<double> z = buf_[k] * post_[k];
            out[2 * k] = z.real();
            out[N_ - 1 - 2 * k] = -z.imag();
        }
    }

private:
    // FFT radix-2 iterativa sobre buf_ (já reordenado por inversão de bits).
    void fft() {
        for (int len = 2; len <= M_; len <<= 1) {
            const int step = M_ / len;
            for (int i = 0; i < M_; i += len)
                for (int j = 0; j < len / 2; ++j) {
                    const std::complex<double> t = buf_[i + j + len / 2] * tw_[j * step];
                    buf_[i + j + len / 2] = buf_[i + j] - t;
                    buf_[i + j] += t;
                }
        }
    }

    int N_, M_;
    std::vector<double> window_;
    std::vector<std::complex<double>> pre_, post_, tw_, buf_;
    std::vector<int> rev_;
    std::vector<double> u_;
};
