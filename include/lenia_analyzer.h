#pragma once

#include "lenia_board.h"
#include "lenia_automaton.h"
#include <vector>
#include <string>
#include <map>
#include <cmath>

namespace lenia {

/// Statistics row: one snapshot of organism state per generation.
struct StatRow {
    double p_m = 0;     // Param m
    double p_s = 0;     // Param s
    int n = 0;          // Generation #
    double t = 0;       // Time (s)
    double m = 0;       // Mass (mg)
    double g = 0;       // Growth (mg/s)
    double r = 0;       // Gyradius (mm)
    double d = 0;       // Mass-growth distance (mm)
    double s = 0;       // Speed (mm/s)
    double w = 0;       // Angular speed (deg/s)
    double m_a = 0;     // Mass asymmetry (mg)
    double x = 0;       // X position (mm)
    double y = 0;       // Y position (mm)
    double l = 0;       // Lyapunov exponent
    double k = 0;       // Rotational symmetry
    double w_k = 0;     // Rotational speed
};

/// Analyzer: computes statistics about organisms for analysis and search.
/// Faithful port of Python Analyzer class from LeniaND.py.
class Analyzer {
public:
    static const std::vector<std::string> STAT_HEADERS;
    static const std::map<std::string, std::string> STAT_NAMES;

    static constexpr int RECURRENCE_RANGE_START = 4;
    static constexpr int RECURRENCE_RANGE_END = 11;
    static constexpr int SEGMENT_INIT = 128;
    static constexpr int SEGMENT_INIT_LEN = 64;
    static constexpr int SEGMENT_LEN = 512;
    static constexpr int PSD_INTERVAL = 32;

    explicit Analyzer(Automaton& automaton, Board& world);

    void reset();
    void reset_values();
    void reset_last();
    void reset_position();
    void reset_polar();
    void clear_series();

    /// Compute all statistics for the current generation.
    void calc_stats(int polar_what = 0);

    /// Compute polar FFT for symmetry analysis (2D only).
    void calc_symmetry();

    /// Get current stat row as a StatRow struct.
    StatRow get_stat_row() const;

    /// Add current stats to the time series.
    void add_stats();

    /// Start a new segment in the series.
    void new_segment();
    void clear_segment();
    void invalidate_segment();

    /// Center the organism at the world origin.
    void center_world();

    /// Accessors
    bool is_empty() const { return is_empty_; }
    bool is_full() const { return is_full_; }
    double mass() const { return mass_; }
    double growth() const { return growth_; }
    double speed() const { return m_shift_; }
    double gyradius() const { return (mass_ > EPSILON) ? std::sqrt(inertia_ / mass_) : 0.0; }
    double lyapunov() const { return lyapunov_; }
    int symmetry() const { return static_cast<int>(symm_sides_); }

    /// Settings
    bool is_trim_segment = true;
    bool is_calc_symmetry = false;
    bool is_calc_psd = false;

    /// Time series data
    using Segment = std::vector<StatRow>;
    std::vector<Segment> series;

private:
    Automaton& automaton_;
    Board& world_;

    bool is_empty_ = false;
    bool is_full_ = false;
    double mass_ = 0;
    double growth_ = 0;
    double inertia_ = 0;
    std::vector<double> m_center_;      // center of mass (per-dim)
    std::vector<double> g_center_;      // center of growth
    double mg_dist_ = 0;
    double m_shift_ = 0;
    double m_angle_ = 0;
    double m_rotate_ = 0;
    double mass_asym_ = 0;
    double lyapunov_ = 0;
    double symm_sides_ = 0;
    double symm_angle_ = 0;
    double symm_rotate_ = 0;

    // Last-frame tracking
    std::vector<double> m_last_center_;
    double m_last_angle_ = 0;
    bool has_last_ = false;

    // Position tracking
    std::vector<double> last_shift_idx_;
    std::vector<double> total_shift_idx_;

    // Polar analysis (2D only)
    std::vector<double> polar_array_;
    std::vector<double> polar_avg_;
    std::vector<double> density_sum_;
    std::vector<double> density_ema_;
    bool has_density_ema_ = false;
    double ema_alpha_ = 0.05;
    std::vector<double> rotate_wavg_;

    StatRow current_;
};

} // namespace lenia
