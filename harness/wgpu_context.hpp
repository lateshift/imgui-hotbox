#pragma once

#include <imgui.h>
#include <webgpu/webgpu_cpp.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

struct GLFWwindow;

namespace harness {

class WebGpuContext {
public:
    explicit WebGpuContext(GLFWwindow* window);
    ~WebGpuContext();

    WebGpuContext(const WebGpuContext&) = delete;
    WebGpuContext& operator=(const WebGpuContext&) = delete;

    const wgpu::Device& device() const { return device_; }
    wgpu::TextureFormat format() const { return config_.format; }
    const std::string& adapter_name() const { return adapter_name_; }
    bool failed() const { return failed_->load(); }

    /// Return false if the window is minimized or the surface needs another frame.
    bool render(ImDrawData* draw_data, ImVec4 clear, const std::filesystem::path& capture, bool force_offscreen = false);

private:
    bool configure_surface();

    GLFWwindow* window_;
    std::shared_ptr<std::atomic_bool> failed_ = std::make_shared<std::atomic_bool>(false);
    wgpu::Instance instance_;
    wgpu::Surface surface_;
    wgpu::Device device_;
    wgpu::Queue queue_;
    wgpu::Texture offscreen_;
    unsigned offscreen_width_ = 0, offscreen_height_ = 0;
    wgpu::SurfaceConfiguration config_;
    std::string adapter_name_;
    float scale_x_ = 0;
    float scale_y_ = 0;
    bool configured_ = false;
};

void WritePng(const wgpu::Instance& instance, const wgpu::Buffer& pixels,
    wgpu::TextureFormat format, unsigned width, unsigned height, unsigned stride,
    const std::filesystem::path& path);

} // namespace harness
