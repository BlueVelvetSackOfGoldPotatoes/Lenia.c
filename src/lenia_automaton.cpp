#include "lenia_automaton.h"
#include "fftw_runtime.h"
#include <cmath>
#include <random>
#include <iostream>

// Global FFTW runtime (loaded once via dlopen)
static lenia::FFTWRuntime g_fftw;

namespace lenia {

// ============================================================================
// Kernel Core Functions
// ============================================================================

double kernel_core(int type, double r) {
    if (r <= 0.0 || r >= 1.0) return 0.0;
    switch (type) {
        case 0: { // polynomial quad4: (4r(1-r))^4
            double t = 4.0 * r * (1.0 - r);
            return t * t * t * t;
        }
        case 1: { // exponential bump4: exp(4 - 1/(r(1-r)))
            double denom = r * (1.0 - r);
            if (denom < EPSILON) return 0.0;
            return std::exp(4.0 - 1.0 / denom);
        }
        case 2: // step: (r>=1/4) & (r<=3/4)
            return (r >= 0.25 && r <= 0.75) ? 1.0 : 0.0;
        case 3: // staircase (life): step + 0.5 below
            if (r >= 0.25 && r <= 0.75) return 1.0;
            if (r < 0.25) return 0.5;
            return 0.0;
        default:
            return kernel_core(1, r);
    }
}

// ============================================================================
// Growth Functions
// ============================================================================

double growth_func(int type, double n, double m, double s) {
    switch (type) {
        case 0: { // polynomial quad4: max(0, 1-(n-m)²/(9s²))^4 * 2 - 1
            double diff = (n - m);
            double t = 1.0 - (diff * diff) / (9.0 * s * s);
            if (t <= 0.0) return -1.0;
            double t4 = t * t;
            t4 = t4 * t4;
            return t4 * 2.0 - 1.0;
        }
        case 1: { // gaussian: exp(-(n-m)²/(2s²)) * 2 - 1
            double diff = n - m;
            return std::exp(-(diff * diff) / (2.0 * s * s)) * 2.0 - 1.0;
        }
        case 2: // step: |n-m|<=s ? 1 : -1
            return (std::abs(n - m) <= s) ? 1.0 : -1.0;
        default:
            return growth_func(1, n, m, s);
    }
}

// ============================================================================
// Soft-max / Soft-clip
// ============================================================================

double soft_max(double x, double m, double k) {
    return std::log(std::exp(k * x) + std::exp(k * m)) / k;
}

double soft_clip(double x, double lo, double hi, double k) {
    double a = std::exp(k * x);
    double b = std::exp(k * lo);
    double c = std::exp(-k * hi);
    return std::log(1.0 / (a + b) + c) / (-k);
}

// ============================================================================
// FFT
// ============================================================================

void fft1d_inplace(std::complex<double>* data, int n, bool inverse) {
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    // Cooley-Tukey butterflies
    for (int len = 2; len <= n; len <<= 1) {
        double angle = (inverse ? 1.0 : -1.0) * 2.0 * PI / len;
        std::complex<double> wn(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j) {
                auto u = data[i + j];
                auto v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wn;
            }
        }
    }
    if (inverse) {
        for (int i = 0; i < n; ++i) data[i] /= static_cast<double>(n);
    }
}

FFTND::FFTND(const std::vector<int>& shape) : shape_(shape) {
    auto is_pow2 = [](int n) { return n > 0 && (n & (n - 1)) == 0; };
    total_ = 1;
    for (int s : shape_) {
        if (!is_pow2(s)) throw std::runtime_error("FFTND requires power-of-2 dimensions");
        total_ *= s;
    }
    if (g_fftw.available()) {
        static bool printed = false;
        if (!printed) { std::cerr << "[FFT: using FFTW3 via dlopen]" << std::endl; printed = true; }
    }
}

void FFTND::fft_along_axis(ComplexVec& data, int axis, bool inverse) {
    int ndim = static_cast<int>(shape_.size());
    int axis_len = shape_[axis];

    // Compute stride for this axis
    int stride = 1;
    for (int d = axis + 1; d < ndim; ++d) stride *= shape_[d];

    // Number of 1D transforms needed
    int outer = total_ / (axis_len * stride);

    ComplexVec buf(axis_len);
    for (int o = 0; o < outer; ++o) {
        for (int s = 0; s < stride; ++s) {
            int base = o * axis_len * stride + s;
            for (int i = 0; i < axis_len; ++i)
                buf[i] = data[base + i * stride];
            fft1d_inplace(buf.data(), axis_len, inverse);
            for (int i = 0; i < axis_len; ++i)
                data[base + i * stride] = buf[i];
        }
    }
}

void FFTND::forward(const std::vector<double>& real_in, ComplexVec& complex_out) {
    complex_out.resize(total_);
    for (int i = 0; i < total_; ++i)
        complex_out[i] = std::complex<double>(real_in[i], 0.0);

    // Use FFTW for 2D if available
    if (ndim() == 2 && g_fftw.available()) {
        ComplexVec result;
        g_fftw.forward_2d(shape_[0], shape_[1], complex_out, result);
        complex_out = std::move(result);
        return;
    }

    // Fallback: 1D FFT along each axis
    for (int axis = 0; axis < ndim(); ++axis)
        fft_along_axis(complex_out, axis, false);
}

