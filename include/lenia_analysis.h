#pragma once

#include "lenia_board.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace lenia {

/// Advanced analysis tools for Lenia organisms.
/// Measures information-theoretic complexity, boundary integrity (autopoiesis),
/// reproduction events, and behavioral taxonomy.
namespace analysis {

/// Shannon entropy of cell values (binned into N_BINS levels).
double shannon_entropy(const NDArray& cells, int n_bins = 32);

/// Spatial entropy: entropy of local 3x3 neighborhoods.
/// Higher = more spatially complex patterns.
double spatial_entropy(const NDArray& cells, int patch_size = 3);

/// Active Information Storage: mutual information between a cell's past and present.
/// Requires two successive frames. Higher = more memory/predictability.
double active_information_storage(const NDArray& prev, const NDArray& curr, int n_bins = 16);

/// Integration (simplified Phi): how much the system is more than the sum of parts.
/// Splits the grid into quadrants and measures information lost by partitioning.
/// Higher = more integrated/holistic organism.
double integration_phi(const NDArray& cells);

/// Autopoiesis score: measures boundary integrity.
/// An autopoietic organism maintains a clear boundary between self and environment.
/// Score = (mean boundary gradient) / (mean interior gradient).
/// High score = sharp boundary, smooth interior = self-maintaining.
struct AutopoiesisResult {
    double boundary_strength = 0;
    double interior_smoothness = 0;
    double score = 0;              // boundary_strength / max(interior_smoothness, epsilon)
    int boundary_cells = 0;
    int interior_cells = 0;
};
AutopoiesisResult autopoiesis(const NDArray& cells, double threshold = 0.05);

/// Reproduction detection: compare two frames and check if a single connected
/// component split into two or more. Returns the number of components detected.
struct ReproductionResult {
    int components_before = 0;
    int components_after = 0;
    bool reproduced = false;       // components increased
    bool died = false;             // components decreased to 0
};
ReproductionResult detect_reproduction(const NDArray& before, const NDArray& after,
                                        double threshold = 0.05);

/// Count connected components in a thresholded 2D array.
int count_components(const NDArray& cells, double threshold = 0.05);

/// Behavioral taxonomy: classify an organism based on time series of stats.
struct TaxonomyResult {
    std::string primary;           // "still", "oscillator", "glider", "rotator", "chaotic", "dead"
    std::string secondary;         // more specific: "spinner", "pulsator", "breather", etc.
    double confidence = 0;
    double period = 0;             // oscillation period (0 if not oscillating)
    double drift_speed = 0;
    double rotation_speed = 0;
};
TaxonomyResult classify_behavior(const std::vector<double>& mass_history,
                                  const std::vector<double>& speed_history,
                                  const std::vector<double>& gyradius_history,
                                  int T = 10);

/// Compute power spectral density of a time series. Returns (frequencies, magnitudes).
std::pair<std::vector<double>, std::vector<double>> power_spectrum(
    const std::vector<double>& signal, double sample_rate = 1.0);

/// Full analysis report on one organism snapshot + time series.
struct AnalysisReport {
    double shannon = 0;
    double spatial = 0;
    double phi = 0;
    AutopoiesisResult autopoiesis;
    int components = 0;
    TaxonomyResult taxonomy;
    std::vector<double> psd_freq;
    std::vector<double> psd_magnitude;
};

AnalysisReport full_analysis(const NDArray& cells,
                              const NDArray* prev_cells,
                              const std::vector<double>& mass_history,
                              const std::vector<double>& speed_history,
                              const std::vector<double>& gyradius_history,
                              int T = 10);

} // namespace analysis
} // namespace lenia
