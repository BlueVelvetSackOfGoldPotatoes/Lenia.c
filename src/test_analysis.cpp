/**
 * Test: Advanced analysis suite.
 * Runs Orbium for 200 steps, then computes all analysis metrics.
 */
#include "lenia_board.h"
#include "lenia_automaton.h"
#include "lenia_analyzer.h"
#include "lenia_analysis.h"
#include "lenia_app.h"
#include <iostream>
#include <iomanip>
#include <cassert>

using namespace lenia;

int main() {
    std::cout << "=== Test: Analysis Suite on Orbium ===" << std::endl;

    // Set up Orbium
    LeniaApp::Config cfg;
    cfg.size = {128, 128};
    cfg.animals_path = "/home/pessegueirolunar/Documents/biotools/external_repos/Lenia/Python/animals.json";
    LeniaApp app(cfg);

    int id = app.get_animal_id("O2u");
    if (id < 0) {
        std::cerr << "O2u not found in animals.json" << std::endl;
        return 1;
    }
    app.load_animal_id(id);

    // Run for 200 steps, collecting time series
    std::vector<double> mass_h, speed_h, gyrad_h;
    NDArray prev_cells;

    for (int i = 0; i < 200; ++i) {
        if (i == 199) prev_cells = app.world().cells.copy();
        app.step();
        mass_h.push_back(app.analyzer().mass());
        speed_h.push_back(app.analyzer().speed());
        gyrad_h.push_back(app.analyzer().gyradius());
    }

    std::cout << "  Final mass: " << std::fixed << std::setprecision(1)
              << app.world().cells.sum() << std::endl;

    // Run full analysis
    auto report = analysis::full_analysis(
        app.world().cells, &prev_cells, mass_h, speed_h, gyrad_h, app.world().params.T);

    std::cout << "\n  --- Information Metrics ---" << std::endl;
    std::cout << "  Shannon entropy:      " << std::setprecision(3) << report.shannon << std::endl;
    std::cout << "  Spatial entropy:      " << report.spatial << std::endl;
    std::cout << "  Integration (Phi):    " << report.phi << std::endl;

    std::cout << "\n  --- Autopoiesis ---" << std::endl;
    std::cout << "  Boundary strength:    " << report.autopoiesis.boundary_strength << std::endl;
    std::cout << "  Interior smoothness:  " << report.autopoiesis.interior_smoothness << std::endl;
    std::cout << "  Autopoiesis score:    " << report.autopoiesis.score << std::endl;
    std::cout << "  Boundary cells:       " << report.autopoiesis.boundary_cells << std::endl;
    std::cout << "  Interior cells:       " << report.autopoiesis.interior_cells << std::endl;

    std::cout << "\n  --- Topology ---" << std::endl;
    std::cout << "  Connected components: " << report.components << std::endl;

    std::cout << "\n  --- Taxonomy ---" << std::endl;
    std::cout << "  Primary:   " << report.taxonomy.primary << std::endl;
    std::cout << "  Secondary: " << report.taxonomy.secondary << std::endl;
    std::cout << "  Period:    " << report.taxonomy.period << std::endl;
    std::cout << "  Speed:     " << report.taxonomy.drift_speed << std::endl;

    if (!report.psd_freq.empty()) {
        double peak_f = 0, peak_m = 0;
        for (size_t i = 0; i < report.psd_freq.size(); ++i) {
            if (report.psd_magnitude[i] > peak_m) {
                peak_m = report.psd_magnitude[i];
                peak_f = report.psd_freq[i];
            }
        }
        std::cout << "  PSD peak:  f=" << peak_f << " mag=" << peak_m << std::endl;
    }

    // Active information storage
    double ais = analysis::active_information_storage(prev_cells, app.world().cells);
    std::cout << "\n  Active Info Storage: " << ais << std::endl;

    // Reproduction detection
    auto repro = analysis::detect_reproduction(prev_cells, app.world().cells);
    std::cout << "  Components before: " << repro.components_before
              << " after: " << repro.components_after
              << " reproduced: " << (repro.reproduced ? "YES" : "no") << std::endl;

    // Sanity checks
    assert(report.shannon > 0 && "Shannon entropy should be positive for non-trivial state");
    assert(report.components >= 1 && "Orbium should be at least 1 connected component");
    assert(report.autopoiesis.score > 0 && "Living organism should have positive autopoiesis");

    std::cout << "\n  === ALL ANALYSIS TESTS PASSED ===" << std::endl;
    return 0;
}
