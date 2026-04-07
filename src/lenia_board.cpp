#include "lenia_board.h"
#include <cmath>
#include <iterator>
#include <regex>

namespace lenia {

// ============================================================================
// NDArray
// ============================================================================

NDArray::NDArray(const std::vector<int>& shape) : shape_(shape) {
    compute_strides();
    int total = 1;
    for (int s : shape_) total *= s;
    data_.resize(total, 0.0);
}

NDArray::NDArray(const std::vector<int>& shape, double fill) : shape_(shape) {
    compute_strides();
    int total = 1;
    for (int s : shape_) total *= s;
    data_.resize(total, fill);
}

void NDArray::compute_strides() {
    strides_.resize(shape_.size());
    if (shape_.empty()) return;
    strides_.back() = 1;
    for (int i = static_cast<int>(shape_.size()) - 2; i >= 0; --i) {
        strides_[i] = strides_[i + 1] * shape_[i + 1];
    }
}

int NDArray::flat_index(const std::vector<int>& idx) const {
    int flat = 0;
    for (int i = 0; i < static_cast<int>(idx.size()); ++i) {
        flat += idx[i] * strides_[i];
    }
    return flat;
}

double& NDArray::at(const std::vector<int>& idx) {
    return data_[flat_index(idx)];
}

double NDArray::at(const std::vector<int>& idx) const {
    return data_[flat_index(idx)];
}

void NDArray::fill(double v) {
    std::fill(data_.begin(), data_.end(), v);
}

NDArray NDArray::copy() const {
    NDArray out;
    out.shape_ = shape_;
    out.strides_ = strides_;
    out.data_ = data_;
    return out;
}

double NDArray::sum() const {
    double s = 0.0;
    for (double v : data_) s += v;
    return s;
}

double NDArray::max_val() const {
    if (data_.empty()) return 0.0;
    return *std::max_element(data_.begin(), data_.end());
}

double NDArray::min_val() const {
    if (data_.empty()) return 0.0;
    return *std::min_element(data_.begin(), data_.end());
}

NDArray NDArray::operator+(const NDArray& o) const {
    NDArray out = copy();
    for (int i = 0; i < size(); ++i) out.data_[i] += o.data_[i];
    return out;
}

NDArray NDArray::operator-(const NDArray& o) const {
    NDArray out = copy();
    for (int i = 0; i < size(); ++i) out.data_[i] -= o.data_[i];
    return out;
}

NDArray NDArray::operator*(double scalar) const {
    NDArray out = copy();
    for (double& v : out.data_) v *= scalar;
    return out;
}

void NDArray::clip(double lo, double hi) {
    for (double& v : data_) v = std::clamp(v, lo, hi);
}

void NDArray::reshape(const std::vector<int>& new_shape) {
    int total = 1;
    for (int s : new_shape) total *= s;
    if (total != size()) {
        throw std::runtime_error("reshape: size mismatch");
    }
    shape_ = new_shape;
    compute_strides();
}

// ============================================================================
// RLECodec
// ============================================================================

static const std::map<int, std::string> DIM_DELIM = {
    {0, ""}, {1, "$"}, {2, "%"}, {3, "#"},
    {4, "@A"}, {5, "@B"}, {6, "@C"}, {7, "@D"}, {8, "@E"}, {9, "@F"}
};

int RLECodec::ch2val(const std::string& c) {
    if (c.empty()) return 0;
    if (c == "." || c == "b") return 0;
    if (c == "o") return 255;
    if (c.size() == 1) {
        char ch = c[0];
        if (ch >= 'A' && ch <= 'X') return ch - 'A' + 1;
        return 0;
    }
    if (c.size() == 2) {
        return (c[0] - 'p') * 24 + (c[1] - 'A' + 25);
    }
    return 0;
}

std::string RLECodec::val2ch(int v) {
    if (v == 0) return ".";
    if (v < 25) return std::string(1, 'A' + v - 1);
    return std::string(1, 'p' + (v - 25) / 24) + std::string(1, 'A' + (v - 25) % 24);
}

std::vector<double> RLECodec::st2fracs(const std::string& st) {
    std::vector<double> result;
    std::stringstream ss(st);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        size_t slash = token.find('/');
        if (slash != std::string::npos) {
            double num = std::stod(token.substr(0, slash));
            double den = std::stod(token.substr(slash + 1));
            result.push_back(num / den);
        } else {
            result.push_back(std::stod(token));
        }
    }
    return result;
}

