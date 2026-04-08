#include "lenia_analyzer.h"
#include <algorithm>
#include <numeric>

namespace lenia {

const std::vector<std::string> Analyzer::STAT_HEADERS = {
    "p_m", "p_s", "n", "t", "m", "g", "r", "d", "s", "w", "m_a", "x", "y", "l", "k", "w_k"
};

const std::map<std::string, std::string> Analyzer::STAT_NAMES = {
    {"p_m", "Param m"}, {"p_s", "Param s"}, {"n", "Gen (#)"}, {"t", "Time (s)"},
    {"m", "Mass (mg)"}, {"g", "Growth (mg/s)"}, {"r", "Gyradius (mm)"},
    {"d", "Mass-growth distance (mm)"}, {"s", "Speed (mm/s)"}, {"w", "Angular speed (deg/s)"},
    {"m_a", "Mass asymmetry (mg)"}, {"x", "X position(mm)"}, {"y", "Y position(mm)"},
    {"l", "Lyapunov exponent"}, {"k", "Rotational symmetry"}, {"w_k", "Rotational speed"}
};

Analyzer::Analyzer(Automaton& automaton, Board& world)
    : automaton_(automaton), world_(world) {
    reset();
}

void Analyzer::reset() {
    reset_values();
    reset_last();
    reset_position();
    reset_polar();
    clear_series();
}

void Analyzer::reset_values() {
    is_empty_ = false;
    is_full_ = false;
    mass_ = 0;
    growth_ = 0;
    inertia_ = 0;
    m_center_.clear();
    g_center_.clear();
    mg_dist_ = 0;
    m_shift_ = 0;
    m_angle_ = 0;
    m_rotate_ = 0;
    mass_asym_ = 0;
    mass_asym_dirty_ = false;
    lyapunov_ = 0;
}

void Analyzer::reset_last() {
    m_last_center_.clear();
    m_center_.clear();
    m_last_angle_ = 0;
    has_last_ = false;
}

void Analyzer::reset_position() {
    int dim = world_.cells.ndim();
    last_shift_idx_.assign(dim, 0.0);
    total_shift_idx_.assign(dim, 0.0);
}

void Analyzer::reset_polar() {
    polar_array_.clear();
    polar_avg_.clear();
    density_sum_.clear();
    density_ema_.clear();
    has_density_ema_ = false;
    rotate_wavg_.clear();
    symm_sides_ = 0;
    symm_angle_ = 0;
    symm_rotate_ = 0;
}

void Analyzer::clear_series() {
    current_ = StatRow();
    series.clear();
}

void Analyzer::ensure_coordinate_cache(int rows, int cols, int R) {
    if (rows == cached_rows_ && cols == cached_cols_ && R == cached_R_) return;

    cached_rows_ = rows;
    cached_cols_ = cols;
    cached_R_ = R;

    x_coords_.resize(cols);
    x_sq_.resize(cols);
    y_coords_.resize(rows);
    y_sq_.resize(rows);

    const double inv_R = 1.0 / static_cast<double>(R);
    const int mid_r = rows / 2;
    const int mid_c = cols / 2;

    for (int c = 0; c < cols; ++c) {
        const double xr = static_cast<double>(c - mid_c) * inv_R;
        x_coords_[c] = xr;
        x_sq_[c] = xr * xr;
    }
    for (int r = 0; r < rows; ++r) {
        const double yr = static_cast<double>(r - mid_r) * inv_R;
        y_coords_[r] = yr;
        y_sq_[r] = yr * yr;
    }
}

void Analyzer::ensure_mass_asymmetry() {
    if (!mass_asym_dirty_) return;
    mass_asym_dirty_ = false;
    mass_asym_ = 0.0;

    if (world_.cells.ndim() != 2) return;
    if (m_last_center_.size() != 2 || m_center_.size() != 2) return;

    const int rows = world_.cells.shape(0);
    const int cols = world_.cells.shape(1);
    const int R = world_.params.R;
    const int mid_r = rows / 2;
    const int mid_c = cols / 2;
    const auto& A = world_.cells.data();

    const double x0 = m_last_center_[0] * R + mid_c - last_shift_idx_[0];
    const double y0 = m_last_center_[1] * R + mid_r - last_shift_idx_[1];
    const double x1 = m_center_[0] * R + mid_c;
    const double y1 = m_center_[1] * R + mid_r;

    double mass_right = 0.0;
    double mass_left = 0.0;
    for (int r = 0; r < rows; ++r) {
        const int row_offset = r * cols;
        for (int c = 0; c < cols; ++c) {
            const double sign_val = (x1 - x0) * (r - y0) - (y1 - y0) * (c - x0);
            if (sign_val > 0) mass_right += A[row_offset + c];
            else if (sign_val < 0) mass_left += A[row_offset + c];
        }
    }
    mass_asym_ = mass_right - mass_left;
}

StatRow Analyzer::get_stat_row() {
    int R = world_.params.R;
    int T = world_.params.T;
    double pm = world_.params.m;
    double ps = world_.params.s;
    double RN = static_cast<double>(R * R);  // R^DIM for 2D

    ensure_mass_asymmetry();

    StatRow row;
    row.p_m = pm;
    row.p_s = ps;
    row.n = automaton_.gen();
    row.t = automaton_.time();
    row.m = mass_ / RN;
    row.g = growth_ / RN;
    row.r = (mass_ > EPSILON) ? std::sqrt(inertia_ / mass_) : 0.0;
    row.d = mg_dist_;
    row.s = m_shift_ * T;
    row.w = m_rotate_ * T;
    row.m_a = mass_asym_ / RN;
    if (!m_center_.empty()) {
        row.x = m_center_[0] * R + total_shift_idx_[0];
        row.y = -(m_center_[1] * R + total_shift_idx_[1]);
    }
    row.l = lyapunov_;
    row.k = symm_sides_;
    row.w_k = symm_rotate_ * T;
    return row;
}

void Analyzer::calc_stats(int polar_what) {
    // Save last frame's values
    m_last_center_ = m_center_;
    m_last_angle_ = m_angle_;
    has_last_ = !m_last_center_.empty();

    reset_values();

    if (world_.cells.ndim() != 2) return;

    int R = world_.params.R;
    int T = world_.params.T;
    int rows = world_.cells.shape(0);
    int cols = world_.cells.shape(1);
    int mid_r = rows / 2;
    int mid_c = cols / 2;
    ensure_coordinate_cache(rows, cols, R);

    const auto& A = world_.cells.data();
    const auto& F = automaton_.field();
    const auto& change = automaton_.change();

    double m0 = 0.0;
    double g0 = 0.0;
    double border_sum = 0.0;
    double mx_sum = 0.0, my_sum = 0.0;
    double mx2_sum = 0.0, my2_sum = 0.0;
    double gx_sum = 0.0, gy_sum = 0.0;
    double change_sum = 0.0;

    for (int r = 0; r < rows; ++r) {
        const int row_offset = r * cols;
        const double yr = y_coords_[r];
        const double yr2 = y_sq_[r];
        const bool border_row = (r == 0 || r == rows - 1);

        for (int c = 0; c < cols; ++c) {
            const int idx = row_offset + c;
            const double xr = x_coords_[c];
            const double a = A[idx];
            const double g = std::max(0.0, F[idx]);

            m0 += a;
            g0 += g;
            mx_sum += a * xr;
            my_sum += a * yr;
            mx2_sum += a * x_sq_[c];
            my2_sum += a * yr2;
            gx_sum += g * xr;
            gy_sum += g * yr;
            change_sum += change[idx];

            if (border_row || c == 0 || c == cols - 1) {
                border_sum += a;
            }
        }
    }

    mass_ = m0;
    growth_ = g0;
    is_empty_ = (mass_ < EPSILON);
    is_full_ = (border_sum > 0);

    if (m0 > EPSILON) {
        m_center_.resize(2);
        m_center_[0] = mx_sum / m0;
        m_center_[1] = my_sum / m0;
        double mu_x2 = mx2_sum - m_center_[0] * mx_sum;
        double mu_y2 = my2_sum - m_center_[1] * my_sum;
        inertia_ = mu_x2 + mu_y2;

        if (g0 > EPSILON) {
            g_center_.resize(2);
            g_center_[0] = gx_sum / g0;
            g_center_[1] = gy_sum / g0;
            double dx = m_center_[0] - g_center_[0];
            double dy = m_center_[1] - g_center_[1];
            mg_dist_ = std::sqrt(dx * dx + dy * dy);
        }

        // Speed and rotation (relative to last frame)
        if (has_last_ && m_last_center_.size() == 2) {
            double ux = m_center_[0];
            double uy = m_center_[1];
            double vx = m_last_center_[0] - last_shift_idx_[0] / R;
            double vy = m_last_center_[1] - last_shift_idx_[1] / R;
            double dx = ux - vx;
            double dy = uy - vy;
            m_shift_ = std::sqrt(dx * dx + dy * dy);
            m_angle_ = (m_shift_ >= EPSILON) ? std::atan2(dy, dx) * 180.0 / PI : 0.0;
            m_rotate_ = m_angle_ - m_last_angle_;
            m_rotate_ = std::fmod(m_rotate_ + 540.0, 360.0) - 180.0;
            if (automaton_.gen() <= 2) m_rotate_ = 0;
            mass_asym_dirty_ = true;
        }

        // Symmetry analysis
        if (is_calc_symmetry) {
            calc_symmetry();
        }

        if (automaton_.gen() > 0) {
            double log_change = std::log(std::max(EPSILON, std::abs(change_sum)));
            lyapunov_ += (log_change - lyapunov_) / automaton_.gen();
        }
    }
}

void Analyzer::calc_symmetry() {
    // Polar FFT symmetry analysis (2D only).
    // Maps world cells to polar coordinates centered at mass center,
    // computes 1D FFT along the angular axis per radius ring,
    // and finds the dominant rotational symmetry order.
    if (world_.cells.ndim() != 2) return;
    if (mass_ < EPSILON || m_center_.empty()) return;

    int rows = world_.cells.shape(0);
    int cols = world_.cells.shape(1);
    int mid_r = rows / 2, mid_c = cols / 2;
    int sizer = std::min(mid_r, mid_c);
    int sizeth = cols;
    int sizef = mid_c;

    // Sample cells in polar coordinates
    polar_array_.resize(sizer * sizeth, 0.0);
    for (int ri = 0; ri < sizer; ++ri) {
        double r_val = static_cast<double>(ri);
        for (int ti = 0; ti < sizeth; ++ti) {
            double th = PI * 0.5 + (2.0 * PI * ti) / sizeth;
            int py = static_cast<int>(r_val * std::sin(th) + mid_r);
            int px = static_cast<int>(r_val * std::cos(th) + mid_c);
            py = std::clamp(py, 0, rows - 1);
            px = std::clamp(px, 0, cols - 1);
            polar_array_[ri * sizeth + ti] = world_.cells.at2(py, px);
        }
    }

    // Compute FFT magnitude per radius ring to find symmetry
    // For each ring, compute DFT of the angular profile
    density_sum_.assign(sizef, 0.0);
    for (int ri = 0; ri < sizer; ++ri) {
        // Simple DFT for frequencies 0..sizef-1
        for (int k = 1; k < sizef; ++k) {
            double re = 0, im = 0;
            for (int ti = 0; ti < sizeth; ++ti) {
                double angle = -2.0 * PI * k * ti / sizeth;
                double val = polar_array_[ri * sizeth + ti];
                re += val * std::cos(angle);
                im += val * std::sin(angle);
            }
            double mag = std::sqrt(re * re + im * im);
            density_sum_[k] += mag;
        }
    }

    // Find dominant symmetry order (skip k=0 and k=1)
    int best_k = 2;
    double best_density = 0;
    for (int k = 2; k < std::min(sizef, 12); ++k) {
        if (density_sum_[k] > best_density) {
            best_density = density_sum_[k];
            best_k = k;
        }
    }
    symm_sides_ = best_k;
}

void Analyzer::add_stats() {
    int multi = std::max(1, world_.params.T / 10);
    if (series.empty()) new_segment();
    auto& segment = series.back();
    current_ = get_stat_row();
    segment.push_back(current_);

    if (is_trim_segment) {
        int limit = (automaton_.gen() <= SEGMENT_INIT * multi)
                    ? SEGMENT_INIT_LEN * multi
                    : SEGMENT_LEN * multi;
        while (static_cast<int>(segment.size()) > limit) {
            segment.erase(segment.begin());
        }
    }
}

void Analyzer::new_segment() {
    if (series.empty() || !series.back().empty()) {
        series.push_back({});
    }
}

void Analyzer::clear_segment() {
    if (!series.empty()) {
        if (series.back().empty()) series.pop_back();
        if (!series.empty()) series.back().clear();
    }
}

void Analyzer::invalidate_segment() {
    if (!series.empty()) {
        StatRow nan_row;
        nan_row.p_m = world_.params.m;
        nan_row.p_s = world_.params.s;
        series.back() = {nan_row};
        new_segment();
    }
}

void Analyzer::center_world() {
    if (mass_ < EPSILON || m_center_.empty()) return;
    if (world_.cells.ndim() != 2) return;

    int R = world_.params.R;
    int rows = world_.cells.shape(0);
    int cols = world_.cells.shape(1);

    int shift_r = static_cast<int>(m_center_[1] * R);
    int shift_c = static_cast<int>(m_center_[0] * R);
    last_shift_idx_ = {static_cast<double>(shift_c), static_cast<double>(shift_r)};
    total_shift_idx_[0] += shift_c;
    total_shift_idx_[1] += shift_r;

    // Roll world.cells
    NDArray shifted({rows, cols}, 0.0);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int sr = ((r - shift_r) % rows + rows) % rows;
            int sc = ((c - shift_c) % cols + cols) % cols;
            shifted.at2(r, c) = world_.cells.at2(sr, sc);
        }
    }
    world_.cells = std::move(shifted);
}

} // namespace lenia
