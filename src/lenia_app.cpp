#include "lenia_app.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace lenia {

namespace fs = std::filesystem;

// ============================================================================
// Construction
// ============================================================================

LeniaApp::LeniaApp(const Config& config) : config_(config), world_(config.size), rng_(42) {
    // Default params
    int min_size_2 = 0;
    for (int s : config.size) {
        int s2 = 0;
        while ((1 << s2) < s) s2++;
        if (min_size_2 == 0 || s2 < min_size_2) min_size_2 = s2;
    }
    int def_R = static_cast<int>(std::pow(2.0, min_size_2 - 6) * config.dim * 5);
    world_.params.R = std::max(5, def_R);

    automaton_ = std::make_unique<Automaton>(world_);
    analyzer_ = std::make_unique<Analyzer>(*automaton_, world_);
    int pw = config.size.back() * config.pixel;
    int ph = config.size[config.size.size() - 2] * config.pixel;
    recorder_ = std::make_unique<Recorder>(world_, pw, ph);

    // Build colormaps (matching Python: 9 colormaps)
    colormaps_.resize(9);
    colormaps_[0] = create_colormap({{0,0,4},{0,0,8},{0,4,8},{0,8,8},{4,8,4},{8,8,0},{8,4,0},{8,0,0},{4,0,0}});
    colormaps_[1] = create_colormap({{7,6,7},{5,4,5},{4,1,4},{1,3,6},{3,4,6},{4,5,7},{2,6,3},{5,6,4},{6,7,5},{8,8,3},{8,6,2},{8,5,1},{7,0,0}});
    colormaps_[2] = create_colormap({{0,2,0},{0,4,0},{4,6,0},{8,8,0},{8,4,4},{8,0,8},{4,0,8},{0,0,8},{0,0,4}});
    colormaps_[3] = create_colormap({{4,0,2},{8,0,4},{8,0,6},{8,0,8},{4,4,4},{0,8,0},{0,6,0},{0,4,0},{0,2,0}});
    colormaps_[4] = create_colormap({{4,4,6},{2,2,4},{2,4,2},{4,6,4},{6,6,4},{4,2,2}});
    colormaps_[5] = create_colormap({{4,6,4},{2,4,2},{4,4,2},{6,6,4},{6,4,6},{2,2,4}});
    colormaps_[6] = create_colormap({{6,6,4},{4,4,2},{4,2,4},{6,4,6},{4,6,6},{2,4,2}});
    colormaps_[7] = create_colormap({{8,8,8},{7,7,7},{5,5,5},{3,3,3},{0,0,0}}, false);
    colormaps_[8] = create_colormap({{0,0,0},{3,3,3},{5,5,5},{7,7,7},{8,8,8}});

    clear_transform();
    read_animals();
    read_found_animals();
}

void LeniaApp::clear_transform() {
    tx_.shift.assign(config_.dim, 0);
    tx_.rotate = {0, 0, 0};
    tx_.R = world_.params.R;
    tx_.flip = -1;
}

// ============================================================================
// Animal Library
// ============================================================================

std::vector<AnimalEntry> LeniaApp::parse_animals_json(const std::string& json_str) const {
    std::vector<AnimalEntry> entries;

    size_t pos = 0;
    while ((pos = json_str.find("\"code\"", pos)) != std::string::npos) {
        AnimalEntry entry;

        auto extract_string = [&](const std::string& key) -> std::string {
            size_t kp = json_str.find("\"" + key + "\"", pos);
            if (kp == std::string::npos || kp > pos + 5000) return "";
            size_t colon = json_str.find(':', kp);
            if (colon == std::string::npos) return "";
            size_t q1 = json_str.find('"', colon + 1);
            if (q1 == std::string::npos) return "";
            size_t q2 = json_str.find('"', q1 + 1);
            if (q2 == std::string::npos) return "";
            return json_str.substr(q1 + 1, q2 - q1 - 1);
        };

        auto extract_number = [&](const std::string& key) -> double {
            size_t kp = json_str.find("\"" + key + "\"", pos);
            if (kp == std::string::npos || kp > pos + 5000) return 0.0;
            size_t colon = json_str.find(':', kp);
            if (colon == std::string::npos) return 0.0;
            size_t start = json_str.find_first_of("-0123456789.", colon + 1);
            if (start == std::string::npos) return 0.0;
            size_t end = json_str.find_first_not_of("-0123456789.eE+", start);
            try { return std::stod(json_str.substr(start, end - start)); }
            catch (...) { return 0.0; }
        };

        entry.code = extract_string("code");
        entry.name = extract_string("name");
        entry.cname = extract_string("cname");
        entry.params.R = std::max(1, static_cast<int>(extract_number("R")));
        entry.params.T = std::max(1, static_cast<int>(extract_number("T")));
        entry.params.m = extract_number("m");
        entry.params.s = extract_number("s");
        entry.params.kn = std::max(1, static_cast<int>(extract_number("kn")));
        entry.params.gn = std::max(1, static_cast<int>(extract_number("gn")));

        std::string b_str = extract_string("b");
        if (!b_str.empty()) {
            entry.params.b = RLECodec::st2fracs(b_str);
        }

        std::string cells_str = extract_string("cells");
        if (!cells_str.empty()) {
            entry.cells = RLECodec::decode(cells_str, config_.dim);
        }

        if (!entry.code.empty()) {
            entries.push_back(std::move(entry));
        }
        pos += 50;
    }
    return entries;
}

