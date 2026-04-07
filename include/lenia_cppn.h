#pragma once

#include "lenia_board.h"
#include <vector>
#include <random>
#include <cmath>
#include <functional>

namespace lenia {

/// CPPN: Compositional Pattern-Producing Network for generating initial patterns.
/// Faithful port of Python CPPN class from LeniaND.py.
///
/// A small neural network that takes (x, y, distance, z_latent) as input and
/// produces a cell value. By varying the latent z vector, different patterns emerge.
class CPPN {
public:
    CPPN(int size_x, int size_y, int mid_x, int mid_y, int R,
         int z_size = 8, double scale = 1.0, int net_size = 32,
         double variance = -1.0);

    /// Generate a random latent vector z.
    std::vector<double> generate_z(std::mt19937& rng) const;

    /// Generate a 2D pattern from a latent vector z. Returns flat array of size_x * size_y.
    NDArray generate(const std::vector<double>& z) const;

    static double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

private:
    int size_x_, size_y_;
    int z_size_;
    double scale_;
    int net_size_;

    // Flattened coordinate inputs (N = size_x * size_y)
    std::vector<double> X_flat_;  // x coords, scaled
    std::vector<double> Y_flat_;  // y coords, scaled
    std::vector<double> D_flat_;  // distance from center, scaled

    // Network layers: W[layer] is (rows x cols), B[layer] is (cols), F[layer] is activation
    struct Layer {
        std::vector<double> W;  // row-major
        std::vector<double> B;
        int in_size, out_size;
        enum Activation { TANH, SIGMOID } activation;
    };
    std::vector<Layer> layers_;

    void init_model(double variance, std::mt19937& rng);
    std::vector<double> matmul_add_activate(const std::vector<double>& input, int batch,
                                             const Layer& layer) const;
};

} // namespace lenia
