#include "lenia_discovery.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <optional>

namespace lenia {

// ============================================================================
// Organism
// ============================================================================

std::string Organism::to_json() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "{\"params\":{\"R\":" << params.R << ",\"T\":" << params.T
        << ",\"m\":" << params.m << ",\"s\":" << params.s
        << ",\"kn\":" << params.kn << ",\"gn\":" << params.gn << ",\"b\":[";
    for (size_t i = 0; i < params.b.size(); ++i) {
        if (i > 0) oss << ",";
        oss << params.b[i];
    }
    oss << "]},\"mass\":" << mass << ",\"gyradius\":" << gyradius
        << ",\"speed\":" << speed << ",\"lyapunov\":" << lyapunov
        << ",\"symmetry\":" << symmetry << ",\"lifespan\":" << lifespan
        << ",\"is_moving\":" << (is_moving ? "true" : "false")
        << ",\"is_oscillating\":" << (is_oscillating ? "true" : "false")
        << ",\"is_stable\":" << (is_stable ? "true" : "false")
        << ",\"category\":\"" << category << "\""
        << ",\"novelty\":" << novelty_score;
    if (cells.size() > 0) {
        oss << ",\"cells\":\"" << RLECodec::encode(cells) << "\"";
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// DiscoveryEngine
// ============================================================================

DiscoveryEngine::DiscoveryEngine(const DiscoveryConfig& config)
    : config_(config), rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
    load_known();
}

void DiscoveryEngine::load_known() {
    if (config_.animals_path.empty()) return;
    // Load animals.json and extract feature vectors for novelty comparison
    LeniaApp::Config app_cfg;
    app_cfg.size = {config_.board_size, config_.board_size};
    app_cfg.animals_path = config_.animals_path;
    LeniaApp app(app_cfg);
    // We don't actually simulate these — just note their existence
    // Novelty will be computed against discovered organisms
}

Params DiscoveryEngine::random_params() {
    Params p;
    std::uniform_int_distribution<int> R_dist(config_.R_min, config_.R_max);
    std::uniform_real_distribution<double> m_dist(config_.m_min, config_.m_max);
    std::uniform_int_distribution<int> b_count(1, config_.max_b_rings);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<int> T_dist(5, 20);

    p.R = R_dist(rng_);
    p.T = T_dist(rng_);
    p.m = m_dist(rng_);
    p.s = p.m / (5.0 + unit(rng_) * 15.0);
    p.kn = 1;  // exponential bump (most interesting)
    p.gn = 1;  // gaussian growth

    int B = b_count(rng_);
    p.b.clear();
    for (int i = 0; i < B; ++i) p.b.push_back(unit(rng_));
    p.b[std::uniform_int_distribution<int>(0, B - 1)(rng_)] = 1.0;  // at least one = 1

    return p;
}

std::optional<Organism> DiscoveryEngine::evaluate_candidate(const Params& params, unsigned seed) {
    Board board({config_.board_size, config_.board_size});
    board.params = params;

    // Random initialization in center quarter
    std::mt19937 local_rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    int q = config_.board_size / 4;
    for (int r = q; r < 3 * q; ++r) {
        for (int c = q; c < 3 * q; ++c) {
            if (dist(local_rng) < 0.4) {
                board.cells.at2(r, c) = dist(local_rng);
            }
        }
    }

    Automaton automaton(board);
    Analyzer analyzer(automaton, board);

    // Track mass history for stability analysis
    std::vector<double> mass_history;
    std::vector<double> speed_history;
    mass_history.reserve(config_.eval_steps);

    for (int step = 0; step < config_.eval_steps; ++step) {
        automaton.calc_once(true);
        analyzer.calc_stats();

        double mass = analyzer.mass();
        mass_history.push_back(mass);
        speed_history.push_back(analyzer.speed());

        // Early termination: organism died
        if (mass < EPSILON && step > config_.warmup_steps) {
            return std::nullopt;
        }

        // Early termination: organism filled the world
        if (analyzer.is_full() && step > config_.warmup_steps) {
            return std::nullopt;
        }
    }

    // Check stability: mass should be bounded and non-zero in the second half
    int half = config_.eval_steps / 2;
    double late_mass_min = *std::min_element(mass_history.begin() + half, mass_history.end());
    double late_mass_max = *std::max_element(mass_history.begin() + half, mass_history.end());
    double late_mass_mean = std::accumulate(mass_history.begin() + half, mass_history.end(), 0.0) / (config_.eval_steps - half);

    if (late_mass_mean < 1.0) return std::nullopt;  // died
    if (late_mass_max > config_.board_size * config_.board_size * 0.5) return std::nullopt;  // exploded

    // Compute behavioral metrics
    Organism org;
    org.params = params;
    org.mass = late_mass_mean;
    org.gyradius = analyzer.gyradius();
    org.lifespan = config_.eval_steps;
    org.is_stable = true;

    // Average speed in second half
    double speed_sum = 0;
    for (int i = half; i < config_.eval_steps; ++i) speed_sum += speed_history[i];
    org.speed = speed_sum / (config_.eval_steps - half);
    org.is_moving = org.speed > config_.speed_threshold;

    // Mass variance for oscillation detection
    double var = 0;
    for (int i = half; i < config_.eval_steps; ++i) {
        double d = mass_history[i] - late_mass_mean;
        var += d * d;
    }
    var /= (config_.eval_steps - half);
    org.is_oscillating = var > config_.mass_var_threshold;

    org.lyapunov = analyzer.lyapunov();
    org.symmetry = analyzer.symmetry();

    // Classify
    org.category = classify(org);

    // Crop and save cell state
    Board cropped = board;
    cropped.crop();
    org.cells = cropped.cells.copy();

    // Novelty
    org.novelty_score = novelty(org);

    return org;
}

std::string DiscoveryEngine::classify(const Organism& org) {
    if (!org.is_stable) return "dead";
    if (org.is_moving && org.is_oscillating) return "glider";
    if (org.is_moving) return "glider";
    if (org.is_oscillating) return "oscillator";
    if (org.mass < 5.0) return "minimal";
    return "still";
}

std::vector<double> DiscoveryEngine::feature_vector(const Organism& org) {
    return {
        org.mass / 100.0,
        org.gyradius / 10.0,
        org.speed * 100.0,
        org.lyapunov,
        static_cast<double>(org.symmetry) / 10.0,
        org.is_moving ? 1.0 : 0.0,
        org.is_oscillating ? 1.0 : 0.0,
        static_cast<double>(org.params.R) / 30.0,
        org.params.m * 10.0,
        org.params.s * 100.0,
    };
}

double DiscoveryEngine::feature_distance(const std::vector<double>& a, const std::vector<double>& b) {
    double sum = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        double d = a[i] - b[i];
        sum += d * d;
    }
    return std::sqrt(sum);
}