std::string RLECodec::fracs2st(const std::vector<double>& B) {
    std::string result;
    for (size_t i = 0; i < B.size(); ++i) {
        if (i > 0) result += ",";
        std::ostringstream oss;
        oss << B[i];
        result += oss.str();
    }
    return result;
}

NDArray RLECodec::decode(const std::string& rle, int dim) {
    // Faithful port of Python Board.rle2arr() (LeniaND.py lines 169-191).
    // Uses stacks per dimension. Values go to stack[0], delimiters push
    // stack[d] into stack[d+1].
    //
    // Delimiters: dim0="" (value separator), dim1="$", dim2="%", dim3="#",
    //             dim4="@A", dim5="@B", ...

    // Build delimiter lookup: string → dimension index
    std::map<std::string, int> delim_to_dim;
    for (const auto& [d, s] : DIM_DELIM) {
        if (!s.empty() && d < dim) delim_to_dim[s] = d;
    }

    // stacks[d] = list of lists. stacks[0] accumulates values (innermost row).
    // When delimiter at dimension d is hit, stacks[0..d-1] are pushed into stacks[d].
    using RowT = std::vector<double>;
    using StackT = std::vector<RowT>;
    // Use a recursive nested-list representation
    // For simplicity: stacks[0] = current innermost row (doubles)
    //                 stacks[d] for d>0 = list of flattened sub-arrays

    // Actually, the Python uses a list-of-lists-of-... structure that gets
    // cubified into a numpy array. Let's replicate that exactly.

    // Simpler approach: parse into nested vectors, then cubify.
    // nested[dim-1] is the outermost list.

    // Phase 1: Parse into stacks (matching Python exactly)
    std::vector<std::vector<std::vector<double>>> stacks(dim);
    // stacks[0] = current row being built (list of scalar values)
    // stacks[1] = list of rows (list of list of scalars) for 2D
    // etc.

    // Actually Python stacks are: stacks[0] = current 1D row being built,
    // stacks[1] = list of 1D rows (for 2D), stacks[2] = list of 2D slices (for 3D), etc.
    // Each stack[d] is a list. When delimiter d is encountered, stack[0..d-1] are
    // pushed into stack[d], then stack[0..d-1] are cleared.

    // For the flat double representation, use a recursive structure:
    // We'll use a simplified approach that handles 2D and 3D correctly.

    std::string clean = rle;
    while (!clean.empty() && clean.back() == '!') clean.pop_back();
    // Append highest delimiter to terminate
    std::string highest_delim;
    {
        auto it = DIM_DELIM.find(dim - 1);
        if (it != DIM_DELIM.end()) highest_delim = it->second;
    }
    clean += highest_delim;

    std::vector<double> current_row;
    std::vector<std::vector<double>> current_slice;   // for 2D: rows
    std::vector<std::vector<std::vector<double>>> current_volume; // for 3D: slices

    std::string last, count;

    for (size_t i = 0; i < clean.size(); ++i) {
        char ch = clean[i];

        if (ch >= '0' && ch <= '9') { count += ch; continue; }

        if (ch == 'p' || ch == 'q' || ch == 'r' || ch == 's' || ch == 't' ||
            ch == 'u' || ch == 'v' || ch == 'w' || ch == 'x' || ch == 'y' || ch == '@') {
            last = std::string(1, ch);
            continue;
        }

        std::string token = last + std::string(1, ch);
        int n = count.empty() ? 1 : std::stoi(count);

        // Check if token is a delimiter
        auto dit = delim_to_dim.find(token);
        if (dit != delim_to_dim.end()) {
            int d = dit->second;
            // Push current_row into current_slice (dim 0 → dim 1)
            if (d >= 1) {
                current_slice.push_back(current_row);
                for (int k = 1; k < n; ++k) current_slice.push_back({});
                current_row.clear();
            }
            // Push current_slice into current_volume (dim 1 → dim 2)
            if (d >= 2) {
                current_volume.push_back(current_slice);
                for (int k = 1; k < n; ++k) current_volume.push_back({});
                current_slice.clear();
            }
            // (extend for dim >= 3 as needed)
        } else {
            // It's a value character
            int val = ch2val(token);
            double fval = val / 255.0;
            for (int k = 0; k < n; ++k) {
                current_row.push_back(fval);
            }
        }
        last.clear();
        count.clear();
    }

    // Phase 2: Cubify — pad all rows/slices to the same length, convert to NDArray
    if (dim == 2) {
        auto& rows = current_slice;
        if (!current_row.empty()) rows.push_back(current_row);
        int nrows = static_cast<int>(rows.size());
        int ncols = 0;
        for (auto& r : rows) ncols = std::max(ncols, static_cast<int>(r.size()));
        if (nrows == 0 || ncols == 0) return NDArray({1, 1}, 0.0);
        NDArray arr({nrows, ncols}, 0.0);
        for (int r = 0; r < nrows; ++r)
            for (int c = 0; c < static_cast<int>(rows[r].size()); ++c)
                arr.at2(r, c) = rows[r][c];
        return arr;
    }

    if (dim == 3) {
        auto& slices = current_volume;
        if (!current_slice.empty()) slices.push_back(current_slice);
        int nz = static_cast<int>(slices.size());
        int ny = 0, nx = 0;
        for (auto& sl : slices) {
            ny = std::max(ny, static_cast<int>(sl.size()));
            for (auto& r : sl) nx = std::max(nx, static_cast<int>(r.size()));
        }
        if (nz == 0 || ny == 0 || nx == 0) return NDArray({1, 1, 1}, 0.0);
        NDArray arr({nz, ny, nx}, 0.0);
        for (int z = 0; z < nz; ++z)
            for (int y = 0; y < static_cast<int>(slices[z].size()); ++y)
                for (int x = 0; x < static_cast<int>(slices[z][y].size()); ++x)
                    arr.at({z, y, x}) = slices[z][y][x];
        return arr;
    }

    // Fallback for dim >= 4: only 2D/3D are fully supported
    return NDArray(std::vector<int>(dim, 1), 0.0);
}

