#include "lenia_app.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <thread>

static void print_help() {
    std::cout << R"(
Lenia C++ Interactive GUI

Controls:
  SPACE     Toggle run/pause
  R         Random world
  N         Random world + random params
  C         Clear world
  G         CPPN-generated pattern
  1-9       Load animal by index * 50
  +/-       Next/previous animal
  [ / ]     Previous/next colormap
  S         Save current frame as PPM
  Q / Esc   Quit
  ← → ↑ ↓  Shift world
  P         Show potential
  F         Show field
  W         Show world (default)
)" << std::endl;
}

int main(int argc, char* argv[]) {
    int size = 256;
    int pixel = 2;
    std::string animals_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (arg == "--pixel" && i + 1 < argc) pixel = std::atoi(argv[++i]);
        else if (arg == "--animals" && i + 1 < argc) animals_path = argv[++i];
        else if (arg == "--help") { print_help(); return 0; }
    }

    lenia::LeniaApp::Config cfg;
    cfg.size = {size, size};
    cfg.pixel = pixel;
    if (!animals_path.empty()) cfg.animals_path = animals_path;

    lenia::LeniaApp app(cfg);
    app.world().params.R = 13;
    app.world().params.T = 10;
    app.world().params.b = {1.0};
    app.world().params.m = 0.15;
    app.world().params.s = 0.015;
    app.world().params.kn = 1;
    app.world().params.gn = 1;

    if (!animals_path.empty() && !app.animals().empty()) {
        app.load_animal_id(2);  // Load first real creature (skip headers)
    } else {
        app.random_world();
    }

    int win_w = size * pixel;
    int win_h = size * pixel;

    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "Cannot open X display" << std::endl;
        return 1;
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    Window window = XCreateSimpleWindow(display, root, 0, 0, win_w, win_h, 0,
                                         BlackPixel(display, screen),
                                         BlackPixel(display, screen));
    XStoreName(display, window, "Lenia C++");
    XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(display, window);

    // Wait for map
    XEvent event;
    while (true) {
        XNextEvent(display, &event);
        if (event.type == MapNotify) break;
    }

    GC gc = XCreateGC(display, window, 0, nullptr);
    XImage* ximage = nullptr;
    char* image_data = nullptr;
    int depth = DefaultDepth(display, screen);

    // Allocate image buffer
    image_data = new char[win_w * win_h * 4];
    ximage = XCreateImage(display, DefaultVisual(display, screen), depth, ZPixmap,
                           0, image_data, win_w, win_h, 32, 0);

    std::vector<uint8_t> rgb;
    int img_w, img_h;
    int show_what = 0;
    bool running = true;
    int frame_count = 0;
    auto last_fps_time = std::chrono::high_resolution_clock::now();
    double fps = 0;

    app.set_running(true);

    print_help();

    while (true) {
        // Process X events
        while (XPending(display)) {
            XNextEvent(display, &event);
            if (event.type == KeyPress) {
                KeySym key = XLookupKeysym(&event.xkey, 0);
                switch (key) {
                    case XK_q: case XK_Escape: goto done;
                    case XK_space: app.set_running(!app.is_running()); break;
                    case XK_r: app.random_world(true); break;
                    case XK_n: app.random_world_and_params(true); break;
                    case XK_c: app.clear_world(); break;
                    case XK_g: app.cppn_world(true); break;
                    case XK_s: {
                        app.render_rgb(rgb, img_w, img_h, show_what);
                        std::ofstream f("lenia_screenshot.ppm", std::ios::binary);
                        f << "P6\n" << img_w << " " << img_h << "\n255\n";
                        f.write(reinterpret_cast<char*>(rgb.data()), rgb.size());
                        std::cout << "> Saved lenia_screenshot.ppm" << std::endl;
                        break;
                    }
                    case XK_p: show_what = 1; break;
                    case XK_f: show_what = 2; break;
                    case XK_w: show_what = 0; break;
                    case XK_bracketleft:
                        app.set_colormap((app.config().dim + 8) % 9); break;
                    case XK_bracketright:
                        app.set_colormap((app.config().dim + 1) % 9); break;
                    case XK_plus: case XK_equal:
                        if (!app.animals().empty())
                            app.load_animal_id(app.animal_id() + 1);
                        break;
                    case XK_minus:
                        if (!app.animals().empty())
                            app.load_animal_id(std::max(0, app.animal_id() - 1));
                        break;
                    case XK_Left: app.tx().shift = {0, -5}; app.transform_world(); break;
                    case XK_Right: app.tx().shift = {0, 5}; app.transform_world(); break;
                    case XK_Up: app.tx().shift = {-5, 0}; app.transform_world(); break;
                    case XK_Down: app.tx().shift = {5, 0}; app.transform_world(); break;
                    default:
                        if (key >= XK_1 && key <= XK_9) {
                            int idx = (key - XK_1) * 50 + 2;
                            if (!app.animals().empty())
                                app.load_animal_id(std::min(idx, static_cast<int>(app.animals().size()) - 1));
                        }
                        break;
                }
            }
        }

        // Step simulation
        if (app.is_running()) {
            app.step();
        }

        // Render
        app.render_rgb(rgb, img_w, img_h, show_what);

        // Convert RGB to XImage (BGRA)
        for (int y = 0; y < std::min(img_h, win_h); ++y) {
            for (int x = 0; x < std::min(img_w, win_w); ++x) {
                int src = (y * img_w + x) * 3;
                int dst = (y * win_w + x) * 4;
                image_data[dst + 0] = rgb[src + 2]; // B
                image_data[dst + 1] = rgb[src + 1]; // G
                image_data[dst + 2] = rgb[src + 0]; // R
                image_data[dst + 3] = 0;
            }
        }

        XPutImage(display, window, gc, ximage, 0, 0, 0, 0, win_w, win_h);

        // FPS counter
        frame_count++;
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_fps_time).count();
        if (elapsed >= 1.0) {
            fps = frame_count / elapsed;
            frame_count = 0;
            last_fps_time = now;
            std::string title = "Lenia C++ - gen " + std::to_string(app.automaton().gen()) +
                " mass=" + std::to_string(static_cast<int>(app.analyzer().mass())) +
                " fps=" + std::to_string(static_cast<int>(fps));
            if (!app.animals().empty() && app.animal_id() < static_cast<int>(app.animals().size())) {
                title += " [" + app.animals()[app.animal_id()].name + "]";
            }
            XStoreName(display, window, title.c_str());
        }

        // Small delay to not max CPU when paused
        if (!app.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

done:
    ximage->data = nullptr;  // prevent XDestroyImage from freeing our buffer
    XDestroyImage(ximage);
    delete[] image_data;
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}
