#pragma once

#include <vector>
#include <cmath>
#include <random>
#include <string>
#include <sstream>
#include <iomanip>

namespace lenia {

/// Particle Lenia: continuous-space cellular automaton where each "cell" is a
/// particle that moves through space. Instead of updating a grid, particles
/// sense their neighbors within a radius, compute growth fields, and move
/// toward regions of positive growth.
///
/// Based on: "Particle Lenia and the energy of approximate self-replication"
/// (Mordvintsev et al. 2023 / Bert Chan)
///
/// Each particle has:
///   - position (x, y) in continuous 2D space
///   - energy e in [0, 1]
///
/// On each step:
///   1. For each particle, compute the neighborhood density field U
///      (sum of kernel contributions from neighbors within radius R)
///   2. Apply growth function G(U, m, s) to get the growth rate
///   3. Update energy: e_new = clip(e + dt * G, 0, 1)
///   4. Compute energy gradient and move particle toward higher energy
///   5. Remove dead particles (e < epsilon), optionally spawn new ones

struct Particle {
    double x = 0, y = 0;
    double energy = 1.0;
    double vx = 0, vy = 0;  // velocity for momentum
};

struct ParticleConfig {
    int num_particles = 200;
    double world_size = 100.0;   // continuous space [0, world_size)
    double R = 10.0;             // interaction radius
    double dt = 0.1;             // time step
    double m = 0.15;             // growth function mean
    double s = 0.015;            // growth function stdev
    int kn = 1;                  // kernel type (1=bump)
    int gn = 1;                  // growth type (1=gaussian)
    double mu = 0.1;             // movement strength (gradient following)
    double friction = 0.9;       // velocity damping
    double spawn_threshold = 0.9;// energy above which particle can split
    double death_threshold = 0.01;
    bool wrap = true;            // toroidal boundary
    bool enable_spawn = false;
    int max_particles = 1000;
};

class ParticleLenia {
public:
    explicit ParticleLenia(const ParticleConfig& config);

    void step();
    void step_n(int n);
    void reset(unsigned seed = 42);

    // Accessors
    int gen() const { return gen_; }
    int num_particles() const { return static_cast<int>(particles_.size()); }
    const std::vector<Particle>& particles() const { return particles_; }
    const ParticleConfig& config() const { return config_; }
    double total_energy() const;
    double mean_energy() const;

    // Rasterize to a grid for visualization
    void rasterize(std::vector<double>& grid, int grid_size) const;

    // JSON output for server mode
    std::string to_json() const;

private:
    ParticleConfig config_;
    std::vector<Particle> particles_;
    std::mt19937 rng_;
    int gen_ = 0;

    double kernel_value(double r) const;
    double growth_value(double u) const;
    double wrap_dist(double dx) const;
    double wrap_coord(double x) const;
};

} // namespace lenia