std::string RLECodec::encode(const NDArray& arr, bool shorten) {
    if (arr.ndim() != 2) {
        throw std::runtime_error("RLE encode only supports 2D arrays currently");
    }
    int rows = arr.shape(0);
    int cols = arr.shape(1);
    std::string result;

    for (int r = 0; r < rows; ++r) {
        if (r > 0) result += "$";
        int c = 0;
        while (c < cols) {
            int val = std::clamp(static_cast<int>(std::round(arr.at2(r, c) * 255)), 0, 255);
            int run = 1;
            if (shorten) {
                while (c + run < cols &&
                       std::clamp(static_cast<int>(std::round(arr.at2(r, c + run) * 255)), 0, 255) == val) {
                    run++;
                }
            }
            std::string ch = val2ch(val);
            if (shorten && run > 1) {
                result += std::to_string(run);
            }
            result += ch;
            c += run;
        }
    }
    result += "!";
    return result;
}

// ============================================================================
// Board
// ============================================================================

Board::Board(const std::vector<int>& size) : cells(size, 0.0) {}

Board Board::from_values(const NDArray& cells, const Params* params,
                         const std::vector<std::string>* names) {
    Board b;
    if (names) b.names = *names;
    if (params) b.params = *params;
    b.cells = cells.copy();
    return b;
}

