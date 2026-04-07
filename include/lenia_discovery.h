#pragma once

#include "lenia_board.h"
#include "lenia_automaton.h"
#include "lenia_analyzer.h"
#include "lenia_app.h"
#include <vector>
#include <string>
#include <random>
#include <functional>
#include <chrono>
#include <fstream>

namespace lenia {

/// A discovered organism: params + snapshot + behavioral metrics.
struct Organism {
    Params params;
    NDArray cells;                 // final cell state (cropped)
    double mass = 0;
    double gyradius = 0;
    double speed = 0;
    double lyapunov = 0;
    int symmetry = 0;
    int lifespan = 0;              // steps survived before dying or cap
    bool is_moving = false;        // speed > threshold
    bool is_oscillating = false;   // mass oscillates periodically
    bool is_stable = false;        // survived the full eval window
    std::string category;          // "still", "oscillator", "glider", "chaotic", "dead"
    double novelty_score = 0;      // distance from known organisms

    std::string to_json() const;
};

/// Discovery configuration.
struct DiscoveryConfig {
    int board_size = 128;
    int eval_steps = 500;          // steps to evaluate each candidate
    int warmup_steps = 50;         // ignore first N steps
    int max_candidates = 10000;    // total candidates to try
    int R_min = 8;
    int R_max = 30;
    double m_min = 0.05;
    double m_max = 0.40;
    double s_min = 0.002;
    double s_max = 0.08;
    int max_b_rings = 4;
    double speed_threshold = 0.001;// threshold to classify as "moving"
    double mass_var_threshold = 5; // mass variance threshold for oscillation
    std::string output_path = "discovered.json";
    std::string animals_path;      // optional: load known creatures for novelty
};

/// Discovery engine: searches parameter space for stable Lenia organisms.
/// Runs many random trials, evaluates stability, classifies behavior,
/// and saves survivors to a JSON database.
class DiscoveryEngine {
public:
    explicit DiscoveryEngine(const DiscoveryConfig& config);

    /// Run the full discovery process. Returns number of organisms found.
    int run();

    /// Run a single trial with given params. Returns organism if stable.
    std::optional<Organism> evaluate_candidate(const Params& params, unsigned seed);

    /// Classify an organism based on its behavioral metrics.
    static std::string classify(const Organism& org);

    /// Compute novelty score relative to existing organisms.
    double novelty(const Organism& org) const;

    /// Access discovered organisms.
    const std::vector<Organism>& found() const { return found_; }

    /// Progress callback
    std::function<void(int tried, int found, const std::string& status)> on_progress;

private:
    DiscoveryConfig config_;
    std::mt19937 rng_;
    std::vector<Organism> found_;
    std::vector<Organism> known_;  // from animals.json for novelty comparison

    Params random_params();
    void save_results() const;
    void load_known();

    /// Feature vector for novelty comparison
    static std::vector<double> feature_vector(const Organism& org);
    static double feature_distance(const std::vector<double>& a, const std::vector<double>& b);
};

} // namespace lenia
