#pragma once

#include "lenia_board.h"
#include <complex>
#include <vector>
#include <memory>
#include <functional>

namespace lenia {

static constexpr double PI = 3.14159265358979323846;

using ComplexVec = std::vector<std::complex<double>>;

/// Kernel core functions (matching Python Automaton.kernel_core)
///   0: polynomial quad4
///   1: exponential bump4
///   2: step
///   3: staircase (life)
double kernel_core(int type, double r);

/// Growth functions (matching Python Automaton.growth_func)
///   0: polynomial quad4
///   1: gaussian
///   2: step
double growth_func(int type, double n, double m, double s);

/// Soft-max and soft-clip utilities
double soft_max(double x, double m, double k);
double soft_clip(double x, double lo, double hi, double k);

/// In-place Cooley-Tukey radix-2 FFT on interleaved complex data.
void fft1d_inplace(std::complex<double>* data, int n, bool inverse);

/// N-dimensional FFT/IFFT for arrays stored flat. Each dimension must be power of 2.
/// Uses FFTW via dlopen when available, falls back to Cooley-Tukey.
class FFTND {
public:
    explicit FFTND(const std::vector<int>& shape);
    void forward(const std::vector<double>& real_in, ComplexVec& complex_out);
    void inverse(const ComplexVec& complex_in, std::vector<double>& real_out);
    const std::vector<int>& shape() const { return shape_; }
    int ndim() const { return static_cast<int>(shape_.size()); }
    int total() const { return total_; }
private:
    std::vector<int> shape_;
    int total_;
    void fft_along_axis(ComplexVec& data, int axis, bool inverse);
};

// Backward compat alias
using FFT2D = FFTND;

/// fftshift for an n-D real array (swap quadrants along all axes)
void fftshift_nd(std::vector<double>& arr, const std::vector<int>& shape);
/// 2D convenience wrapper
void fftshift2d(std::vector<double>& arr, int rows, int cols);

/// The Automaton: computes Lenia state transitions via FFT convolution + growth function.
/// Faithful port of Python Automaton class from LeniaND.py.
class Automaton {
public:
    explicit Automaton(Board& world);

    /// Recalculate kernel from current world.params.
    void calc_kernel();

    /// Perform one simulation step. If is_update=true, modifies world.cells.
    void calc_once(bool is_update = true);

    /// Reset generation/time counters.
    void reset();

    /// Accessors
    int gen() const { return gen_; }
    double time() const { return time_; }
    const NDArray& kernel() const { return kernel_; }
    const std::vector<double>& potential() const { return potential_; }
    const std::vector<double>& field() const { return field_; }
    const std::vector<double>& change() const { return change_; }

    /// Simulation flags (matching Python)
    bool is_multi_step = false;
    bool is_soft_clip = false;
    bool is_inverted = false;
    double mask_rate = 0.0;
    double add_noise = 0.0;

    /// Convection velocity: adds v·∇A to the update rule for WASD steering.
    /// The organism is transported while staying alive.
    double convection_vx = 0.0;
    double convection_vy = 0.0;

private:
    Board& world_;
    std::unique_ptr<FFT2D> fft_;

    NDArray kernel_;           // spatial kernel
    ComplexVec kernel_fft_;    // kernel in frequency domain
    double kernel_sum_ = 0.0;

    ComplexVec world_fft_;
    ComplexVec potential_fft_;
    std::vector<double> potential_;
    std::vector<double> field_;
    std::vector<double> field_old_;
    bool has_field_old_ = false;
    std::vector<double> change_;

    int gen_ = 0;
    double time_ = 0.0;

    /// Coordinate grids (for kernel computation and polar analysis)
    std::vector<std::vector<double>> X_;  // per-dimension coordinate grids
    std::vector<double> D_;                // distance matrix

    /// 2D polar coordinates (for analyzer)
    std::vector<double> TH_, R_grid_;
    std::vector<int> polar_X_, polar_Y_;

    double kernel_shell(double r) const;
};

} // namespace lenia