Board Board::from_data(const std::map<std::string, std::string>& data, int dim) {
    Board b;
    auto get = [&](const std::string& key) -> std::string {
        auto it = data.find(key);
        return (it != data.end()) ? it->second : "";
    };

    b.names = {get("code"), get("name"), get("cname")};

    std::string R_s = get("R"), T_s = get("T"), m_s = get("m"), s_s = get("s");
    std::string kn_s = get("kn"), gn_s = get("gn"), b_s = get("b");

    if (!R_s.empty()) b.params.R = std::stoi(R_s);
    if (!T_s.empty()) b.params.T = std::stoi(T_s);
    if (!m_s.empty()) b.params.m = std::stod(m_s);
    if (!s_s.empty()) b.params.s = std::stod(s_s);
    if (!kn_s.empty()) b.params.kn = std::stoi(kn_s);
    if (!gn_s.empty()) b.params.gn = std::stoi(gn_s);
    if (!b_s.empty()) b.params.b = RLECodec::st2fracs(b_s);

    std::string cells_st = get("cells");
    if (!cells_st.empty()) {
        b.cells = RLECodec::decode(cells_st, dim);
    }
    return b;
}

std::map<std::string, std::string> Board::to_data(bool shorten) const {
    std::map<std::string, std::string> data;
    data["code"] = names[0];
    data["name"] = names[1];
    data["cname"] = names[2];
    data["R"] = std::to_string(params.R);
    data["T"] = std::to_string(params.T);
    data["b"] = RLECodec::fracs2st(params.b);
    data["m"] = std::to_string(params.m);
    data["s"] = std::to_string(params.s);
    data["kn"] = std::to_string(params.kn);
    data["gn"] = std::to_string(params.gn);
    data["cells"] = RLECodec::encode(cells, shorten);
    return data;
}

std::string Board::params2st() const {
    std::ostringstream oss;
    oss << "R=" << params.R << ",T=" << params.T
        << ",b=[" << RLECodec::fracs2st(params.b) << "]"
        << ",m=" << params.m << ",s=" << params.s
        << ",kn=" << params.kn << ",gn=" << params.gn;
    return oss.str();
}

std::string Board::long_name() const {
    return names[0] + " - " + names[1] + " " + names[2];
}

void Board::clear() {
    cells.fill(0.0);
}

void Board::add(const Board& part, const std::vector<int>& shift, bool centered) {
    if (cells.ndim() != 2 || part.cells.ndim() != 2) return;

    int h1 = cells.shape(0), w1 = cells.shape(1);
    int h2 = part.cells.shape(0), w2 = part.cells.shape(1);
    int h0 = std::min(h1, h2), w0 = std::min(w1, w2);

    int sr = centered ? (h1 - h0) / 2 + shift[0] : shift[0];
    int sc = centered ? (w1 - w0) / 2 + shift[1] : shift[1];
    int pr = centered ? (h2 - h0) / 2 : 0;
    int pc = centered ? (w2 - w0) / 2 : 0;

    for (int r = 0; r < h0; ++r) {
        for (int c = 0; c < w0; ++c) {
            double val = part.cells.at2(pr + r, pc + c);
            if (val > EPSILON) {
                int dr = ((sr + r) % h1 + h1) % h1;
                int dc = ((sc + c) % w1 + w1) % w1;
                cells.at2(dr, dc) = val;
            }
        }
    }
}

void Board::add_centered(const NDArray& pattern) {
    if (cells.ndim() != 2 || pattern.ndim() != 2) return;
    int or_ = (cells.shape(0) - pattern.shape(0)) / 2;
    int oc = (cells.shape(1) - pattern.shape(1)) / 2;
    for (int r = 0; r < pattern.shape(0); ++r) {
        for (int c = 0; c < pattern.shape(1); ++c) {
            double val = pattern.at2(r, c);
            if (val > EPSILON) {
                int dr = ((or_ + r) % cells.shape(0) + cells.shape(0)) % cells.shape(0);
                int dc = ((oc + c) % cells.shape(1) + cells.shape(1)) % cells.shape(1);
                cells.at2(dr, dc) = val;
            }
        }
    }
}

