#include "wgpu_context.hpp"

#include <GLFW/glfw3.h>
#include <imgui_impl_wgpu.h>
#include <spdlog/spdlog.h>
#include <webgpu/webgpu_glfw.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace harness {
namespace {

constexpr auto wait_forever = std::numeric_limits<std::uint64_t>::max();

std::string Message(wgpu::StringView message) {
    if (!message.data)
        return {};
    return message.length == WGPU_STRLEN ? std::string(message.data)
                                        : std::string(message.data, message.length);
}

} // namespace

WebGpuContext::WebGpuContext(GLFWwindow* window) : window_(window) {
    const auto feature = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instance_descriptor;
    instance_descriptor.requiredFeatureCount = 1;
    instance_descriptor.requiredFeatures = &feature;
    instance_ = wgpu::CreateInstance(&instance_descriptor);
    if (!instance_)
        throw std::runtime_error("Cannot create a Dawn instance");

    surface_ = wgpu::glfw::CreateSurfaceForWindow(instance_, window_);
    if (!surface_)
        throw std::runtime_error("Cannot create the GLFW WebGPU surface");

    wgpu::RequestAdapterOptions adapter_options;
    adapter_options.compatibleSurface = surface_;
    adapter_options.powerPreference = wgpu::PowerPreference::HighPerformance;
    wgpu::Adapter adapter;
    std::string error;
    const auto adapter_request = instance_.RequestAdapter(&adapter_options, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestAdapterStatus status, wgpu::Adapter candidate, wgpu::StringView message) {
            if (status == wgpu::RequestAdapterStatus::Success)
                adapter = std::move(candidate);
            else
                error = Message(message);
        });
    if (instance_.WaitAny(adapter_request, wait_forever) != wgpu::WaitStatus::Success || !adapter)
        throw std::runtime_error("Dawn adapter request failed. " + error);

    wgpu::AdapterInfo info;
    if (adapter.GetInfo(&info) == wgpu::Status::Success)
        adapter_name_ = Message(info.device);
    spdlog::info("Dawn adapter {} ({})", adapter_name_, Message(info.description));

    wgpu::DeviceDescriptor device_descriptor;
    device_descriptor.label = "Hotbox harness device";
    device_descriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message, std::atomic_bool* failed) {
            failed->store(true);
            spdlog::error("WebGPU error {}. {}", static_cast<int>(type), Message(message));
        }, failed_.get());
    device_descriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
        [failed = failed_](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
            if (reason == wgpu::DeviceLostReason::Destroyed || reason == wgpu::DeviceLostReason::CallbackCancelled)
                return;
            failed->store(true);
            spdlog::error("WebGPU device lost. {}", Message(message));
        });
    error.clear();
    const auto device_request = adapter.RequestDevice(&device_descriptor, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
            if (status == wgpu::RequestDeviceStatus::Success)
                device_ = std::move(device);
            else
                error = Message(message);
        });
    if (instance_.WaitAny(device_request, wait_forever) != wgpu::WaitStatus::Success || !device_)
        throw std::runtime_error("Dawn device request failed. " + error);

    wgpu::SurfaceCapabilities capabilities;
    if (surface_.GetCapabilities(adapter, &capabilities) != wgpu::Status::Success || !capabilities.formatCount)
        throw std::runtime_error("Cannot query WebGPU surface capabilities");
    const auto* end = capabilities.formats + capabilities.formatCount;
    for (const auto format : {wgpu::TextureFormat::BGRA8Unorm, wgpu::TextureFormat::RGBA8Unorm}) {
        if (std::find(capabilities.formats, end, format) != end) {
            config_.format = format;
            break;
        }
    }
    if (config_.format == wgpu::TextureFormat::Undefined)
        throw std::runtime_error("The surface must support BGRA8Unorm or RGBA8Unorm");

    queue_ = device_.GetQueue();
    config_.device = device_;
    config_.usage = wgpu::TextureUsage::RenderAttachment;
    if (capabilities.usages & wgpu::TextureUsage::CopySrc)
        config_.usage |= wgpu::TextureUsage::CopySrc;
    config_.presentMode = wgpu::PresentMode::Fifo;
    configure_surface();
}

