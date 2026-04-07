#pragma once

#include "lenia_board.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace lenia {

/// Recorder: handles video recording via ffmpeg pipe and frame export.
/// Faithful port of Python Recorder class from LeniaND.py.
class Recorder {
public:
    static constexpr const char* RECORD_ROOT = "record";
    static constexpr const char* FRAME_EXT = ".png";
    static constexpr const char* VIDEO_EXT = ".mov";
    static constexpr const char* GIF_EXT = ".gif";
    static constexpr int ANIM_FPS = 25;

    explicit Recorder(Board& world, int pixel_w, int pixel_h);
    ~Recorder();

    void toggle_recording(bool save_frames = false);
    void start_record();
    void record_frame(const std::vector<uint8_t>& rgb_data, int width, int height);
    void finish_record();
    void save_image(const std::vector<uint8_t>& rgb_data, int width, int height,
                    const std::string& filename = "");

    bool is_recording() const { return is_recording_; }

private:
    Board& world_;
    int pixel_w_, pixel_h_;
    bool is_recording_ = false;
    bool is_save_frames_ = false;
    std::string record_id_;
    int record_seq_ = 0;
    std::string img_dir_;
    std::string video_path_;
    std::string gif_path_;
    FILE* video_pipe_ = nullptr;

    // GIF frames stored as raw RGB buffers
    struct GifFrame {
        std::vector<uint8_t> rgb;
        int width, height;
    };
    std::vector<GifFrame> gif_frames_;

    std::string make_record_id() const;
    void save_ppm(const std::string& path, const std::vector<uint8_t>& rgb, int w, int h);
};

} // namespace lenia
