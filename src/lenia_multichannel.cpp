#include "lenia_multichannel.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <iostream>

namespace lenia {

// ============================================================================
// MultiBoard
// ============================================================================

MultiBoard::MultiBoard(int num_channels, const std::vector<int>& size)
    : num_channels(num_channels) {
    channels.reserve(num_channels);
    for (int c = 0; c < num_channels; ++c) {
        channels.emplace_back(size, 0.0);
    }
}

void MultiBoard::clear() {
    for (auto& ch : channels) ch.fill(0.0);
}

double MultiBoard::total_mass() const {
    double total = 0;
    for (const auto& ch : channels) total += ch.sum();
    return total;
}

NDArray MultiBoard::composite_view() const {
    if (channels.empty()) return NDArray({1, 1}, 0.0);
    NDArray result = channels[0].copy();
    for (size_t c = 1; c < channels.size(); ++c) {
        for (int i = 0; i < result.size(); ++i) {
            result[i] = std::max(result[i], channels[c][i]);
        }
    }
    return result;
}

// ============================================================================
// MultiAutomaton
// ============================================================================

MultiAutomaton::MultiAutomaton(MultiBoard& world) : world_(world) {
    num_kernels_ = static_cast<int>(world_.kernels.size());
    if (num_kernels_ == 0) {
        throw std::runtime_error("MultiAutomaton requires at least one kernel");
    }
    if (world_.channels.empty()) {
        throw std::runtime_error("MultiAutomaton requires at least one channel");
    }

    const auto& shape = world_.channels[0].shape();
    fft_ = std::make_unique<FFTND>(shape);
    int n = fft_->total();

    // Per-kernel arrays
    kernels_.resize(num_kernels_);
    kernel_ffts_.resize(num_kernels_);
    kernel_sums_.resize(num_kernels_, 0.0);
    potentials_.resize(num_kernels_);
    potential_ffts_.resize(num_kernels_);
    fields_.resize(num_kernels_);
    for (int k = 0; k < num_kernels_; ++k) {
        kernels_[k] = NDArray(shape, 0.0);
        kernel_ffts_[k].resize(n);
        potentials_[k].resize(n, 0.0);
        potential_ffts_[k].resize(n);
        fields_[k].resize(n, 0.0);
    }

    // Per-channel FFT cache
    channel_ffts_.resize(world_.num_channels);
    channel_deltas_.resize(world_.num_channels);
    channel_delta_weights_.resize(world_.num_channels, 0.0);
    for (int c = 0; c < world_.num_channels; ++c) {
        channel_ffts_[c].resize(n);
        channel_deltas_[c].resize(n, 0.0);
    }

    // Coordinate grids
    int ndim = static_cast<int>(shape.size());
    X_.resize(ndim);
    D_.resize(n, 0.0);
    for (int d = 0; d < ndim; ++d) X_[d].resize(n);

    for (int flat = 0; flat < n; ++flat) {
        int remainder = flat;
        for (int d = ndim - 1; d >= 0; --d) {
            int coord = remainder % shape[d];
            remainder /= shape[d];
            X_[d][flat] = static_cast<double>(coord - shape[d] / 2);
        }
    }

    calc_kernel();
}

double MultiAutomaton::kernel_shell(double dist, const KernelParams& params) const {
    double r_scale = params.r;
    double scaled_dist = dist / r_scale;
    if (scaled_dist >= 1.0 || scaled_dist <= 0.0) return 0.0;

    const auto& b = params.b;
    int B = static_cast<int>(b.size());
    double Br = B * scaled_dist;
    int ring_idx = std::min(static_cast<int>(std::floor(Br)), B - 1);
    double ring_weight = b[ring_idx];
    double core_r = std::fmod(Br, 1.0);
    core_r = std::min(core_r, 1.0);
    return kernel_core(params.kn - 1, core_r) * ring_weight;
}

void MultiAutomaton::calc_kernel() {
    int R = world_.kernels[0].R;
    int n = fft_->total();
    int ndim = static_cast<int>(world_.channels[0].shape().size());

    // Compute distance field normalized by R
    for (int i = 0; i < n; ++i) {
        double sum_sq = 0.0;
        for (int d = 0; d < ndim; ++d) {
            double xr = X_[d][i] / R;
            sum_sq += xr * xr;
        }
        D_[i] = std::sqrt(sum_sq);
    }

    // Compute each kernel
    for (int k = 0; k < num_kernels_; ++k) {
        kernel_sums_[k] = 0.0;
        for (int i = 0; i < n; ++i) {
            double val = kernel_shell(D_[i], world_.kernels[k]);
            kernels_[k].data()[i] = val;
            kernel_sums_[k] += val;
        }
        // Normalize
        if (kernel_sums_[k] > EPSILON) {
            for (int i = 0; i < n; ++i) {
                kernels_[k].data()[i] /= kernel_sums_[k];
            }
        }
        // FFT
        fft_->forward(kernels_[k].data(), kernel_ffts_[k]);
    }
}

void MultiAutomaton::calc_once(bool is_update) {
    int n = fft_->total();
    int nc = world_.num_channels;
    double dt = 1.0 / world_.kernels[0].T;

    // FFT all channels
    for (int c = 0; c < nc; ++c) {
        fft_->forward(world_.channels[c].data(), channel_ffts_[c]);
        std::fill(channel_deltas_[c].begin(), channel_deltas_[c].end(), 0.0);
        channel_delta_weights_[c] = 0.0;
    }

    for (int k = 0; k < num_kernels_; ++k) {
        const auto& kp = world_.kernels[k];
        int c0 = kp.c0;  // source channel
        int c1 = kp.c1;  // target channel

        // Convolve: potential = IFFT(kernel_FFT * channel_FFT[c0])
        for (int i = 0; i < n; ++i) {
            potential_ffts_[k][i] = kernel_ffts_[k][i] * channel_ffts_[c0][i];
        }
        fft_->inverse(potential_ffts_[k], potentials_[k]);
        fftshift_nd(potentials_[k], world_.channels[0].shape());

        // Growth function
        int gn = kp.gn - 1;
        for (int i = 0; i < n; ++i) {
            fields_[k][i] = growth_func(gn, potentials_[k][i], kp.m, kp.s);
        }

        // Accumulate into target channel
        double h = kp.h;
        for (int i = 0; i < n; ++i) {
            channel_deltas_[c1][i] += dt * h * fields_[k][i];
        }
        channel_delta_weights_[c1] += h;
    }

    // Update each channel
    for (int c = 0; c < nc; ++c) {
        auto& cells = world_.channels[c].data();
        auto& delta = channel_deltas_[c];
        double delta_weight = channel_delta_weights_[c];
        if (delta_weight > 0) {
            for (int i = 0; i < n; ++i) {
                double new_val = cells[i] + delta[i] / delta_weight;
                if (add_noise > 0.0) {
                    // Simple deterministic noise based on position
                    new_val *= 1.0 + (((i * 7919 + gen_) % 1000) / 1000.0 - 0.5) * (add_noise / 10.0);
                }
                cells[i] = std::clamp(new_val, 0.0, 1.0);
            }
        }
        if (world_.param_P > 0) {
            double P = static_cast<double>(world_.param_P);
            for (int i = 0; i < n; ++i) {
                cells[i] = std::round(cells[i] * P) / P;
            }
        }
    }

    if (is_update) {
        gen_++;
        time_ = std::round((time_ + dt) * 1e10) / 1e10;
    }
}

void MultiAutomaton::reset() {
    gen_ = 0;
    time_ = 0.0;
}

} // namespace lenia
