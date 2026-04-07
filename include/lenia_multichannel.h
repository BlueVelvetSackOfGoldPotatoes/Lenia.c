#pragma once

#include "lenia_board.h"
#include "lenia_automaton.h"
#include <vector>
#include <memory>

namespace lenia {

/// Per-kernel parameters for multi-channel Lenia (LeniaNDKC).
struct KernelParams {
    int R = 13;
    int T = 10;
    std::vector<double> b = {1.0};
    double m = 0.1;
    double s = 0.01;
    double h = 1.0;         // growth weight/height
    double r = 1.0;         // ring radius scaling
    int kn = 1;             // kernel core type (1-4)
    int gn = 1;             // growth function type (1-3)
    int c0 = 0;             // source channel (read from)
    int c1 = 0;             // target channel (write to)
};

/// Multi-channel Board: holds multiple cell arrays (one per channel).
struct MultiBoard {
    std::vector<std::string> names = {"", "", ""};
    std::vector<KernelParams> kernels;   // one per kernel
    int num_channels = 1;
    int param_P = 0;
    std::vector<NDArray> channels;       // one NDArray per channel

    MultiBoard() = default;
    MultiBoard(int num_channels, const std::vector<int>& size);

    void clear();
    double total_mass() const;

    /// Get a composite view (max across channels) for rendering
    NDArray composite_view() const;
};

/// Multi-channel Automaton: N kernels, C channels, cross-channel interaction.
/// Faithful port of LeniaNDKC.py Automaton.calc_once().
///
/// Each kernel k:
///   1. Convolves channel c0 with kernel k's spatial kernel
///   2. Applies growth function g(potential, m_k, s_k)
///   3. Accumulates h_k * growth into channel c1's delta
///
/// Then: A_new[c] = clip(A[c] + sum_k(h_k * field_k) / sum_k(h_k), 0, 1)
class MultiAutomaton {
public:
    MultiAutomaton(MultiBoard& world);

    void calc_kernel();
    void calc_once(bool is_update = true);
    void reset();

    int gen() const { return gen_; }
    double time() const { return time_; }

    /// Per-kernel accessors
    const std::vector<NDArray>& kernels_spatial() const { return kernels_; }
    const std::vector<std::vector<double>>& potentials() const { return potentials_; }
    const std::vector<std::vector<double>>& fields() const { return fields_; }

    bool is_soft_clip = false;
    double add_noise = 0.0;

private:
    MultiBoard& world_;
    int num_kernels_;

    std::unique_ptr<FFTND> fft_;

    // Per-kernel data
    std::vector<NDArray> kernels_;
    std::vector<ComplexVec> kernel_ffts_;
    std::vector<double> kernel_sums_;
    std::vector<std::vector<double>> potentials_;
    std::vector<ComplexVec> potential_ffts_;
    std::vector<std::vector<double>> fields_;

    // Per-channel FFT cache
    std::vector<ComplexVec> channel_ffts_;

    // Coordinate grids
    std::vector<std::vector<double>> X_;
    std::vector<double> D_;

    int gen_ = 0;
    double time_ = 0.0;

    double kernel_shell(double dist, const KernelParams& params) const;
};

} // namespace lenia