void Board::crop() {
    if (cells.ndim() != 2) return;
    int rows = cells.shape(0), cols = cells.shape(1);
    int min_r = rows, min_c = cols, max_r = -1, max_c = -1;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (cells.at2(r, c) > EPSILON) {
                min_r = std::min(min_r, r);
                min_c = std::min(min_c, c);
                max_r = std::max(max_r, r);
                max_c = std::max(max_c, c);
            }
        }
    }
    if (max_r < 0) {
        cells = NDArray({1, 1}, 0.0);
        return;
    }
    int nr = max_r - min_r + 1;
    int nc = max_c - min_c + 1;
    NDArray cropped({nr, nc}, 0.0);
    for (int r = 0; r < nr; ++r)
        for (int c = 0; c < nc; ++c)
            cropped.at2(r, c) = cells.at2(min_r + r, min_c + c);
    cells = std::move(cropped);
}

void Board::restore_to(Board& dest) const {
    dest.params = params;
    dest.cells = cells.copy();
    dest.names = names;
}

void Board::shift(const std::vector<int>& offsets) {
    if (cells.ndim() != 2) return;
    int rows = cells.shape(0), cols = cells.shape(1);
    NDArray shifted({rows, cols}, 0.0);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int sr = ((r + offsets[0]) % rows + rows) % rows;
            int sc = ((c + offsets[1]) % cols + cols) % cols;
            shifted.at2(sr, sc) = cells.at2(r, c);
        }
    }
    cells = std::move(shifted);
}

void Board::rotate_2d(double angle_degrees, bool wrap) {
    if (cells.ndim() != 2) return;
    int rows = cells.shape(0), cols = cells.shape(1);
    double angle_rad = angle_degrees * 3.14159265358979323846 / 180.0;
    double cos_a = std::cos(angle_rad), sin_a = std::sin(angle_rad);
    double cx = cols / 2.0, cy = rows / 2.0;

    NDArray rotated({rows, cols}, 0.0);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            double dx = c - cx, dy = r - cy;
            double src_c = cos_a * dx + sin_a * dy + cx;
            double src_r = -sin_a * dx + cos_a * dy + cy;
            int sr = static_cast<int>(std::round(src_r));
            int sc = static_cast<int>(std::round(src_c));
            if (wrap) {
                sr = ((sr % rows) + rows) % rows;
                sc = ((sc % cols) + cols) % cols;
                rotated.at2(r, c) = cells.at2(sr, sc);
            } else if (sr >= 0 && sr < rows && sc >= 0 && sc < cols) {
                rotated.at2(r, c) = cells.at2(sr, sc);
            }
        }
    }
    cells = std::move(rotated);
}

void Board::zoom_2d(double factor) {
    if (cells.ndim() != 2 || factor <= 0) return;
    int rows = cells.shape(0), cols = cells.shape(1);
    int new_rows = std::max(1, static_cast<int>(rows * factor));
    int new_cols = std::max(1, static_cast<int>(cols * factor));

    NDArray zoomed({new_rows, new_cols}, 0.0);
    for (int r = 0; r < new_rows; ++r) {
        for (int c = 0; c < new_cols; ++c) {
            int sr = static_cast<int>(r / factor);
            int sc = static_cast<int>(c / factor);
            sr = std::clamp(sr, 0, rows - 1);
            sc = std::clamp(sc, 0, cols - 1);
            zoomed.at2(r, c) = cells.at2(sr, sc);
        }
    }
    cells = std::move(zoomed);
}

void Board::flip(int axis) {
    if (cells.ndim() != 2) return;
    int rows = cells.shape(0), cols = cells.shape(1);
    NDArray flipped({rows, cols}, 0.0);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int fr = (axis == 0) ? (rows - 1 - r) : r;
            int fc = (axis == 1) ? (cols - 1 - c) : c;
            flipped.at2(fr, fc) = cells.at2(r, c);
        }
    }
    cells = std::move(flipped);
}

} // namespace lenia
