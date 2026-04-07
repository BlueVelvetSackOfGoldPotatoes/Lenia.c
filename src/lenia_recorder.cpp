#include "lenia_recorder.h"
#include <iostream>
#include <fstream>

namespace lenia {

namespace fs = std::filesystem;

Recorder::Recorder(Board& world, int pixel_w, int pixel_h)
    : world_(world), pixel_w_(pixel_w), pixel_h_(pixel_h) {}

Recorder::~Recorder() {
    if (is_recording_) {
        try { finish_record(); } catch (...) {}
    }
}

std::string Recorder::make_record_id() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()) % 1000000;
    std::ostringstream oss;
    std::string code = world_.names[0];
    auto paren = code.find('(');
    if (paren != std::string::npos) code = code.substr(0, paren);
    oss << code << "-" << std::put_time(std::localtime(&time), "%Y%m%d-%H%M%S")
        << "-" << std::setfill('0') << std::setw(6) << ms.count();
    return oss.str();
}

void Recorder::toggle_recording(bool save_frames) {
    is_save_frames_ = save_frames;
    if (!is_recording_) {
        start_record();
    } else {
        finish_record();
    }
}

void Recorder::start_record() {
    is_recording_ = true;
    record_id_ = make_record_id();
    record_seq_ = 1;

    video_path_ = std::string(RECORD_ROOT) + "/" + record_id_ + VIDEO_EXT;
    gif_path_ = std::string(RECORD_ROOT) + "/" + record_id_ + GIF_EXT;
    img_dir_ = std::string(RECORD_ROOT) + "/" + record_id_;

    fs::create_directories(RECORD_ROOT);

    if (is_save_frames_) {
        fs::create_directories(img_dir_);
        std::cout << "> start saving frames to " << img_dir_ << std::endl;
    } else {
        std::string cmd = "ffmpeg -loglevel warning -y"
            " -f rawvideo -vcodec rawvideo -pix_fmt rgb24"
            " -s " + std::to_string(pixel_w_) + "x" + std::to_string(pixel_h_) +
            " -r " + std::to_string(ANIM_FPS) +
            " -i -"
            " -an -vcodec copy"
            " " + video_path_;
        video_pipe_ = popen(cmd.c_str(), "w");
        if (!video_pipe_) {
            std::cerr << "> ffmpeg not found, video recording disabled" << std::endl;
        } else {
            std::cout << "> start recording video..." << std::endl;
        }
    }
    gif_frames_.clear();
}

void Recorder::save_ppm(const std::string& path, const std::vector<uint8_t>& rgb, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
}

void Recorder::save_image(const std::vector<uint8_t>& rgb_data, int width, int height,
                          const std::string& filename) {
    std::string id = make_record_id();
    std::string path = filename.empty()
        ? (std::string(RECORD_ROOT) + "/" + id + ".ppm")
        : (filename + ".ppm");
    fs::create_directories(fs::path(path).parent_path());
    save_ppm(path, rgb_data, width, height);
}

void Recorder::record_frame(const std::vector<uint8_t>& rgb_data, int width, int height) {
    if (is_save_frames_) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%03d", record_seq_);
        std::string path = img_dir_ + "/" + buf + ".ppm";
        save_ppm(path, rgb_data, width, height);
    } else {
        if (video_pipe_) {
            fwrite(rgb_data.data(), 1, rgb_data.size(), video_pipe_);
        }
    }

    gif_frames_.push_back({rgb_data, width, height});
    record_seq_++;
}

void Recorder::finish_record() {
    if (is_save_frames_) {
        std::cout << "> frames saved to " << img_dir_ << std::endl;

        std::string cmd = "ffmpeg -loglevel warning -y"
            " -f image2 -pattern_type glob -framerate " + std::to_string(ANIM_FPS) +
            " -i '" + img_dir_ + "/*.ppm'"
            " -an -vcodec libx264 -pix_fmt yuv420p"
            " " + video_path_;
        int ret = std::system(cmd.c_str());
        if (ret == 0) {
            std::cout << "> video saved to " << video_path_ << std::endl;
        } else {
            std::cerr << "> ffmpeg encoding failed" << std::endl;
        }
    } else {
        if (video_pipe_) {
            pclose(video_pipe_);
            video_pipe_ = nullptr;
            std::cout << "> video saved to " << video_path_ << std::endl;
        }
    }

    // Save GIF as individual frames (full GIF encoding requires a library)
    // For now, save as an image sequence that can be converted externally
    if (!gif_frames_.empty()) {
        std::string gif_dir = std::string(RECORD_ROOT) + "/" + record_id_ + "_gif";
        fs::create_directories(gif_dir);
        for (size_t i = 0; i < gif_frames_.size(); ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%04zu", i);
            save_ppm(gif_dir + "/" + buf + ".ppm",
                     gif_frames_[i].rgb, gif_frames_[i].width, gif_frames_[i].height);
        }
        // Convert to GIF via ffmpeg if available
        std::string cmd = "ffmpeg -loglevel warning -y"
            " -f image2 -pattern_type glob -framerate " + std::to_string(ANIM_FPS) +
            " -i '" + gif_dir + "/*.ppm'"
            " " + gif_path_;
        int ret = std::system(cmd.c_str());
        if (ret == 0) {
            std::cout << "> GIF saved to " << gif_path_ << std::endl;
        }
        gif_frames_.clear();
    }

    is_recording_ = false;
}

} // namespace lenia
