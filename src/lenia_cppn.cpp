#include "lenia_cppn.h"

namespace lenia {

CPPN::CPPN(int size_x, int size_y, int mid_x, int mid_y, int R,
           int z_size, double scale, int net_size, double variance)
    : size_x_(size_x), size_y_(size_y), z_size_(z_size), scale_(scale), net_size_(net_size) {

    int N = size_x * size_y;
    X_flat_.resize(N);
    Y_flat_.resize(N);
    D_flat_.resize(N);

    for (int r = 0; r < size_y; ++r) {
        for (int c = 0; c < size_x; ++c) {
            int idx = r * size_x + c;
            double xr = static_cast<double>(c - mid_x) / R;
            double yr = static_cast<double>(r - mid_y) / R;
            X_flat_[idx] = xr * scale;
            Y_flat_[idx] = yr * scale;
            D_flat_[idx] = std::sqrt(xr * xr + yr * yr) * scale;
        }
    }

    std::mt19937 rng(42);
    double var = (variance > 0) ? variance : std::uniform_real_distribution<double>(0.5, 5.0)(rng);
    init_model(var, rng);
}

void CPPN::init_model(double variance, std::mt19937& rng) {
    layers_.clear();
    int x = 3 + z_size_;  // input size: X, Y, D, Z[z_size]
    int n = net_size_;

    auto make_layer = [&](int in, int out, double stddev, Layer::Activation act) {
        Layer layer;
        layer.in_size = in;
        layer.out_size = out;
        layer.activation = act;
        std::normal_distribution<double> dist(0.0, stddev);
        layer.W.resize(in * out);
        for (auto& w : layer.W) w = dist(rng);
        layer.B.assign(out, 0.0);
        return layer;
    };

    layers_.push_back(make_layer(x, n, 3.0, Layer::TANH));
    layers_.push_back(make_layer(n, n, 1.0, Layer::TANH));
    layers_.push_back(make_layer(n, n, 1.0, Layer::TANH));
    layers_.push_back(make_layer(n, 1, 0.1, Layer::SIGMOID));
}

std::vector<double> CPPN::generate_z(std::mt19937& rng) const {
    std::normal_distribution<double> dist(0.0, 1.0);
    std::vector<double> z(z_size_);
    for (auto& v : z) v = dist(rng);
    return z;
}

std::vector<double> CPPN::matmul_add_activate(const std::vector<double>& input, int batch,
                                               const Layer& layer) const {
    int in_sz = layer.in_size;
    int out_sz = layer.out_size;
    std::vector<double> output(batch * out_sz);

    for (int b = 0; b < batch; ++b) {
        for (int j = 0; j < out_sz; ++j) {
            double sum = layer.B[j];
            for (int i = 0; i < in_sz; ++i) {
                sum += input[b * in_sz + i] * layer.W[i * out_sz + j];
            }
            if (layer.activation == Layer::TANH) {
                output[b * out_sz + j] = std::tanh(sum);
            } else {
                output[b * out_sz + j] = sigmoid(sum);
            }
        }
    }
    return output;
}

NDArray CPPN::generate(const std::vector<double>& z) const {
    int N = size_x_ * size_y_;
    int in_size = 3 + z_size_;

    // Build input: [X, Y, D, Z...] for each pixel
    std::vector<double> input(N * in_size);
    for (int i = 0; i < N; ++i) {
        input[i * in_size + 0] = X_flat_[i];
        input[i * in_size + 1] = Y_flat_[i];
        input[i * in_size + 2] = D_flat_[i];
        for (int j = 0; j < z_size_; ++j) {
            input[i * in_size + 3 + j] = z[j];
        }
    }

    // Forward pass through all layers
    std::vector<double> A = input;
    int current_cols = in_size;
    for (const auto& layer : layers_) {
        A = matmul_add_activate(A, N, layer);
        current_cols = layer.out_size;
    }

    // Reshape to 2D array
    NDArray result({size_y_, size_x_}, 0.0);
    for (int i = 0; i < N; ++i) {
        result[i] = A[i];  // output is 1-wide, so A[i*1 + 0]
    }
    return result;
}

} // namespace lenia