void FFTND::inverse(const ComplexVec& complex_in, std::vector<double>& real_out) {
    // Use FFTW for 2D if available
    if (ndim() == 2 && g_fftw.available()) {
        ComplexVec result;
        g_fftw.inverse_2d(shape_[0], shape_[1], complex_in, result);
        real_out.resize(total_);
        for (int i = 0; i < total_; ++i) real_out[i] = result[i].real();
        return;
    }

    ComplexVec temp(complex_in);
    for (int axis = 0; axis < ndim(); ++axis)
        fft_along_axis(temp, axis, true);

    real_out.resize(total_);
    for (int i = 0; i < total_; ++i) real_out[i] = temp[i].real();
}

void fftshift_nd(std::vector<double>& arr, const std::vector<int>& shape) {
    int total = 1;
    for (int s : shape) total *= s;
    std::vector<double> temp(total);
    int ndim = static_cast<int>(shape.size());

    for (int flat = 0; flat < total; ++flat) {
        // Decompose flat index into n-D coords
        int remainder = flat;
        int dst = 0;
        int stride = 1;
        for (int d = ndim - 1; d >= 0; --d) {
            int coord = remainder % shape[d];
            remainder /= shape[d];
            int shifted = (coord + shape[d] / 2) % shape[d];
            dst += shifted * stride;
            stride *= shape[d];
        }
        temp[dst] = arr[flat];
    }
    arr = std::move(temp);
}

void fftshift2d(std::vector<double>& arr, int rows, int cols) {
    fftshift_nd(arr, {rows, cols});
}

// ============================================================================
// Automaton
// ============================================================================

Automaton::Automaton(Board& world) : world_(world) {
    int n = world_.cells.size();
    fft_ = std::make_unique<FFTND>(world_.cells.shape());
    kernel_ = NDArray(world_.cells.shape(), 0.0);
    kernel_fft_.resize(n);
    world_fft_.resize(n);
    potential_fft_.resize(n);
    potential_.resize(n, 0.0);
    field_.resize(n, 0.0);
    change_.resize(n, 0.0);

    // Build coordinate grids: X[dim] = (coord - mid) for each dimension
    int ndim = world_.cells.ndim();
    X_.resize(ndim);
    D_.resize(n, 0.0);
    std::vector<int> mid(ndim);
    for (int d = 0; d < ndim; ++d) mid[d] = world_.cells.shape(d) / 2;

    for (int d = 0; d < ndim; ++d) X_[d].resize(n);

    // Fill coordinate grids
    for (int flat = 0; flat < n; ++flat) {
        int remainder = flat;
        for (int d = ndim - 1; d >= 0; --d) {
            int coord = remainder % world_.cells.shape(d);
            remainder /= world_.cells.shape(d);
            X_[d][flat] = static_cast<double>(coord - mid[d]);
        }
    }

    // Build polar coordinate indices (for 2D only)
    if (ndim == 2) {
        int rows = world_.cells.shape(0), cols = world_.cells.shape(1);
        int mid_r = rows / 2, mid_c = cols / 2;
        int sizer = std::min(mid_r, mid_c);
        int sizeth = cols;
        TH_.resize((2 * sizer - 1) * sizeth);
        R_grid_.resize((2 * sizer - 1) * sizeth);
        polar_X_.resize((2 * sizer - 1) * sizeth);
        polar_Y_.resize((2 * sizer - 1) * sizeth);
        for (int ri = 0; ri < 2 * sizer - 1; ++ri) {
            double r_val = sizer - 1 - ri;
            for (int ti = 0; ti < sizeth; ++ti) {
                double th = PI * 0.5 + (2.0 * PI * ti) / sizeth;
                int idx = ri * sizeth + ti;
                TH_[idx] = th;
                R_grid_[idx] = r_val;
                polar_X_[idx] = static_cast<int>(r_val * std::cos(th) + mid_c);
                polar_Y_[idx] = static_cast<int>(r_val * std::sin(th) + mid_r);
            }
        }
    }

    calc_kernel();
}

double Automaton::kernel_shell(double r) const {
    if (r >= 1.0) return 0.0;
    const auto& b = world_.params.b;
    int B = static_cast<int>(b.size());
    double Br = B * r;
    int ring_idx = std::min(static_cast<int>(std::floor(Br)), B - 1);
    double ring_weight = b[ring_idx];
    double core_r = std::fmod(Br, 1.0);
    if (core_r < 0) core_r += 1.0;
    core_r = std::min(core_r, 1.0);
    int kn = world_.params.kn - 1;  // 0-indexed
    return (r < 1.0) ? kernel_core(kn, core_r) * ring_weight : 0.0;
}