WebGpuContext::~WebGpuContext() {
    if (configured_)
        surface_.Unconfigure();
    device_.Destroy();
}

bool WebGpuContext::configure_surface() {
    int width, height;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0)
        return false;

    float scale_x, scale_y;
    glfwGetWindowContentScale(window_, &scale_x, &scale_y);
    const auto w = static_cast<unsigned>(width);
    const auto h = static_cast<unsigned>(height);
    if (configured_ && config_.width == w && config_.height == h &&
        scale_x_ == scale_x && scale_y_ == scale_y)
        return true;

    config_.width = w;
    config_.height = h;
    scale_x_ = scale_x;
    scale_y_ = scale_y;
    surface_.Configure(&config_);
    configured_ = true;
    spdlog::info("WebGPU surface {} × {} pixels, GLFW content scale {} × {}",
        w, h, scale_x_, scale_y_);
    return true;
}

bool WebGpuContext::render(ImDrawData* draw_data, ImVec4 clear, const std::filesystem::path& capture, bool force_offscreen) {
    instance_.ProcessEvents();
    if (failed() || (!force_offscreen && !configure_surface()))
        return false;

    const bool offscreen = force_offscreen || (!capture.empty() && !(config_.usage & wgpu::TextureUsage::CopySrc));
    const auto width = force_offscreen
        ? static_cast<unsigned>(std::lround(draw_data->DisplaySize.x * draw_data->FramebufferScale.x)) : config_.width;
    const auto height = force_offscreen
        ? static_cast<unsigned>(std::lround(draw_data->DisplaySize.y * draw_data->FramebufferScale.y)) : config_.height;
    if (!width || !height)
        return false;
    wgpu::Texture target;
    if (offscreen) {
        if (!offscreen_ || offscreen_width_ != width || offscreen_height_ != height) {
            wgpu::TextureDescriptor descriptor;
            descriptor.size = {width, height, 1};
            descriptor.format = config_.format;
            descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
            offscreen_ = device_.CreateTexture(&descriptor);
            offscreen_width_ = width;
            offscreen_height_ = height;
        }
        target = offscreen_;
    } else {
        wgpu::SurfaceTexture current;
        surface_.GetCurrentTexture(&current);
        switch (current.status) {
        case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
            return false;
        case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
        case wgpu::SurfaceGetCurrentTextureStatus::Lost:
            surface_.Unconfigure();
            configured_ = false;
            return false;
        case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
        case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
            target = std::move(current.texture);
            break;
        default:
            throw std::runtime_error("Cannot acquire a WebGPU surface texture");
        }
    }

    wgpu::RenderPassColorAttachment attachment;
    attachment.view = target.CreateView();
    attachment.loadOp = wgpu::LoadOp::Clear;
    attachment.storeOp = wgpu::StoreOp::Store;
    attachment.clearValue = {clear.x, clear.y, clear.z, clear.w};
    wgpu::RenderPassDescriptor descriptor;
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &attachment;
    const auto encoder = device_.CreateCommandEncoder();
    const auto pass = encoder.BeginRenderPass(&descriptor);
    ImGui_ImplWGPU_RenderDrawData(draw_data, pass.Get());
    pass.End();

    wgpu::Buffer pixels;
    const auto stride = (width * 4u + 255u) & ~255u;
    if (!capture.empty()) {
        wgpu::BufferDescriptor readback;
        readback.size = static_cast<std::uint64_t>(stride) * height;
        readback.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
        pixels = device_.CreateBuffer(&readback);

        wgpu::TexelCopyTextureInfo source;
        source.texture = target;
        wgpu::TexelCopyBufferInfo destination;
        destination.buffer = pixels;
        destination.layout.bytesPerRow = stride;
        const wgpu::Extent3D extent{width, height, 1};
        encoder.CopyTextureToBuffer(&source, &destination, &extent);
    }

    const auto commands = encoder.Finish();
    queue_.Submit(1, &commands);
    if (!offscreen && surface_.Present() != wgpu::Status::Success)
        throw std::runtime_error("WebGPU presentation failed");
    if (pixels)
        WritePng(instance_, pixels, config_.format, width, height, stride, capture);
    instance_.ProcessEvents();
    return !failed();
}

} // namespace harness
