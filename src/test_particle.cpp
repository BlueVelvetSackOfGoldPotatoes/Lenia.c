/**
 * Test: Particle Lenia simulation.
 */
#include "lenia_particle.h"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <algorithm>

using namespace lenia;

int main() {
    std::cout << "=== Test: Particle Lenia ===" << std::endl;

    ParticleConfig config;
    config.num_particles = 100;
    config.world_size = 50.0;
    config.R = 8.0;
    config.dt = 0.1;
    config.m = 0.15;
    config.s = 0.02;
    config.mu = 0.5;
    config.friction = 0.8;
    config.enable_spawn = true;
    config.spawn_threshold = 0.95;

    ParticleLenia sim(config);

    std::cout << "  Initial: " << sim.num_particles() << " particles, "
              << "energy=" << std::fixed << std::setprecision(1)
              << sim.total_energy() << std::endl;

    for (int step = 0; step < 500; ++step) {
        sim.step();
        if (step % 100 == 0 || step == 499) {
            std::cout << "  gen=" << std::setw(3) << sim.gen()
                      << " particles=" << std::setw(4) << sim.num_particles()
                      << " energy=" << std::setw(8) << std::setprecision(1) << sim.total_energy()
                      << " mean_e=" << std::setprecision(3) << sim.mean_energy()
                      << std::endl;
        }
    }

    // Verify no NaN in particle positions
    for (const auto& p : sim.particles()) {
        assert(!std::isnan(p.x) && !std::isnan(p.y) && "NaN position");
        assert(!std::isnan(p.energy) && "NaN energy");
        assert(p.energy >= 0 && p.energy <= 1 && "Energy out of range");
        assert(p.x >= 0 && p.x < config.world_size && "X out of bounds");
        assert(p.y >= 0 && p.y < config.world_size && "Y out of bounds");
    }

    // Test rasterization
    std::vector<double> grid;
    sim.rasterize(grid, 64);
    assert(grid.size() == 64 * 64);
    double grid_max = *std::max_element(grid.begin(), grid.end());
    std::cout << "  Rasterized grid max: " << grid_max << std::endl;

    // Test JSON output
    std::string json = sim.to_json();
    assert(json.size() > 10);
    assert(json.find("\"gen\"") != std::string::npos);
    std::cout << "  JSON output: " << json.size() << " bytes" << std::endl;

    std::cout << "  === PARTICLE LENIA TESTS PASSED ===" << std::endl;
    return 0;
}