void Automaton::calc_kernel() {
    int n = world_.cells.size();
    int R = world_.params.R;
    int ndim = world_.cells.ndim();

    // D = sqrt(sum((X[i]/R)^2)) for each cell
    for (int i = 0; i < n; ++i) {
        double sum_sq = 0.0;
        for (int d = 0; d < ndim; ++d) {
            double xr = X_[d][i] / R;
            sum_sq += xr * xr;
        }
        D_[i] = std::sqrt(sum_sq);
    }

    kernel_sum_ = 0.0;
    for (int i = 0; i < n; ++i) {
        double val = kernel_shell(D_[i]);
        kernel_.data()[i] = val;
        kernel_sum_ += val;
    }

    if (kernel_sum_ > EPSILON) {
        for (int i = 0; i < n; ++i) kernel_.data()[i] /= kernel_sum_;
    }

    fft_->forward(kernel_.data(), kernel_fft_);
}

void Automaton::calc_once(bool is_update) {
    int n = world_.cells.size();
    double dt = 1.0 / world_.params.T;
    int gn = world_.params.gn - 1;  // 0-indexed
    double m = world_.params.m;
    double s = world_.params.s;

    // FFT of world
    fft_->forward(world_.cells.data(), world_fft_);

    // Convolution in frequency domain
    for (int i = 0; i < n; ++i) {
        potential_fft_[i] = kernel_fft_[i] * world_fft_[i];
    }

    // IFFT → potential
    fft_->inverse(potential_fft_, potential_);

    // FFTshift the potential (matching Python: self.fftshift(np.real(self.ifftn(...))))
    fftshift_nd(potential_, world_.cells.shape());

    // Apply growth function
    for (int i = 0; i < n; ++i) {
        field_[i] = growth_func(gn, potential_[i], m, s);
    }

    // Multi-step Adams-Bashforth (if enabled)
    std::vector<double>* D_ptr = &field_;
    std::vector<double> D_multistep;
    if (is_multi_step && has_field_old_) {
        D_multistep.resize(n);
        for (int i = 0; i < n; ++i) {
            D_multistep[i] = 0.5 * (3.0 * field_[i] - field_old_[i]);
        }
        field_old_ = field_;
        D_ptr = &D_multistep;
    } else if (is_multi_step) {
        field_old_ = field_;
        has_field_old_ = true;
    }

    // Update: A_new = A + dt * D + dt * (vx * dA/dx + vy * dA/dy)
    auto& cells = world_.cells.data();
    std::vector<double> A_new(n);

    bool has_convection = (std::abs(convection_vx) > 1e-6 || std::abs(convection_vy) > 1e-6)
                          && world_.cells.ndim() == 2;

    if (has_convection) {
        int rows = world_.cells.shape(0);
        int cols = world_.cells.shape(1);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int i = r * cols + c;
                // Central difference gradients with wrapping
                int rp = ((r + 1) % rows) * cols + c;
                int rm = ((r - 1 + rows) % rows) * cols + c;
                int cp = r * cols + ((c + 1) % cols);
                int cm = r * cols + ((c - 1 + cols) % cols);
                double dAdx = (cells[cp] - cells[cm]) * 0.5;
                double dAdy = (cells[rp] - cells[rm]) * 0.5;
                double convection = convection_vx * dAdx + convection_vy * dAdy;
                A_new[i] = cells[i] + dt * (*D_ptr)[i] - dt * convection;
            }
        }
    } else {
        for (int i = 0; i < n; ++i) {
            A_new[i] = cells[i] + dt * (*D_ptr)[i];
        }
    }

    // Add noise (if enabled)
    if (add_noise > 0.0) {
        std::mt19937 rng(gen_);
        std::uniform_real_distribution<double> dist(-0.5, 0.5);
        for (int i = 0; i < n; ++i) {
            double rand_factor = dist(rng) * (add_noise / 10.0) + 1.0;
            A_new[i] *= rand_factor;
        }
    }

    // Clip or soft-clip
    if (is_soft_clip) {
        for (int i = 0; i < n; ++i) {
            A_new[i] = lenia::soft_clip(A_new[i], 0.0, 1.0, 1.0 / dt);
        }
    } else {
        for (int i = 0; i < n; ++i) {
            A_new[i] = std::clamp(A_new[i], 0.0, 1.0);
        }
    }

    // Quantization (if param_P > 0)
    if (world_.param_P > 0) {
        double P = static_cast<double>(world_.param_P);
        for (int i = 0; i < n; ++i) {
            A_new[i] = std::round(A_new[i] * P) / P;
        }
    }

    // Compute change rate
    for (int i = 0; i < n; ++i) {
        change_[i] = (A_new[i] - cells[i]) / dt;
    }

    // Apply update
    if (is_update) {
        if (mask_rate > 0.0) {
            std::mt19937 rng(gen_ + 12345);
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            for (int i = 0; i < n; ++i) {
                if (dist(rng) > (mask_rate / 10.0)) {
                    cells[i] = A_new[i];
                }
            }
        } else {
            cells = A_new;
        }
        gen_++;
        // Round time to avoid floating point drift
        time_ = std::round((time_ + dt) * 1e10) / 1e10;
    }
}

void Automaton::reset() {
    gen_ = 0;
    time_ = 0.0;
    has_field_old_ = false;
}

} // namespace lenia
