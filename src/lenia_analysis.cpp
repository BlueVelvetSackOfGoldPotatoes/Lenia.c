#include "lenia_analysis.h"
#include <cmath>
#include <map>
#include <set>
#include <queue>
#include <complex>

namespace lenia {
namespace analysis {

// ============================================================================
// Shannon Entropy
// ============================================================================

double shannon_entropy(const NDArray& cells, int n_bins) {
    std::vector<int> bins(n_bins, 0);
    int total = 0;
    for (int i = 0; i < cells.size(); ++i) {
        int bin = std::clamp(static_cast<int>(cells[i] * n_bins), 0, n_bins - 1);
        bins[bin]++;
        total++;
    }
    if (total == 0) return 0;
    double H = 0;
    for (int b = 0; b < n_bins; ++b) {
        if (bins[b] > 0) {
            double p = static_cast<double>(bins[b]) / total;
            H -= p * std::log2(p);
        }
    }
    return H;
}

// ============================================================================
// Spatial Entropy
// ============================================================================

double spatial_entropy(const NDArray& cells, int patch_size) {
    if (cells.ndim() != 2) return 0;
    int rows = cells.shape(0), cols = cells.shape(1);
    int half = patch_size / 2;

    // Hash local patches
    std::map<int, int> patch_counts;
    int total = 0;
    for (int r = half; r < rows - half; ++r) {
        for (int c = half; c < cols - half; ++c) {
            int hash = 0;
            for (int dr = -half; dr <= half; ++dr) {
                for (int dc = -half; dc <= half; ++dc) {
                    int bin = std::clamp(static_cast<int>(cells.at2(r + dr, c + dc) * 8), 0, 7);
                    hash = hash * 8 + bin;
                }
            }
            patch_counts[hash]++;
            total++;
        }
    }
    if (total == 0) return 0;
    double H = 0;
    for (auto& [hash, count] : patch_counts) {
        double p = static_cast<double>(count) / total;
        H -= p * std::log2(p);
    }
    return H;
}

// ============================================================================
// Active Information Storage
// ============================================================================

double active_information_storage(const NDArray& prev, const NDArray& curr, int n_bins) {
    if (prev.size() != curr.size() || prev.size() == 0) return 0;

    // Joint and marginal histograms
    std::vector<int> hist_prev(n_bins, 0);
    std::vector<int> hist_curr(n_bins, 0);
    std::vector<std::vector<int>> hist_joint(n_bins, std::vector<int>(n_bins, 0));
    int N = prev.size();

    for (int i = 0; i < N; ++i) {
        int bp = std::clamp(static_cast<int>(prev[i] * n_bins), 0, n_bins - 1);
        int bc = std::clamp(static_cast<int>(curr[i] * n_bins), 0, n_bins - 1);
        hist_prev[bp]++;
        hist_curr[bc]++;
        hist_joint[bp][bc]++;
    }

    // MI = sum p(x,y) * log2(p(x,y) / (p(x)*p(y)))
    double mi = 0;
    for (int bp = 0; bp < n_bins; ++bp) {
        for (int bc = 0; bc < n_bins; ++bc) {
            if (hist_joint[bp][bc] > 0 && hist_prev[bp] > 0 && hist_curr[bc] > 0) {
                double pxy = static_cast<double>(hist_joint[bp][bc]) / N;
                double px = static_cast<double>(hist_prev[bp]) / N;
                double py = static_cast<double>(hist_curr[bc]) / N;
                mi += pxy * std::log2(pxy / (px * py));
            }
        }
    }
    return mi;
}

// ============================================================================
// Integration Phi (simplified)
// ============================================================================

double integration_phi(const NDArray& cells) {
    if (cells.ndim() != 2) return 0;
    int rows = cells.shape(0), cols = cells.shape(1);
    int mid_r = rows / 2, mid_c = cols / 2;

    // H(whole)
    double H_whole = shannon_entropy(cells);

    // H(quadrants) — partition into 4
    double H_parts = 0;
    for (int qr = 0; qr < 2; ++qr) {
        for (int qc = 0; qc < 2; ++qc) {
            int r0 = qr * mid_r, r1 = (qr + 1) * mid_r;
            int c0 = qc * mid_c, c1 = (qc + 1) * mid_c;
            NDArray quad({r1 - r0, c1 - c0}, 0.0);
            for (int r = r0; r < r1; ++r)
                for (int c = c0; c < c1; ++c)
                    quad.at2(r - r0, c - c0) = cells.at2(r, c);
            H_parts += shannon_entropy(quad);
        }
    }

    // Phi ≈ H(parts) - H(whole): positive means the whole has less entropy
    // than the sum of parts, indicating integration
    return std::max(0.0, H_parts - H_whole);
}

// ============================================================================
// Autopoiesis
// ============================================================================

AutopoiesisResult autopoiesis(const NDArray& cells, double threshold) {
    AutopoiesisResult result;
    if (cells.ndim() != 2) return result;
    int rows = cells.shape(0), cols = cells.shape(1);

    double boundary_grad_sum = 0;
    double interior_grad_sum = 0;
    int boundary_count = 0;
    int interior_count = 0;

    for (int r = 1; r < rows - 1; ++r) {
        for (int c = 1; c < cols - 1; ++c) {
            double v = cells.at2(r, c);
            if (v < threshold) continue;

            // Gradient magnitude (Sobel-like)
            double gx = cells.at2(r, c + 1) - cells.at2(r, c - 1);
            double gy = cells.at2(r + 1, c) - cells.at2(r - 1, c);
            double grad = std::sqrt(gx * gx + gy * gy);

            // Is this a boundary cell? (has at least one neighbor below threshold)
            bool is_boundary = false;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dr == 0 && dc == 0) continue;
                    if (cells.at2(r + dr, c + dc) < threshold) {
                        is_boundary = true;
                        break;
                    }
                }
                if (is_boundary) break;
            }

