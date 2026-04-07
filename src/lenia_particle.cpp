#include "lenia_particle.h"
#include <algorithm>
#include <cmath>

namespace lenia {

static constexpr double PI = 3.14159265358979323846;
static constexpr double PART_EPSILON = 1e-10;

ParticleLenia::ParticleLenia(const ParticleConfig& config) : config_(config), rng_(42) {
    reset();
}

void ParticleLenia::reset(unsigned seed) {
    rng_.seed(seed);
    gen_ = 0;
    particles_.clear();
    particles_.reserve(config_.num_particles);

    std::uniform_real_distribution<double> pos_dist(
        config_.world_size * 0.3, config_.world_size * 0.7);
    std::uniform_real_distribution<double> energy_dist(0.5, 1.0);

    for (int i = 0; i < config_.num_particles; ++i) {
        Particle p;
        p.x = pos_dist(rng_);
        p.y = pos_dist(rng_);
        p.energy = energy_dist(rng_);
        p.vx = 0;
        p.vy = 0;
        particles_.push_back(p);
    }
}

double ParticleLenia::wrap_dist(double dx) const {
    if (!config_.wrap) return dx;
    double half = config_.world_size / 2;
    if (dx > half) dx -= config_.world_size;
    if (dx < -half) dx += config_.world_size;
    return dx;
}

double ParticleLenia::wrap_coord(double x) const {
    if (!config_.wrap) return std::clamp(x, 0.0, config_.world_size);
    x = std::fmod(x, config_.world_size);
    if (x < 0) x += config_.world_size;
    return x;
}

double ParticleLenia::kernel_value(double r) const {
    if (r <= 0 || r >= 1.0) return 0;
    switch (config_.kn) {
        case 0: { double t = 4*r*(1-r); return t*t*t*t; }           // poly
        case 1: return std::exp(4.0 - 1.0 / (r * (1.0 - r)));       // bump
        case 2: return (r >= 0.25 && r <= 0.75) ? 1.0 : 0.0;        // step
        default: return std::exp(4.0 - 1.0 / (r * (1.0 - r)));
    }
}

double ParticleLenia::growth_value(double u) const {
    switch (config_.gn) {
        case 0: { double d = (u - config_.m) / (3*config_.s); double t = 1 - d*d;
                  return t > 0 ? std::pow(t, 4) * 2 - 1 : -1; }
        case 1: { double d = u - config_.m;
                  return std::exp(-(d*d)/(2*config_.s*config_.s)) * 2 - 1; }
        case 2: return std::abs(u - config_.m) <= config_.s ? 1.0 : -1.0;
        default: { double d = u - config_.m;
                   return std::exp(-(d*d)/(2*config_.s*config_.s)) * 2 - 1; }
    }
}

void ParticleLenia::step() {
    int N = static_cast<int>(particles_.size());
    if (N == 0) return;

    double R = config_.R;
    double R2 = R * R;
    double dt = config_.dt;
    double mu = config_.mu;

    // Compute growth and gradient for each particle
    std::vector<double> growth(N, 0.0);
    std::vector<double> grad_x(N, 0.0);
    std::vector<double> grad_y(N, 0.0);

    // O(N^2) pairwise interaction (fine for N < 2000)
    for (int i = 0; i < N; ++i) {
        double U = 0;          // neighborhood density
        double dU_dx = 0;      // gradient of density
        double dU_dy = 0;

        for (int j = 0; j < N; ++j) {
            if (i == j) continue;

            double dx = wrap_dist(particles_[j].x - particles_[i].x);
            double dy = wrap_dist(particles_[j].y - particles_[i].y);
            double d2 = dx * dx + dy * dy;
            if (d2 >= R2 || d2 < PART_EPSILON) continue;

            double d = std::sqrt(d2);
            double r = d / R;  // normalized distance
            double kv = kernel_value(r) * particles_[j].energy;
            U += kv;

            // Kernel gradient (chain rule: dK/dx = dK/dr * dr/dx)
            // dr/dx = dx / (d * R)
            // dK/dr approximated by finite difference
            double r_eps = 0.01;
            double dk = (kernel_value(std::min(r + r_eps, 0.999)) - kernel_value(std::max(r - r_eps, 0.001))) / (2 * r_eps);
            double dk_dx = dk * dx / (d * R) * particles_[j].energy;
            double dk_dy = dk * dy / (d * R) * particles_[j].energy;
            dU_dx += dk_dx;
            dU_dy += dk_dy;
        }

        // Normalize U by expected number of neighbors (area of disk)
        double norm = PI * R2 / (config_.world_size * config_.world_size) * N;
        if (norm > PART_EPSILON) {
            U /= norm;
            dU_dx /= norm;
            dU_dy /= norm;
        }

        growth[i] = growth_value(U);

        // Growth gradient: dG/dx = dG/dU * dU/dx
        // dG/dU for gaussian: dG/dU = -(U-m)/s^2 * exp(-(U-m)^2 / (2s^2)) * 2
        double dG_dU = 0;
        if (config_.gn == 1) {
            double d = U - config_.m;
            dG_dU = -(d / (config_.s * config_.s)) *
                     std::exp(-(d * d) / (2 * config_.s * config_.s)) * 2;
        }
        grad_x[i] = dG_dU * dU_dx;
        grad_y[i] = dG_dU * dU_dy;
    }

    // Update particles
    std::vector<Particle> new_particles;
    new_particles.reserve(N);

    for (int i = 0; i < N; ++i) {
        Particle p = particles_[i];

        // Update energy
        p.energy = std::clamp(p.energy + dt * growth[i], 0.0, 1.0);

        // Update velocity (gradient ascent + friction)
        p.vx = config_.friction * p.vx + mu * grad_x[i];
        p.vy = config_.friction * p.vy + mu * grad_y[i];

        // Update position
        p.x = wrap_coord(p.x + dt * p.vx);
        p.y = wrap_coord(p.y + dt * p.vy);

        // Kill dead particles
        if (p.energy < config_.death_threshold) continue;

        new_particles.push_back(p);

        // Spawn: if energy high enough and under max, split
        if (config_.enable_spawn && p.energy > config_.spawn_threshold &&
            static_cast<int>(new_particles.size()) < config_.max_particles) {
            Particle child;
            std::uniform_real_distribution<double> jitter(-1.0, 1.0);
            child.x = wrap_coord(p.x + jitter(rng_));
            child.y = wrap_coord(p.y + jitter(rng_));
            child.energy = p.energy * 0.5;
            child.vx = p.vx * 0.5;
            child.vy = p.vy * 0.5;
            new_particles.back().energy *= 0.5;  // parent loses energy too
            new_particles.push_back(child);
        }
    }

    particles_ = std::move(new_particles);
    gen_++;
}

void ParticleLenia::step_n(int n) {
    for (int i = 0; i < n; ++i) step();
}

double ParticleLenia::total_energy() const {
    double sum = 0;
    for (const auto& p : particles_) sum += p.energy;
    return sum;
}

double ParticleLenia::mean_energy() const {
    return particles_.empty() ? 0 : total_energy() / particles_.size();
}

void ParticleLenia::rasterize(std::vector<double>& grid, int grid_size) const {
    grid.assign(grid_size * grid_size, 0.0);
    double scale = grid_size / config_.world_size;
    for (const auto& p : particles_) {
        int gx = std::clamp(static_cast<int>(p.x * scale), 0, grid_size - 1);
        int gy = std::clamp(static_cast<int>(p.y * scale), 0, grid_size - 1);
        grid[gy * grid_size + gx] = std::max(grid[gy * grid_size + gx], p.energy);
    }
}

std::string ParticleLenia::to_json() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{\"gen\":" << gen_
        << ",\"num_particles\":" << particles_.size()
        << ",\"total_energy\":" << std::setprecision(1) << total_energy()
        << ",\"mean_energy\":" << std::setprecision(3) << mean_energy()
        << ",\"particles\":[";
    for (size_t i = 0; i < particles_.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "[" << std::setprecision(1) << particles_[i].x
            << "," << particles_[i].y
            << "," << std::setprecision(3) << particles_[i].energy << "]";
    }
    oss << "]}";
    return oss.str();
}

} // namespace lenia
