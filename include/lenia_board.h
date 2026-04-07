#pragma once

#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <map>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace lenia {

static constexpr double EPSILON = 1e-10;
static constexpr int ROUND = 10;

struct Params {
    int R = 13;
    int T = 10;
    std::vector<double> b = {1.0};
    double m = 0.1;
    double s = 0.01;
    int kn = 1;
    int gn = 1;

    Params copy() const { return *this; }
};

/// N-dimensional array stored flat with shape metadata.
/// For 2D: shape = {height, width}, strides = {width, 1}.
class NDArray {
public:
    NDArray() = default;
    explicit NDArray(const std::vector<int>& shape);
    NDArray(const std::vector<int>& shape, double fill);

    int ndim() const { return static_cast<int>(shape_.size()); }
    const std::vector<int>& shape() const { return shape_; }
    int shape(int dim) const { return shape_[dim]; }
    int size() const { return static_cast<int>(data_.size()); }

    double& operator[](int flat) { return data_[flat]; }
    double operator[](int flat) const { return data_[flat]; }

    double& at(const std::vector<int>& idx);
    double at(const std::vector<int>& idx) const;

    /// 2D convenience accessors
    double& at2(int r, int c) { return data_[r * shape_[1] + c]; }
    double at2(int r, int c) const { return data_[r * shape_[1] + c]; }

    std::vector<double>& data() { return data_; }
    const std::vector<double>& data() const { return data_; }

    void fill(double v);
    NDArray copy() const;
    double sum() const;
    double max_val() const;
    double min_val() const;

    /// Element-wise operations (return new array)
    NDArray operator+(const NDArray& o) const;
    NDArray operator-(const NDArray& o) const;
    NDArray operator*(double scalar) const;

    /// In-place clip to [lo, hi]
    void clip(double lo, double hi);

    /// Reshape (total size must match)
    void reshape(const std::vector<int>& new_shape);

private:
    std::vector<int> shape_;
    std::vector<int> strides_;
    std::vector<double> data_;

    void compute_strides();
    int flat_index(const std::vector<int>& idx) const;
};

/// RLE encoding/decoding for Lenia patterns (n-dimensional).
class RLECodec {
public:
    /// Decode an RLE string into an NDArray. `dim` is the number of dimensions.
    static NDArray decode(const std::string& rle, int dim);

    /// Encode an NDArray into an RLE string.
    static std::string encode(const NDArray& arr, bool shorten = true);

    /// Character ↔ value conversion (Lenia encoding: A=1..X=24, pA=25..yO=255)
    static int ch2val(const std::string& c);
    static std::string val2ch(int v);

    /// Fraction string parsing: "1/3,1/2,1" → vector of doubles
    static std::vector<double> st2fracs(const std::string& st);
    static std::string fracs2st(const std::vector<double>& B);
};

/// The Board: holds the world state (cells) and parameters.
class Board {
public:
    Board() = default;
    explicit Board(const std::vector<int>& size);

    /// Factory methods
    static Board from_values(const NDArray& cells, const Params* params = nullptr,
                             const std::vector<std::string>* names = nullptr);
    static Board from_data(const std::map<std::string, std::string>& data, int dim);

    /// Serialization
    std::map<std::string, std::string> to_data(bool shorten = true) const;
    std::string params2st() const;
    std::string long_name() const;

    /// World manipulation
    void clear();
    void add(const Board& part, const std::vector<int>& shift, bool centered = true);
    void add_centered(const NDArray& pattern);
    void crop();
    void restore_to(Board& dest) const;

    /// Transform operations
    void shift(const std::vector<int>& offsets);
    void flip(int axis);
    void rotate_2d(double angle_degrees, bool wrap = false);
    void zoom_2d(double factor);

    /// Data members (public, matching Python)
    std::vector<std::string> names = {"", "", ""};
    Params params;
    int param_P = 0;
    NDArray cells;

private:
    void recur_add(int dim, NDArray& cells1, const NDArray& cells2,
                   const std::vector<int>& shift, bool centered,
                   std::vector<int>& idx1, std::vector<int>& idx2);
};

} // namespace lenia