bool LeniaApp::read_animals() {
    std::ifstream file(config_.animals_path);
    if (!file.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    animal_data_ = parse_animals_json(content);
    return !animal_data_.empty();
}

bool LeniaApp::read_found_animals() {
    std::ifstream file(config_.found_animals_path);
    if (!file.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // Found animals file is newline-delimited JSON objects, not a proper array
    if (!content.empty() && content.back() == ',') content.pop_back();
    if (!content.empty() && content.back() == '\n') content.pop_back();
    if (!content.empty() && content.back() == ',') content.pop_back();
    content = "[" + content + "]";
    found_animal_data_ = parse_animals_json(content);
    return !found_animal_data_.empty();
}

void LeniaApp::load_animal_id(int id) {
    if (animal_data_.empty()) return;
    animal_id_ = std::clamp(id, 0, static_cast<int>(animal_data_.size()) - 1);
    const auto& entry = animal_data_[animal_id_];
    Board part(entry.cells.shape());
    part.cells = entry.cells.copy();
    part.params = entry.params;
    part.names = {entry.code, entry.name, entry.cname};
    load_part(part);
}

void LeniaApp::load_found_animal_id(int id) {
    if (found_animal_data_.empty()) return;
    found_animal_id_ = std::clamp(id, 0, static_cast<int>(found_animal_data_.size()) - 1);
    const auto& entry = found_animal_data_[found_animal_id_];
    Board part(entry.cells.shape());
    part.cells = entry.cells.copy();
    part.params = entry.params;
    part.names = {"Found #" + std::to_string(found_animal_id_ + 1), "", ""};
    load_part(part, true, true);
}

void LeniaApp::load_animal_code(const std::string& code) {
    int id = get_animal_id(code);
    if (id >= 0) load_animal_id(id);
}

int LeniaApp::get_animal_id(const std::string& code) const {
    for (int i = 0; i < static_cast<int>(animal_data_.size()); ++i) {
        if (animal_data_[i].code == code) return i;
    }
    return -1;
}

int LeniaApp::search_animal_id(const std::string& prefix, int old_id, int dir) const {
    int id = old_id + dir;
    while (id >= 0 && id < static_cast<int>(animal_data_.size())) {
        if (animal_data_[id].name.substr(0, prefix.size()) == prefix) return id;
        id += dir;
    }
    return old_id;
}

void LeniaApp::search_animal(const std::string& prefix, int dir) {
    int id = animal_id_;
    id = search_animal_id(prefix, id, dir);
    if (dir == -1) id = search_animal_id(prefix, id, dir);
    while (id < static_cast<int>(animal_data_.size()) && animal_data_[id].code[0] == '>') id++;
    load_animal_id(id);
}

// ============================================================================
// World Operations
// ============================================================================

void LeniaApp::load_part(const Board& part, bool replace, bool use_part_R, bool random_pos) {
    if (part.cells.size() == 0) return;
    if (replace) {
        world_.names = part.names;
    }
    if (use_part_R || part.params.R != world_.params.R) {
        world_.params = part.params;
        automaton_->calc_kernel();
    } else {
        world_.params.T = part.params.T;
        world_.params.b = part.params.b;
        world_.params.m = part.params.m;
        world_.params.s = part.params.s;
        world_.params.kn = part.params.kn;
        world_.params.gn = part.params.gn;
    }

    if (replace) world_.clear();

    std::vector<int> shift(config_.dim, 0);
    if (random_pos) {
        for (int d = 0; d < config_.dim; ++d) {
            std::uniform_int_distribution<int> dist(0, config_.size[d] - 1);
            shift[d] = dist(rng_);
        }
    }
    world_.add(part, shift, true);
    world_updated();
}

void LeniaApp::clear_world() {
    world_.clear();
    world_updated();
}

void LeniaApp::random_world(bool reseed, bool fill) {
    if (reseed) {
        last_seed_ = std::uniform_int_distribution<int>(0, 999999)(rng_);
    }
    std::mt19937 local_rng(last_seed_);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    world_.clear();
    int total = world_.cells.size();
    double density = fill ? 0.9 : 0.5;

    // Fill center region (works for any dimensionality)
    int ndim = world_.cells.ndim();
    std::vector<int> lo(ndim), hi(ndim);
    for (int d = 0; d < ndim; ++d) {
        lo[d] = config_.size[d] / 4;
        hi[d] = 3 * config_.size[d] / 4;
    }

    // Iterate over all cells, fill those in center region
    for (int flat = 0; flat < total; ++flat) {
        int remainder = flat;
        bool in_center = true;
        for (int d = ndim - 1; d >= 0; --d) {
            int coord = remainder % config_.size[d];
            remainder /= config_.size[d];
            if (coord < lo[d] || coord >= hi[d]) { in_center = false; break; }
        }
        if (in_center && dist(local_rng) < density) {
            world_.cells[flat] = dist(local_rng);
        }
    }
    world_updated();
}

void LeniaApp::random_world_and_params(bool reseed, bool fill) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    std::uniform_int_distribution<int> B_dist(1, 4);

    int min_size_2 = 0;
    for (int s : config_.size) {
        int s2 = 0;
        while ((1 << s2) < s) s2++;
        if (min_size_2 == 0 || s2 < min_size_2) min_size_2 = s2;
    }
    int rand_r1 = static_cast<int>(std::pow(2.0, min_size_2 - 7) * config_.dim * 5);
    int rand_r2 = static_cast<int>(std::pow(2.0, min_size_2 - 5) * config_.dim * 5);

    world_.params.R = std::uniform_int_distribution<int>(rand_r1, rand_r2)(rng_);
    int B = B_dist(rng_);
    world_.params.b.clear();
    for (int i = 0; i < B; ++i) {
        world_.params.b.push_back(dist(rng_));
    }
    world_.params.b[std::uniform_int_distribution<int>(0, B - 1)(rng_)] = 1.0;
    world_.params.m = 0.1 + dist(rng_) * 0.4;
    world_.params.s = world_.params.m / (5.0 + dist(rng_) * 15.0);

    automaton_->calc_kernel();
    random_world(reseed, fill);
}

void LeniaApp::cppn_world(bool reseed) {
    if (!cppn_) {
        int mid_x = config_.size.back() / 2;
        int mid_y = config_.size[config_.size.size() - 2] / 2;
        cppn_ = std::make_unique<CPPN>(config_.size.back(), config_.size[config_.size.size() - 2],
                                        mid_x, mid_y, world_.params.R, 8, 2.0, 64);
    }
    auto z = cppn_->generate_z(rng_);
    world_.cells = cppn_->generate(z);
    world_updated();
}

void LeniaApp::transform_world() {
    // Apply rotation (2D only, using rotate[1] as the angle)
    if (tx_.rotate[1] != 0.0) {
        world_.rotate_2d(-tx_.rotate[1], true);
    }
    // Apply zoom (R change)
    if (tx_.R != world_.params.R) {
        double factor = static_cast<double>(tx_.R) / world_.params.R;
        Board zoomed(world_.cells.shape());
        zoomed.cells = world_.cells.copy();
        zoomed.crop();
        zoomed.zoom_2d(factor);
        world_.clear();
        world_.add(zoomed, std::vector<int>(config_.dim, 0), true);
        world_.params.R = tx_.R;
        automaton_->calc_kernel();
    }
    // Apply flip
    if (tx_.flip >= 0 && tx_.flip <= 1) {
        world_.flip(tx_.flip);
    }
    // Apply shift
    bool has_shift = false;
    for (int s : tx_.shift) if (s != 0) has_shift = true;
    if (has_shift) {
        world_.shift(tx_.shift);
    }
    clear_transform();
}

void LeniaApp::world_updated() {
    automaton_->calc_kernel();
    automaton_->reset();
    analyzer_->reset();
    clear_transform();
}

// ============================================================================
// Search/Breeding
// ============================================================================

void LeniaApp::toggle_search(int dir, bool small) {
    if (search_mode_ == 0) {
        search_mode_ = dir;
        is_search_small_ = small;
        search_total_ = 0;
        search_success_ = 0;
        is_run_ = true;
        status_messages.push_back("> search started (dir=" + std::to_string(dir) + ")");
    } else {
        stop_search();
    }
}

void LeniaApp::stop_search() {
    search_mode_ = 0;
    is_run_ = false;
    status_messages.push_back("> search stopped (" + std::to_string(search_success_) +
                              "/" + std::to_string(search_total_) + " found)");
}

bool LeniaApp::do_search() {
    if (search_mode_ == 0) return false;

    // Wait at least 25 steps before judging stability
    if (automaton_->gen() < 25) return false;

    search_total_++;

    if (analyzer_->is_empty() || analyzer_->is_full()) {
        // Organism died or filled the world — mutate and try again
        if (is_search_small_) {
            // Small mutation: tweak m and s slightly
            std::uniform_real_distribution<double> delta(-0.01, 0.01);
            world_.params.m = std::clamp(world_.params.m + delta(rng_), 0.01, 0.5);
            world_.params.s = std::clamp(world_.params.s + delta(rng_) * 0.1, 0.001, 0.1);
        } else {
            // Large mutation: fully random params
            random_world_and_params(true);
            return false;
        }
        automaton_->calc_kernel();
        random_world(true);
        return false;
    }

    // Organism is alive and contained — it's a success
    search_success_++;

    // If breeding up, try to increase complexity
    if (search_mode_ == +1) {
        std::uniform_real_distribution<double> delta(-0.005, 0.005);
        double new_m = std::clamp(world_.params.m + delta(rng_), 0.01, 0.5);
        double new_s = std::clamp(world_.params.s + delta(rng_) * 0.1, 0.001, 0.1);
        world_.params.m = new_m;
        world_.params.s = new_s;
        automaton_->calc_kernel();
        automaton_->reset();
        analyzer_->reset();
    }
    // If breeding down, try to simplify
    else if (search_mode_ == -1) {
        std::uniform_real_distribution<double> delta(-0.005, 0.005);
        world_.params.m = std::clamp(world_.params.m + delta(rng_), 0.01, 0.5);
        world_.params.s = std::clamp(world_.params.s + delta(rng_) * 0.1, 0.001, 0.1);
        automaton_->calc_kernel();
        automaton_->reset();
        analyzer_->reset();
    }

    // Log the find
    if (search_success_ % 10 == 0) {
        status_messages.push_back("> search: " + std::to_string(search_success_) + "/" +
                                  std::to_string(search_total_) + " stable organisms found");
    }

    return true;
}

// ============================================================================
// Simulation
// ============================================================================

void LeniaApp::step() {
    automaton_->calc_once(true);
    analyzer_->calc_stats();
    if (is_auto_center_) analyzer_->center_world();
    if (search_mode_ != 0) do_search();
    run_counter_++;
}

void LeniaApp::step_n(int n) {
    for (int i = 0; i < n; ++i) step();
}

// ============================================================================
// Rendering
// ============================================================================

Colormap LeniaApp::create_colormap(const std::vector<std::array<int, 3>>& colors,
                                    bool marker_white) const {
    Colormap cmap;
    int n = static_cast<int>(colors.size());
    for (int i = 0; i < 256; ++i) {
        double t = static_cast<double>(i) / 255.0 * (n - 1);
        int idx0 = std::min(static_cast<int>(t), n - 2);
        int idx1 = idx0 + 1;
        double frac = t - idx0;
        for (int ch = 0; ch < 3; ++ch) {
            double v0 = colors[idx0][ch] / 8.0 * 255.0;
            double v1 = colors[idx1][ch] / 8.0 * 255.0;
            double v = v0 + (v1 - v0) * frac;
            reinterpret_cast<uint8_t*>(&cmap.entries[i])[ch] = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
        }
    }
    return cmap;
}

void LeniaApp::set_colormap(int id) {
    colormap_id_ = std::clamp(id, 0, static_cast<int>(colormaps_.size()) - 1);
}

double LeniaApp::normalize(double v, double vmin, double vmax) const {
    if (vmax <= vmin) return 0.0;
    return std::clamp((v - vmin) / (vmax - vmin), 0.0, 1.0);
}

void LeniaApp::render_rgb(std::vector<uint8_t>& rgb_out, int& width, int& height,
                           int show_what) const {
    int ndim = world_.cells.ndim();
    if (ndim < 2) return;

    // For 3D+: render Z-middle slice. For 2D: render directly.
    int rows, cols;
    int slice_offset = 0;
    if (ndim == 2) {
        rows = world_.cells.shape(0);
        cols = world_.cells.shape(1);
    } else {
        // Last two dims are Y, X. Slice at midpoint of all earlier dims.
        rows = world_.cells.shape(ndim - 2);
        cols = world_.cells.shape(ndim - 1);
        int stride = rows * cols;
        slice_offset = 0;
        int outer = 1;
        for (int d = 0; d < ndim - 2; ++d) outer *= world_.cells.shape(d);
        slice_offset = (outer / 2) * stride;  // middle slice
    }
    int px = config_.pixel;
    width = cols * px;
    height = rows * px;
    rgb_out.resize(width * height * 3);

    // Get the data source (cells, potential, or field)
    const std::vector<double>* src_data = &world_.cells.data();
    if (show_what == 1 && !automaton_->potential().empty()) {
        src_data = &automaton_->potential();
    } else if (show_what == 2 && !automaton_->field().empty()) {
        src_data = &automaton_->field();
    }

    double vmin = 0.0, vmax = 1.0;
    if (show_what != 0) {
        vmin = *std::min_element(src_data->begin(), src_data->end());
        vmax = *std::max_element(src_data->begin(), src_data->end());
        if (vmax <= vmin) vmax = vmin + 1.0;
    }

    const auto& cmap = colormaps_[colormap_id_];

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int flat = slice_offset + r * cols + c;
            double v = (flat >= 0 && flat < static_cast<int>(src_data->size())) ? (*src_data)[flat] : 0.0;
            double nv = normalize(v, vmin, vmax);
            int ci = std::clamp(static_cast<int>(nv * 255.0), 0, 255);
            auto color = cmap.entries[ci];

            for (int pr = 0; pr < px; ++pr) {
                for (int pc = 0; pc < px; ++pc) {
                    int oy = r * px + pr;
                    int ox = c * px + pc;
                    if (oy < height && ox < width) {
                        int idx = (oy * width + ox) * 3;
                        rgb_out[idx] = color.r;
                        rgb_out[idx + 1] = color.g;
                        rgb_out[idx + 2] = color.b;
                    }
                }
            }
        }
    }
}

