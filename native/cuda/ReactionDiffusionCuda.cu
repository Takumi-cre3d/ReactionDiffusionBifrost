#include "ReactionDiffusionCore.h"

#include <cuda_runtime.h>

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace Takumi {
namespace ReactionDiffusionCore {
namespace {

void require_cuda(cudaError_t result, const char* operation) {
    if (result != cudaSuccess) {
        throw std::runtime_error(
            std::string(operation) + ": " + cudaGetErrorString(result));
    }
}

class DeviceBuffer {
public:
    DeviceBuffer() = default;

    explicit DeviceBuffer(std::size_t count) {
        require_cuda(
            cudaMalloc(reinterpret_cast<void**>(&data_), count * sizeof(float)),
            "cudaMalloc");
    }

    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    DeviceBuffer(DeviceBuffer&& other) noexcept : data_(other.data_) {
        other.data_ = nullptr;
    }

    DeviceBuffer& operator=(DeviceBuffer&& other) noexcept {
        if (this != &other) {
            if (data_) {
                cudaFree(data_);
            }
            data_ = other.data_;
            other.data_ = nullptr;
        }
        return *this;
    }

    ~DeviceBuffer() {
        if (data_) {
            cudaFree(data_);
        }
    }

    float* get() noexcept { return data_; }
    const float* get() const noexcept { return data_; }

private:
    float* data_ = nullptr;
};

__device__ int wrap_coordinate(int value, int extent) {
    int wrapped = value % extent;
    return wrapped < 0 ? wrapped + extent : wrapped;
}

__device__ int clamp_coordinate(int value, int extent) {
    return max(0, min(value, extent - 1));
}

__device__ float sample_grid(
    const float* values,
    int x,
    int y,
    int width,
    int height,
    int boundary_mode,
    float fixed_value) {
    if (boundary_mode == static_cast<int>(BoundaryMode::Periodic)) {
        x = wrap_coordinate(x, width);
        y = wrap_coordinate(y, height);
    } else if (boundary_mode == static_cast<int>(BoundaryMode::NoFlux)) {
        x = clamp_coordinate(x, width);
        y = clamp_coordinate(y, height);
    } else if (x < 0 || y < 0 || x >= width || y >= height) {
        return fixed_value;
    }
    return values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
        static_cast<std::size_t>(x)];
}

__device__ float laplacian_grid(
    const float* values,
    int x,
    int y,
    int width,
    int height,
    int boundary_mode,
    float fixed_value) {
    float result = sample_grid(
        values, x, y, width, height, boundary_mode, fixed_value) * -1.0f;
    result += sample_grid(
        values, x - 1, y, width, height, boundary_mode, fixed_value) * 0.2f;
    result += sample_grid(
        values, x + 1, y, width, height, boundary_mode, fixed_value) * 0.2f;
    result += sample_grid(
        values, x, y - 1, width, height, boundary_mode, fixed_value) * 0.2f;
    result += sample_grid(
        values, x, y + 1, width, height, boundary_mode, fixed_value) * 0.2f;
    result += sample_grid(
        values, x - 1, y - 1, width, height, boundary_mode, fixed_value) * 0.05f;
    result += sample_grid(
        values, x + 1, y - 1, width, height, boundary_mode, fixed_value) * 0.05f;
    result += sample_grid(
        values, x - 1, y + 1, width, height, boundary_mode, fixed_value) * 0.05f;
    result += sample_grid(
        values, x + 1, y + 1, width, height, boundary_mode, fixed_value) * 0.05f;
    return result;
}

__global__ void gray_scott_step_kernel(
    const float* concentration_a,
    const float* concentration_b,
    float* next_a,
    float* next_b,
    int width,
    int height,
    float feed_rate,
    float kill_rate,
    float diffusion_a,
    float diffusion_b,
    float time_step,
    int boundary_mode,
    bool clamp_concentrations) {
    const std::size_t index =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const std::size_t count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (index >= count) {
        return;
    }

    const int x = static_cast<int>(index % static_cast<std::size_t>(width));
    const int y = static_cast<int>(index / static_cast<std::size_t>(width));
    const float a = concentration_a[index];
    const float b = concentration_b[index];
    const float lap_a = laplacian_grid(
        concentration_a, x, y, width, height, boundary_mode, 1.0f);
    const float lap_b = laplacian_grid(
        concentration_b, x, y, width, height, boundary_mode, 0.0f);
    const float reaction = a * b * b;
    float output_a = a + (
        diffusion_a * lap_a - reaction + feed_rate * (1.0f - a)) * time_step;
    float output_b = b + (
        diffusion_b * lap_b + reaction - (kill_rate + feed_rate) * b) * time_step;
    if (clamp_concentrations) {
        output_a = fminf(1.0f, fmaxf(0.0f, output_a));
        output_b = fminf(1.0f, fmaxf(0.0f, output_b));
    }
    next_a[index] = output_a;
    next_b[index] = output_b;
}

} // namespace

bool cuda_device_available() noexcept {
    int device_count = 0;
    const cudaError_t result = cudaGetDeviceCount(&device_count);
    if (result != cudaSuccess) {
        cudaGetLastError();
        return false;
    }
    return device_count > 0;
}

StepResult step_cuda_2d(
    GridState& state,
    const Parameters& parameters,
    const std::vector<SeedSample>& seeds,
    BoundaryMode boundary,
    int substeps,
    Backend requested_backend) {
    if (!state.valid()) {
        throw std::invalid_argument("Invalid reaction-diffusion state.");
    }
    if (substeps < 0) {
        throw std::invalid_argument("substeps must be zero or greater.");
    }
    Detail::validate_parameters(parameters);
    apply_seeds(state, seeds, boundary);

    const auto started = std::chrono::steady_clock::now();
    if (substeps > 0) {
        const std::size_t count = state.size();
        const std::size_t bytes = count * sizeof(float);
        DeviceBuffer device_a(count);
        DeviceBuffer device_b(count);
        DeviceBuffer scratch_a(count);
        DeviceBuffer scratch_b(count);

        require_cuda(
            cudaMemcpy(device_a.get(), state.a.data(), bytes, cudaMemcpyHostToDevice),
            "cudaMemcpy A host to device");
        require_cuda(
            cudaMemcpy(device_b.get(), state.b.data(), bytes, cudaMemcpyHostToDevice),
            "cudaMemcpy B host to device");

        float* current_a = device_a.get();
        float* current_b = device_b.get();
        float* output_a = scratch_a.get();
        float* output_b = scratch_b.get();
        constexpr unsigned int threads_per_block = 256;
        const unsigned int block_count = static_cast<unsigned int>(
            (count + threads_per_block - 1) / threads_per_block);
        for (int iteration = 0; iteration < substeps; ++iteration) {
            gray_scott_step_kernel<<<block_count, threads_per_block>>>(
                current_a,
                current_b,
                output_a,
                output_b,
                static_cast<int>(state.width),
                static_cast<int>(state.height),
                parameters.feed_rate,
                parameters.kill_rate,
                parameters.diffusion_a,
                parameters.diffusion_b,
                parameters.time_step,
                static_cast<int>(boundary),
                parameters.clamp_concentrations);
            require_cuda(cudaGetLastError(), "Gray-Scott CUDA kernel launch");
            std::swap(current_a, output_a);
            std::swap(current_b, output_b);
        }
        require_cuda(cudaDeviceSynchronize(), "Gray-Scott CUDA synchronization");
        require_cuda(
            cudaMemcpy(state.a.data(), current_a, bytes, cudaMemcpyDeviceToHost),
            "cudaMemcpy A device to host");
        require_cuda(
            cudaMemcpy(state.b.data(), current_b, bytes, cudaMemcpyDeviceToHost),
            "cudaMemcpy B device to host");
    }
    const auto finished = std::chrono::steady_clock::now();

    StepResult result;
    result.requested_backend = requested_backend;
    result.actual_backend = Backend::CUDA;
    result.substeps = substeps;
    result.elapsed_milliseconds =
        std::chrono::duration<double, std::milli>(finished - started).count();
    result.status = requested_backend == Backend::Auto
        ? "auto_selected_cuda"
        : "cuda";
    return result;
}

} // namespace ReactionDiffusionCore
} // namespace Takumi
