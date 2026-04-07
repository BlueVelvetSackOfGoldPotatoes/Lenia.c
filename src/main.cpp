#include "lenia_app.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstdlib>
#include <fstream>

void save_ppm(const std::string& path, const std::vector<uint8_t>& rgb, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
}

int main(int argc, char* argv[]) {
    int steps = 1000;
    int size = 256;
    int pixel = 2;
    bool save_frames = false;
    std::string animals_path;
    std::string code;
    int colormap = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--steps" && i + 1 < argc) steps = std::atoi(argv[++i]);
        else if (arg == "--size" && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (arg == "--pixel" && i + 1 < argc) pixel = std::atoi(argv[++i]);
        else if (arg == "--frames") save_frames = true;
        else if (arg == "--animals" && i + 1 < argc) animals_path = argv[++i];
        else if (arg == "--code" && i + 1 < argc) code = argv[++i];
        else if (arg == "--colormap" && i + 1 < argc) colormap = std::atoi(argv[++i]);
        else if (arg == "--random-search") {
            // Special mode: search for stable organisms
            lenia::LeniaApp::Config cfg;
            cfg.size = {size, size};
            cfg.pixel = pixel;
            if (!animals_path.empty()) cfg.animals_path = animals_path;
            lenia::LeniaApp app(cfg);
            app.toggle_search(+1);
            int found = 0;
            for (int s = 0; s < steps; ++s) {
                app.step();
                if (s % 100 == 0) {
                    std::cout << "step=" << s << " mass=" << std::fixed
                              << std::setprecision(1) << app.analyzer().mass() << std::endl;
                }
            }
            return 0;
        }
        else if (arg == "--help") {
            std::cout << "Lenia C++ — full port of Chakazul/Lenia\n\n"
                      << "Usage: lenia [options]\n"
                      << "  --steps N       Simulation steps (default: 1000)\n"
                      << "  --size N        Board size NxN, power of 2 (default: 256)\n"
                      << "  --pixel N       Pixel magnification (default: 2)\n"
                      << "  --frames        Save PPM frames to record/\n"
                      << "  --animals PATH  Load animals.json\n"
                      << "  --code CODE     Load specific pattern by code\n"
                      << "  --colormap N    Colormap index 0-8 (default: 0)\n"
                      << "  --random-search Run random parameter search\n"
                      << std::endl;
            return 0;
        }
    }

    lenia::LeniaApp::Config cfg;
    cfg.size = {size, size};
    cfg.pixel = pixel;
    if (!animals_path.empty()) cfg.animals_path = animals_path;

    lenia::LeniaApp app(cfg);
    app.set_colormap(colormap);

    if (!code.empty()) {
        if (animals_path.empty()) {
            std::cerr << "Error: --code requires --animals path" << std::endl;
            return 1;
        }
        int id = app.get_animal_id(code);
        if (id < 0) {
            std::cerr << "Pattern '" << code << "' not found. Available:" << std::endl;
            for (const auto& e : app.animals()) {
                std::cout << "  " << e.code << " - " << e.name << std::endl;
            }
            return 1;
        }
        app.load_animal_id(id);
        std::cout << "Loaded '" << app.animals()[id].name << "' (R="
                  << app.world().params.R << " T=" << app.world().params.T
                  << " m=" << app.world().params.m << " s=" << app.world().params.s << ")"
                  << std::endl;
    } else {
        // Default: Orbium parameters with random init
        app.world().params.R = 13;
        app.world().params.T = 10;
        app.world().params.b = {1.0};
        app.world().params.m = 0.15;
        app.world().params.s = 0.015;
        app.world().params.kn = 1;
        app.world().params.gn = 1;
        app.random_world();
        std::cout << "Lenia C++ — " << size << "x" << size
                  << ", " << steps << " steps, colormap " << colormap << std::endl;
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < steps; ++i) {
        app.step();

        if (i % 100 == 0 || i == steps - 1) {
            std::cout << "gen=" << std::setw(5) << app.automaton().gen()
                      << "  mass=" << std::fixed << std::setprecision(1) << app.analyzer().mass()
                      << "  gyrad=" << std::setprecision(2) << app.analyzer().mass()
                      << std::endl;
        }

        if (save_frames && (i % 10 == 0)) {
            std::vector<uint8_t> rgb;
            int w, h;
            app.render_rgb(rgb, w, h);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "record/frame_%05d.ppm", i);
            std::filesystem::create_directories("record");
            save_ppm(buf, rgb, w, h);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "\n" << steps << " steps in " << std::fixed << std::setprecision(2)
              << elapsed << "s (" << std::setprecision(0) << steps / elapsed << " steps/s)"
              << std::endl;

    // Save final frame
    std::vector<uint8_t> rgb;
    int w, h;
    app.render_rgb(rgb, w, h);
    save_ppm("lenia_final.ppm", rgb, w, h);
    std::cout << "Saved lenia_final.ppm (" << w << "x" << h << ")" << std::endl;

    return 0;
}
