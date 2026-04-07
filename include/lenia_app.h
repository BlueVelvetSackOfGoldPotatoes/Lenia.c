#pragma once

#include "lenia_board.h"
#include "lenia_automaton.h"
#include "lenia_analyzer.h"
#include "lenia_recorder.h"
#include "lenia_cppn.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <random>
#include <functional>
#include <fstream>

namespace lenia {

/// Animal data entry from animals.json
struct AnimalEntry {
    std::string code;
    std::string name;
    std::string cname;
    Params params;
    NDArray cells;
};

/// Colormap: 256-entry RGB lookup table
struct Colormap {
    struct RGB { uint8_t r, g, b; };
    RGB entries[256];
};

/// Transform state (matching Python tx dict)
struct Transform {
    std::vector<int> shift;     // per-dimension shift
    std::array<double, 3> rotate = {0, 0, 0};
    int R = 13;
    int flip = -1;
};

/// Lenia application: headless simulation engine with animal library,
/// world operations, search/breeding, and image rendering.
/// Faithful port of the non-GUI parts of Python Lenia class from LeniaND.py.
class LeniaApp {
public:
    /// Configuration
    struct Config {
        int dim = 2;
        std::vector<int> size = {256, 256};  // reversed order: [Y, X] for 2D
        int pixel = 4;
        int pixel_border = 0;
        std::string animals_path = "animals.json";
        std::string found_animals_path = "found.json";
        std::string save_root = "save";
    };

    explicit LeniaApp(const Config& config);

    // ---- Animal library ----
    bool read_animals();
    bool read_found_animals();
    void load_animal_id(int id);
    void load_found_animal_id(int id);
    void load_animal_code(const std::string& code);
    int get_animal_id(const std::string& code) const;
    int search_animal_id(const std::string& prefix, int old_id, int dir) const;
    void search_animal(const std::string& prefix, int dir);
    const std::vector<AnimalEntry>& animals() const { return animal_data_; }

    // ---- World operations ----
    void load_part(const Board& part, bool replace = true, bool use_part_R = false,
                   bool random_pos = false);
    void clear_world();
    void random_world(bool reseed = false, bool fill = false);
    void random_world_and_params(bool reseed = false, bool fill = false);
    void cppn_world(bool reseed = false);
    void transform_world();
    void world_updated();

    // ---- Search/breeding ----
    void toggle_search(int dir, bool small = false);
    void stop_search();
    bool do_search();

    // ---- Simulation ----
    void step();
    void step_n(int n);
    bool is_running() const { return is_run_; }
    void set_running(bool run) { is_run_ = run; }

    // ---- Rendering (to image buffer) ----
    void render_rgb(std::vector<uint8_t>& rgb_out, int& width, int& height,
                    int show_what = 0) const;
    void set_colormap(int id);
    Colormap create_colormap(const std::vector<std::array<int, 3>>& colors,
                             bool marker_white = true) const;

    // ---- Save/Load ----
    void save_world(bool sequential = false);
    void copy_world_json(std::string& out) const;
    void paste_world_json(const std::string& json_str);

    // ---- Recording ----
    void toggle_recording(bool save_frames = false);

    // ---- Accessors ----
    Board& world() { return world_; }
    const Board& world() const { return world_; }
    Automaton& automaton() { return *automaton_; }
    Analyzer& analyzer() { return *analyzer_; }
    Recorder& recorder() { return *recorder_; }
    const Config& config() const { return config_; }
    Transform& tx() { return tx_; }
    int animal_id() const { return animal_id_; }

    // ---- Status ----
    std::vector<std::string> status_messages;

private:
    Config config_;
    Board world_;
    std::unique_ptr<Automaton> automaton_;
    std::unique_ptr<Analyzer> analyzer_;
    std::unique_ptr<Recorder> recorder_;
    std::unique_ptr<CPPN> cppn_;

    std::vector<AnimalEntry> animal_data_;
    std::vector<AnimalEntry> found_animal_data_;
    int animal_id_ = 0;
    int found_animal_id_ = 0;

    Transform tx_;
    bool is_run_ = false;
    int run_counter_ = -1;
    bool is_auto_center_ = false;

    int search_mode_ = 0;  // 0=off, +1=breed up, -1=breed down
    bool is_search_small_ = false;
    int search_total_ = 0;
    int search_success_ = 0;

    std::mt19937 rng_;
    int last_seed_ = 0;

    std::vector<Colormap> colormaps_;
    int colormap_id_ = 0;

    // Internal helpers
    void clear_transform();
    std::vector<AnimalEntry> parse_animals_json(const std::string& json_str) const;
    double normalize(double v, double vmin, double vmax) const;
};

} // namespace lenia
