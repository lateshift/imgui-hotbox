#include "wgpu_context.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace harness {

void WritePng(const wgpu::Instance& instance, const wgpu::Buffer& pixels,
    wgpu::TextureFormat format, unsigned width, unsigned height, unsigned stride,
    const std::filesystem::path& path) {
    const auto size = static_cast<std::size_t>(stride) * height;
    bool mapped = false;
    const auto request = pixels.MapAsync(wgpu::MapMode::Read, 0, size, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
            mapped = status == wgpu::MapAsyncStatus::Success;
        });
    if (instance.WaitAny(request, std::numeric_limits<std::uint64_t>::max()) != wgpu::WaitStatus::Success || !mapped)
        throw std::runtime_error("Cannot map the WebGPU screenshot buffer");

    const auto* source = static_cast<const unsigned char*>(pixels.GetConstMappedRange(0, size));
    const auto packed_stride = static_cast<std::size_t>(width) * 4;
    std::vector<unsigned char> rgba(packed_stride * height);
    for (unsigned row = 0; row < height; ++row) {
        auto* destination = rgba.data() + row * packed_stride;
        std::copy_n(source + static_cast<std::size_t>(row) * stride, packed_stride, destination);
        if (format == wgpu::TextureFormat::BGRA8Unorm) {
            for (unsigned column = 0; column < width; ++column)
                std::swap(destination[column * 4u], destination[column * 4u + 2]);
        }
    }
    pixels.Unmap();

    std::ofstream file(path, std::ios::binary);
    const auto write = [](void* stream, void* data, int bytes) {
        static_cast<std::ofstream*>(stream)->write(static_cast<const char*>(data), bytes);
    };
    if (!file || !stbi_write_png_to_func(write, &file, static_cast<int>(width),
            static_cast<int>(height), 4, rgba.data(), static_cast<int>(packed_stride)) || !file)
        throw std::runtime_error("Cannot write screenshot to " + path.string());
}

} // namespace harness