double DiscoveryEngine::novelty(const Organism& org) const {
    if (found_.empty()) return 1.0;  // first organism is maximally novel

    auto fv = feature_vector(org);
    // Average distance to k nearest neighbors
    std::vector<double> distances;
    distances.reserve(found_.size());
    for (const auto& existing : found_) {
        distances.push_back(feature_distance(fv, feature_vector(existing)));
    }
    std::sort(distances.begin(), distances.end());

    int k = std::min(5, static_cast<int>(distances.size()));
    double sum = 0;
    for (int i = 0; i < k; ++i) sum += distances[i];
    return sum / k;
}

void DiscoveryEngine::save_results() const {
    std::ofstream file(config_.output_path);
    file << "[\n";
    for (size_t i = 0; i < found_.size(); ++i) {
        if (i > 0) file << ",\n";
        file << "  " << found_[i].to_json();
    }
    file << "\n]\n";
}

int DiscoveryEngine::run() {
    std::cout << "Discovery: searching " << config_.max_candidates << " candidates on "
              << config_.board_size << "x" << config_.board_size << " grid, "
              << config_.eval_steps << " steps each" << std::endl;

    int tried = 0;
    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < config_.max_candidates; ++i) {
        Params params = random_params();
        unsigned seed = rng_();

        auto result = evaluate_candidate(params, seed);
        tried++;

        if (result.has_value()) {
            found_.push_back(std::move(result.value()));
            const auto& org = found_.back();

            std::cout << "  FOUND #" << found_.size() << ": " << org.category
                      << " mass=" << std::fixed << std::setprecision(1) << org.mass
                      << " speed=" << std::setprecision(4) << org.speed
                      << " sym=" << org.symmetry
                      << " novelty=" << std::setprecision(3) << org.novelty_score
                      << " R=" << org.params.R << " m=" << std::setprecision(3) << org.params.m
                      << " s=" << std::setprecision(4) << org.params.s
                      << std::endl;

            // Save incrementally every 10 discoveries
            if (found_.size() % 10 == 0) {
                save_results();
            }
        }

        if (tried % 100 == 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start_time).count();
            double rate = tried / elapsed;
            std::cout << "  [" << tried << "/" << config_.max_candidates << "] "
                      << found_.size() << " found (" << std::fixed << std::setprecision(1)
                      << (100.0 * found_.size() / tried) << "%) "
                      << rate << " candidates/s" << std::endl;

            if (on_progress) {
                on_progress(tried, static_cast<int>(found_.size()),
                           std::to_string(found_.size()) + " found in " + std::to_string(tried));
            }
        }
    }

    save_results();

    auto end_time = std::chrono::steady_clock::now();
    double total_elapsed = std::chrono::duration<double>(end_time - start_time).count();

    std::cout << "\nDiscovery complete: " << found_.size() << " organisms found in "
              << tried << " trials (" << std::fixed << std::setprecision(1)
              << (100.0 * found_.size() / tried) << "%) in "
              << std::setprecision(1) << total_elapsed << "s" << std::endl;

    // Print taxonomy summary
    std::map<std::string, int> taxonomy;
    for (const auto& org : found_) taxonomy[org.category]++;
    std::cout << "Taxonomy:" << std::endl;
    for (const auto& [cat, count] : taxonomy) {
        std::cout << "  " << cat << ": " << count << std::endl;
    }

    std::cout << "Results saved to " << config_.output_path << std::endl;
    return static_cast<int>(found_.size());
}

} // namespace lenia