            if (is_boundary) {
                boundary_grad_sum += grad;
                boundary_count++;
            } else {
                interior_grad_sum += grad;
                interior_count++;
            }
        }
    }

    result.boundary_cells = boundary_count;
    result.interior_cells = interior_count;
    result.boundary_strength = boundary_count > 0 ? boundary_grad_sum / boundary_count : 0;
    result.interior_smoothness = interior_count > 0 ? interior_grad_sum / interior_count : 0;
    result.score = result.boundary_strength / std::max(result.interior_smoothness, EPSILON);

    return result;
}

// ============================================================================
// Connected Components
// ============================================================================

int count_components(const NDArray& cells, double threshold) {
    if (cells.ndim() != 2) return 0;
    int rows = cells.shape(0), cols = cells.shape(1);
    std::vector<bool> visited(rows * cols, false);
    int components = 0;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (cells.at2(r, c) < threshold || visited[r * cols + c]) continue;

            // BFS flood fill
            components++;
            std::queue<std::pair<int, int>> q;
            q.push({r, c});
            visited[r * cols + c] = true;

            while (!q.empty()) {
                auto [cr, cc] = q.front(); q.pop();
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        if (dr == 0 && dc == 0) continue;
                        int nr = cr + dr, nc = cc + dc;
                        if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                        if (visited[nr * cols + nc]) continue;
                        if (cells.at2(nr, nc) < threshold) continue;
                        visited[nr * cols + nc] = true;
                        q.push({nr, nc});
                    }
                }
            }
        }
    }
    return components;
}

// ============================================================================
// Reproduction Detection
// ============================================================================

ReproductionResult detect_reproduction(const NDArray& before, const NDArray& after,
                                        double threshold) {
    ReproductionResult result;
    result.components_before = count_components(before, threshold);
    result.components_after = count_components(after, threshold);
    result.reproduced = result.components_after > result.components_before;
    result.died = result.components_after == 0;
    return result;
}

// ============================================================================
// Power Spectrum
// ============================================================================

