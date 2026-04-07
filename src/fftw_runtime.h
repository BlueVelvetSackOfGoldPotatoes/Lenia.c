#pragma once
/// Runtime FFTW3 loader — dlopen libfftw3.so at runtime, no headers needed.
#include <complex>
#include <vector>
#include <dlfcn.h>
#include <cstddef>

namespace lenia {

class FFTWRuntime {
public:
    using fftw_complex = double[2];  // re, im
    using fftw_plan = void*;

    // Function pointer types matching FFTW3 API
    using plan_dft_2d_fn = fftw_plan(*)(int, int, fftw_complex*, fftw_complex*, int, unsigned);
    using execute_fn = void(*)(fftw_plan);
    using destroy_plan_fn = void(*)(fftw_plan);

    static constexpr int FFTW_FORWARD = -1;
    static constexpr int FFTW_BACKWARD = 1;
    static constexpr unsigned FFTW_ESTIMATE = (1U << 6);

    FFTWRuntime() {
        handle_ = dlopen("libfftw3.so.3", RTLD_LAZY);
        if (!handle_) handle_ = dlopen("libfftw3.so", RTLD_LAZY);
        if (handle_) {
            plan_dft_2d_ = reinterpret_cast<plan_dft_2d_fn>(dlsym(handle_, "fftw_plan_dft_2d"));
            execute_ = reinterpret_cast<execute_fn>(dlsym(handle_, "fftw_execute"));
            destroy_plan_ = reinterpret_cast<destroy_plan_fn>(dlsym(handle_, "fftw_destroy_plan"));
            available_ = (plan_dft_2d_ && execute_ && destroy_plan_);
        }
    }

    ~FFTWRuntime() {
        if (handle_) dlclose(handle_);
    }

    bool available() const { return available_; }

    void forward_2d(int rows, int cols,
                    const std::vector<std::complex<double>>& in,
                    std::vector<std::complex<double>>& out) {
        out.resize(rows * cols);
        fftw_plan p = plan_dft_2d_(rows, cols,
            reinterpret_cast<fftw_complex*>(const_cast<std::complex<double>*>(in.data())),
            reinterpret_cast<fftw_complex*>(out.data()),
            FFTW_FORWARD, FFTW_ESTIMATE);
        execute_(p);
        destroy_plan_(p);
    }

    void inverse_2d(int rows, int cols,
                    const std::vector<std::complex<double>>& in,
                    std::vector<std::complex<double>>& out) {
        int n = rows * cols;
        out.resize(n);
        fftw_plan p = plan_dft_2d_(rows, cols,
            reinterpret_cast<fftw_complex*>(const_cast<std::complex<double>*>(in.data())),
            reinterpret_cast<fftw_complex*>(out.data()),
            FFTW_BACKWARD, FFTW_ESTIMATE);
        execute_(p);
        destroy_plan_(p);
        // FFTW doesn't normalize; divide by n
        for (int i = 0; i < n; ++i) out[i] /= n;
    }

private:
    void* handle_ = nullptr;
    bool available_ = false;
    plan_dft_2d_fn plan_dft_2d_ = nullptr;
    execute_fn execute_ = nullptr;
    destroy_plan_fn destroy_plan_ = nullptr;
};

} // namespace lenia
