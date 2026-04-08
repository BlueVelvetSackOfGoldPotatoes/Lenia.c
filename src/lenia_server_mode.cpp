/**
 * Lenia server mode: runs simulation and writes binary frames to stdout.
 * Designed to be spawned by a Node.js API server that forwards binary frames
 * directly to WebSocket clients.
 *
 * Packet format:
 *   bytes  0.. 3  magic "LFRM"
 *   bytes  4.. 7  uint32_le version (=1)
 *   bytes  8..11  uint32_le metadata JSON byte length
 *   bytes 12..15  uint32_le cells plane byte length
 *   bytes 16..19  uint32_le potential plane byte length
 *   bytes 20..23  uint32_le field plane byte length
 *   bytes 24..    UTF-8 JSON metadata, then raw uint8 cells/potential/field planes
 */
#include "lenia_app.h"
#include "lenia_analysis.h"
#include <array>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <thread>

namespace {

constexpr size_t FRAME_HEADER_SIZE = 24;
constexpr uint32_t FRAME_VERSION = 1;
constexpr char FRAME_MAGIC[4] = {'L', 'F', 'R', 'M'};

void write_u32_le(uint8_t* dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value & 0xFFu);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    dst[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    dst[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

bool write_all(int fd, const void* data, size_t size) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t written = 0;
    while (written < size) {
        ssize_t n = ::write(fd, ptr + written, size - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

bool write_frame_packet(int fd, const std::string& metadata,
                        const std::vector<uint8_t>& cells,
                        const std::vector<uint8_t>& potential,
                        const std::vector<uint8_t>& field) {
    std::array<uint8_t, FRAME_HEADER_SIZE> header{};
    std::memcpy(header.data(), FRAME_MAGIC, sizeof(FRAME_MAGIC));
    write_u32_le(header.data() + 4, FRAME_VERSION);
    write_u32_le(header.data() + 8, static_cast<uint32_t>(metadata.size()));
    write_u32_le(header.data() + 12, static_cast<uint32_t>(cells.size()));
    write_u32_le(header.data() + 16, static_cast<uint32_t>(potential.size()));
    write_u32_le(header.data() + 20, static_cast<uint32_t>(field.size()));

    return write_all(fd, header.data(), header.size()) &&
           write_all(fd, metadata.data(), metadata.size()) &&
           write_all(fd, cells.data(), cells.size()) &&
           write_all(fd, potential.data(), potential.size()) &&
           write_all(fd, field.data(), field.size());
}

} // namespace

static std::string escape_json(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

int main(int argc, char* argv[]) {
    int size = 128;
    int fps = 30;
    int dim = 2;
    std::string animals_path;
    std::string initial_code;
    int colormap = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (arg == "--fps" && i + 1 < argc) fps = std::atoi(argv[++i]);
        else if (arg == "--dim" && i + 1 < argc) dim = std::atoi(argv[++i]);
        else if (arg == "--animals" && i + 1 < argc) animals_path = argv[++i];
        else if (arg == "--code" && i + 1 < argc) initial_code = argv[++i];
        else if (arg == "--colormap" && i + 1 < argc) colormap = std::atoi(argv[++i]);
    }

    lenia::LeniaApp::Config cfg;
    cfg.dim = dim;
    if (dim == 3) {
        cfg.size = {size, size, size};
        cfg.animals_path = animals_path.empty() ? "animals3D.json" : animals_path;
    } else {
        cfg.size = {size, size};
    }
    cfg.pixel = 1;
    if (!animals_path.empty()) cfg.animals_path = animals_path;

    lenia::LeniaApp app(cfg);
    app.set_colormap(colormap);

    if (!initial_code.empty() && !app.animals().empty()) {
        int id = app.get_animal_id(initial_code);
        if (id >= 0) app.load_animal_id(id);
    } else {
        app.random_world_and_params(true);
    }
    app.set_running(true);

    auto frame_duration = std::chrono::microseconds(1000000 / fps);
    const int view_size = size * size;
    std::vector<uint8_t> cells_u8(view_size);
    std::vector<uint8_t> pot_u8(view_size, 0);
    std::vector<uint8_t> fld_u8(view_size, 0);

    // Read commands from stdin, write state frames to stdout
    // Commands (one per line): "step", "pause", "run", "random", "random_params",
    //   "load CODE", "set_m VALUE", "set_s VALUE", "set_R VALUE", "set_T VALUE",
    //   "set_kn VALUE", "set_gn VALUE", "set_b VALUE,VALUE,...", "colormap N", "quit"

    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    std::string line;
    bool running = true;
    char stdin_buf[4096];
    std::string stdin_accum;

    while (running) {
        auto frame_start = std::chrono::high_resolution_clock::now();

        // Read stdin non-blocking
        ssize_t nread = read(0, stdin_buf, sizeof(stdin_buf) - 1);
        if (nread > 0) {
            stdin_buf[nread] = '\0';
            stdin_accum += stdin_buf;
            size_t nl;
            while ((nl = stdin_accum.find('\n')) != std::string::npos) {
                line = stdin_accum.substr(0, nl);
                stdin_accum.erase(0, nl + 1);
                if (line.empty()) continue;
                if (line == "quit") { running = false; break; }
                else if (line == "step") { app.step(); }
                else if (line == "pause") { app.set_running(false); }
                else if (line == "run") { app.set_running(true); }
                else if (line == "random") { app.random_world(true); }
                else if (line == "random_params") { app.random_world_and_params(true); }
                else if (line == "clear") { app.clear_world(); }
                else if (line == "cppn") { app.cppn_world(true); }
                else if (line.substr(0, 5) == "load ") { app.load_animal_code(line.substr(5)); }
                else if (line.substr(0, 6) == "set_m ") { app.world().params.m = std::stod(line.substr(6)); app.world_updated(); }
                else if (line.substr(0, 6) == "set_s ") { app.world().params.s = std::stod(line.substr(6)); app.world_updated(); }
                else if (line.substr(0, 6) == "set_R ") { app.world().params.R = std::stoi(line.substr(6)); app.world_updated(); }
                else if (line.substr(0, 6) == "set_T ") { app.world().params.T = std::stoi(line.substr(6)); app.world_updated(); }
                else if (line.substr(0, 7) == "set_kn ") { app.world().params.kn = std::stoi(line.substr(7)); app.world_updated(); }
                else if (line.substr(0, 7) == "set_gn ") { app.world().params.gn = std::stoi(line.substr(7)); app.world_updated(); }
                else if (line.substr(0, 9) == "colormap ") { app.set_colormap(std::stoi(line.substr(9))); }
                else if (line == "search_up") { app.toggle_search(+1); }
                else if (line == "search_down") { app.toggle_search(-1); }
                else if (line == "search_stop") { app.stop_search(); }
                else if (line.substr(0, 5) == "stim ") {
                    // Set convection velocity: adds v·∇A to the Lenia equation.
                    // The organism is smoothly transported while staying alive.
                    std::istringstream ss(line.substr(5));
                    double dx = 0, dy = 0;
                    ss >> dx >> dy;
                    double speed = 8.0;  // convection strength (visible at 128x128)
                    app.automaton().convection_vx = dx * speed;
                    app.automaton().convection_vy = dy * speed;
                }
                else if (line == "stim_stop") {
                    app.automaton().convection_vx = 0;
                    app.automaton().convection_vy = 0;
                }
            }
        } else if (nread == 0) {
            // EOF on stdin — parent closed pipe. Exit gracefully.
            running = false; break;
        }
        // nread == -1 with EAGAIN/EWOULDBLOCK is normal for non-blocking — ignore

        if (!running) break;

        // Step simulation
        if (app.is_running()) {
            app.step();
        }

        // Encode cells, potential, field as uint8 for compact transmission
        const auto& cells = app.world().cells.data();
        const auto& potential = app.automaton().potential();
        const auto& field_data = app.automaton().field();
        if (dim == 3) {
            int z_mid = size / 2;
            int slice_offset = z_mid * size * size;
            for (int i = 0; i < view_size; ++i) {
                cells_u8[i] = static_cast<uint8_t>(std::clamp(cells[slice_offset + i] * 255.0, 0.0, 255.0));
            }
        } else {
            for (int i = 0; i < view_size; ++i) {
                cells_u8[i] = static_cast<uint8_t>(std::clamp(cells[i] * 255.0, 0.0, 255.0));
            }
        }

        // Encode potential (normalize to 0-255 from its actual range)
        if (!potential.empty()) {
            const auto [pmin_it, pmax_it] = std::minmax_element(potential.begin(), potential.end());
            const double pmin = *pmin_it;
            const double pmax = *pmax_it;
            const double prange = std::max(pmax - pmin, 1e-10);
            for (int i = 0; i < view_size; ++i) {
                pot_u8[i] = static_cast<uint8_t>(std::clamp((potential[i] - pmin) / prange * 255.0, 0.0, 255.0));
            }
        }

        // Encode field (normalize -1..+1 to 0..255)
        if (!field_data.empty()) {
            for (int i = 0; i < view_size; ++i) {
                fld_u8[i] = static_cast<uint8_t>(std::clamp((field_data[i] + 1.0) * 0.5 * 255.0, 0.0, 255.0));
            }
        }
        std::ostringstream meta;
        meta << "{\"gen\":" << app.automaton().gen()
             << ",\"time\":" << app.automaton().time()
             << ",\"mass\":" << app.analyzer().mass()
             << ",\"growth\":" << app.analyzer().growth()
             << ",\"speed\":" << app.analyzer().speed()
             << ",\"gyradius\":" << app.analyzer().gyradius()
             << ",\"lyapunov\":" << app.analyzer().lyapunov()
             << ",\"symmetry\":" << app.analyzer().symmetry()
             << ",\"is_empty\":" << (app.analyzer().is_empty() ? "true" : "false")
             << ",\"is_full\":" << (app.analyzer().is_full() ? "true" : "false")
             << ",\"running\":" << (app.is_running() ? "true" : "false")
             << ",\"params\":{\"R\":" << app.world().params.R
             << ",\"T\":" << app.world().params.T
             << ",\"m\":" << app.world().params.m
             << ",\"s\":" << app.world().params.s
             << ",\"kn\":" << app.world().params.kn
             << ",\"gn\":" << app.world().params.gn
             << ",\"b\":[";
        for (size_t i = 0; i < app.world().params.b.size(); ++i) {
            if (i > 0) meta << ",";
            meta << app.world().params.b[i];
        }
        meta << "]}"
             << ",\"name\":\"" << escape_json(app.world().names[1]) << "\""
             << ",\"code\":\"" << escape_json(app.world().names[0]) << "\""
             << ",\"components\":" << (app.automaton().gen() % 10 == 0 ? lenia::analysis::count_components(app.world().cells) : -1)
             << ",\"entropy\":" << (app.automaton().gen() % 10 == 0 ? lenia::analysis::shannon_entropy(app.world().cells) : -1)
             << ",\"dim\":" << dim
             << ",\"width\":" << size
             << ",\"height\":" << size
             << "}";

        const std::string metadata = meta.str();
        if (!write_frame_packet(STDOUT_FILENO, metadata, cells_u8, pot_u8, fld_u8)) {
            running = false;
            break;
        }

        // Rate limit
        auto elapsed = std::chrono::high_resolution_clock::now() - frame_start;
        if (elapsed < frame_duration) {
            std::this_thread::sleep_for(frame_duration - elapsed);
        }
    }

    return 0;
}