std::pair<std::vector<double>, std::vector<double>> power_spectrum(
    const std::vector<double>& signal, double sample_rate) {
    int N = static_cast<int>(signal.size());
    if (N < 4) return {{}, {}};

    // Zero-pad to power of 2
    int n = 1;
    while (n < N) n <<= 1;
    std::vector<std::complex<double>> fft_in(n, {0, 0});
    for (int i = 0; i < N; ++i) fft_in[i] = {signal[i], 0};

    // Simple DFT (small enough for direct computation)
    std::vector<std::complex<double>> fft_out(n);
    for (int k = 0; k < n / 2; ++k) {
        std::complex<double> sum(0, 0);
        for (int t = 0; t < n; ++t) {
            double angle = -2.0 * 3.14159265358979 * k * t / n;
            sum += fft_in[t] * std::complex<double>(std::cos(angle), std::sin(angle));
        }
        fft_out[k] = sum;
    }

    std::vector<double> freqs, magnitudes;
    for (int k = 1; k < n / 2; ++k) {
        freqs.push_back(k * sample_rate / n);
        magnitudes.push_back(std::abs(fft_out[k]) / n);
    }
    return {freqs, magnitudes};
}

// ============================================================================
// Behavioral Taxonomy
// ============================================================================

TaxonomyResult classify_behavior(const std::vector<double>& mass_history,
                                  const std::vector<double>& speed_history,
                                  const std::vector<double>& gyradius_history,
                                  int T) {
    TaxonomyResult result;
    int N = static_cast<int>(mass_history.size());
    if (N < 10) { result.primary = "unknown"; return result; }

    // Use second half for stability analysis
    int half = N / 2;
    double mass_mean = 0, speed_mean = 0;
    for (int i = half; i < N; ++i) {
        mass_mean += mass_history[i];
        speed_mean += speed_history[i];
    }
    mass_mean /= (N - half);
    speed_mean /= (N - half);

    if (mass_mean < 1.0) {
        result.primary = "dead"; result.confidence = 1.0;
        return result;
    }

    // Mass variance
    double mass_var = 0;
    for (int i = half; i < N; ++i) {
        double d = mass_history[i] - mass_mean;
        mass_var += d * d;
    }
    mass_var /= (N - half);

    // Detect oscillation period via autocorrelation
    std::vector<double> mass_late(mass_history.begin() + half, mass_history.end());
    auto [psd_f, psd_m] = power_spectrum(mass_late, static_cast<double>(T));
    double peak_freq = 0, peak_mag = 0;
    for (size_t i = 0; i < psd_f.size(); ++i) {
        if (psd_m[i] > peak_mag) {
            peak_mag = psd_m[i];
            peak_freq = psd_f[i];
        }
    }
    result.period = (peak_freq > 0) ? 1.0 / peak_freq : 0;
    result.drift_speed = speed_mean;

    bool is_moving = speed_mean > 0.001;
    bool is_oscillating = mass_var > 2.0 && peak_mag > mass_mean * 0.01;

    if (is_moving && is_oscillating) {
        result.primary = "glider";
        result.secondary = "pulsating_glider";
    } else if (is_moving) {
        result.primary = "glider";
        result.secondary = "smooth_glider";
    } else if (is_oscillating) {
        result.primary = "oscillator";
        result.secondary = (result.period > 0 && result.period < 50) ? "breather" : "pulsator";
    } else {
        result.primary = "still";
        result.secondary = mass_mean > 50 ? "large_still" : "small_still";
    }

    result.confidence = 0.8;  // simplified
    return result;
}

// ============================================================================
// Full Analysis Report
// ============================================================================

AnalysisReport full_analysis(const NDArray& cells,
                              const NDArray* prev_cells,
                              const std::vector<double>& mass_history,
                              const std::vector<double>& speed_history,
                              const std::vector<double>& gyradius_history,
                              int T) {
    AnalysisReport report;

    report.shannon = shannon_entropy(cells);
    report.spatial = spatial_entropy(cells);
    report.phi = integration_phi(cells);
    report.autopoiesis = autopoiesis(cells);
    report.components = count_components(cells);

    if (!mass_history.empty()) {
        report.taxonomy = classify_behavior(mass_history, speed_history, gyradius_history, T);
        auto [f, m] = power_spectrum(mass_history, static_cast<double>(T));
        report.psd_freq = f;
        report.psd_magnitude = m;
    }

    return report;
}

} // namespace analysis
} // namespace lenia