// ============================================================================
// Save/Load
// ============================================================================

void LeniaApp::save_world(bool sequential) {
    fs::create_directories(config_.save_root);
    auto data = world_.to_data(true);
    std::string filename;
    if (sequential) {
        filename = config_.save_root + "/save_" + std::to_string(run_counter_) + ".json";
    } else {
        filename = config_.save_root + "/" + world_.names[0] + ".json";
    }

    std::ofstream file(filename);
    file << "{\n";
    for (auto it = data.begin(); it != data.end(); ++it) {
        file << "  \"" << it->first << "\": \"" << it->second << "\"";
        if (std::next(it) != data.end()) file << ",";
        file << "\n";
    }
    file << "}\n";
    status_messages.push_back("> saved to " + filename);
}

void LeniaApp::copy_world_json(std::string& out) const {
    auto data = world_.to_data(true);
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [key, val] : data) {
        if (!first) oss << ", ";
        oss << "\"" << key << "\": \"" << val << "\"";
        first = false;
    }
    oss << "}";
    out = oss.str();
}

void LeniaApp::paste_world_json(const std::string& json_str) {
    auto entries = parse_animals_json("[" + json_str + "]");
    if (entries.empty()) return;
    Board part(entries[0].cells.shape());
    part.cells = entries[0].cells.copy();
    part.params = entries[0].params;
    part.names = {entries[0].code, entries[0].name, entries[0].cname};
    load_part(part, true, true);
}

void LeniaApp::toggle_recording(bool save_frames) {
    recorder_->toggle_recording(save_frames);
}

} // namespace lenia
