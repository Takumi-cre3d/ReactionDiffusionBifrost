#include "ReactionDiffusionCore.h"
#include "ReactionDiffusionVolumeCore.h"
#include "ReactionDiffusionSurfaceCore.h"

#include <cuda_runtime.h>

#include <chrono>
#include <climits>
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

template<class T> class DeviceArray {
public:
    DeviceArray() = default;

    explicit DeviceArray(std::size_t count) {
        require_cuda(
            cudaMalloc(reinterpret_cast<void**>(&data_), count * sizeof(T)),
            "cudaMalloc");
    }

    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;

    ~DeviceArray() {
        if (data_) {
            cudaFree(data_);
        }
    }

    T* get() noexcept { return data_; }

private:
    T* data_ = nullptr;
};
using DeviceBuffer = DeviceArray<float>;

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

__device__ float sample_volume(const float* values, int x, int y, int z,
    int width, int height, int depth, int boundary, float fixed) {
    if (boundary == static_cast<int>(BoundaryMode::Periodic)) {
        x = wrap_coordinate(x, width); y = wrap_coordinate(y, height);
        z = wrap_coordinate(z, depth);
    } else if (boundary == static_cast<int>(BoundaryMode::NoFlux)) {
        x = clamp_coordinate(x, width); y = clamp_coordinate(y, height);
        z = clamp_coordinate(z, depth);
    } else if (x < 0 || y < 0 || z < 0 || x >= width || y >= height || z >= depth) {
        return fixed;
    }
    return values[(static_cast<std::size_t>(z) * height + y) * width + x];
}

__device__ float laplacian_volume(const float* values, int x, int y, int z,
    int w, int h, int d, int boundary, float fixed) {
    float result = -sample_volume(values, x, y, z, w, h, d, boundary, fixed);
    result += sample_volume(values, x-1, y, z, w, h, d, boundary, fixed) * (1.0f/6.0f);
    result += sample_volume(values, x+1, y, z, w, h, d, boundary, fixed) * (1.0f/6.0f);
    result += sample_volume(values, x, y-1, z, w, h, d, boundary, fixed) * (1.0f/6.0f);
    result += sample_volume(values, x, y+1, z, w, h, d, boundary, fixed) * (1.0f/6.0f);
    result += sample_volume(values, x, y, z-1, w, h, d, boundary, fixed) * (1.0f/6.0f);
    result += sample_volume(values, x, y, z+1, w, h, d, boundary, fixed) * (1.0f/6.0f);
    return result;
}

__global__ void gray_scott_step_kernel(
    const float* concentration_a,
    const float* concentration_b,
    float* next_a,
    float* next_b,
    int width,
    int height,
    int depth,
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
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * depth;
    if (index >= count) {
        return;
    }

    const int x = static_cast<int>(index % static_cast<std::size_t>(width));
    const int y = static_cast<int>((index / width) % height);
    const int z = static_cast<int>(index / (static_cast<std::size_t>(width) * height));
    const float a = concentration_a[index];
    const float b = concentration_b[index];
    const float lap_a = depth == 1 ? laplacian_grid(
        concentration_a, x, y, width, height, boundary_mode, 1.0f) : laplacian_volume(
        concentration_a, x, y, z, width, height, depth, boundary_mode, 1.0f);
    const float lap_b = depth == 1 ? laplacian_grid(
        concentration_b, x, y, width, height, boundary_mode, 0.0f) : laplacian_volume(
        concentration_b, x, y, z, width, height, depth, boundary_mode, 0.0f);
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

template<class State>
StepResult step_cuda_dense(
    State& state,
    const Parameters& parameters,
    int depth,
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
    if (state.width > INT_MAX || state.height > INT_MAX || depth < 1 ||
        state.size() > static_cast<std::size_t>(INT_MAX)) {
        throw std::invalid_argument("CUDA dense grid exceeds supported index range.");
    }

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
                depth,
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

__global__ void surface_kernel(const float* a, const float* b, float* na, float* nb,
    const int* offsets, const int* neighbours, const float* weights, int count,
    float feed, float kill, float da, float db, float dt, bool clamp) {
    const int i=blockIdx.x*blockDim.x+threadIdx.x;
    if (i>=count) return;
    float la=0,lb=0;
    for (int e=offsets[i];e<offsets[i+1];++e) {
        la+=weights[e]*(a[neighbours[e]]-a[i]);
        lb+=weights[e]*(b[neighbours[e]]-b[i]);
    }
    const float reaction=a[i]*b[i]*b[i];
    float va=a[i]+(da*la-reaction+feed*(1-a[i]))*dt;
    float vb=b[i]+(db*lb+reaction-(feed+kill)*b[i])*dt;
    na[i]=clamp ? fminf(1,fmaxf(0,va)) : va;
    nb[i]=clamp ? fminf(1,fmaxf(0,vb)) : vb;
}

StepResult step_cuda_surface(const SurfaceTopology& mesh, SurfaceState& state,
    const Parameters& p, int substeps, Backend requested) {
    const auto start=std::chrono::steady_clock::now();
    const auto count=state.a.size(), bytes=count*sizeof(float);
    DeviceBuffer a(count),b(count),na(count),nb(count),weights(mesh.weights.size());
    DeviceArray<int> offsets(mesh.offsets.size()), neighbours(mesh.neighbours.size());
    require_cuda(cudaMemcpy(a.get(),state.a.data(),bytes,cudaMemcpyHostToDevice),"surface A upload");
    require_cuda(cudaMemcpy(b.get(),state.b.data(),bytes,cudaMemcpyHostToDevice),"surface B upload");
    require_cuda(cudaMemcpy(weights.get(),mesh.weights.data(),mesh.weights.size()*sizeof(float),cudaMemcpyHostToDevice),"surface weights upload");
    require_cuda(cudaMemcpy(offsets.get(),mesh.offsets.data(),mesh.offsets.size()*sizeof(int),cudaMemcpyHostToDevice),"surface offsets upload");
    require_cuda(cudaMemcpy(neighbours.get(),mesh.neighbours.data(),mesh.neighbours.size()*sizeof(int),cudaMemcpyHostToDevice),"surface neighbours upload");
    float *ca=a.get(),*cb=b.get(),*oa=na.get(),*ob=nb.get();
    const int splits=surface_splits(mesh,p);
    for (int step=0;step<substeps*splits;++step) {
        surface_kernel<<<static_cast<unsigned>((count+255)/256),256>>>(ca,cb,oa,ob,
            offsets.get(),neighbours.get(),weights.get(),static_cast<int>(count),
            p.feed_rate,p.kill_rate,p.diffusion_a,p.diffusion_b,p.time_step/splits,p.clamp_concentrations);
        require_cuda(cudaGetLastError(),"surface kernel");
        std::swap(ca,oa); std::swap(cb,ob);
    }
    require_cuda(cudaDeviceSynchronize(),"surface synchronize");
    require_cuda(cudaMemcpy(state.a.data(),ca,bytes,cudaMemcpyDeviceToHost),"surface A download");
    require_cuda(cudaMemcpy(state.b.data(),cb,bytes,cudaMemcpyDeviceToHost),"surface B download");
    StepResult result;
    result.requested_backend=requested; result.actual_backend=Backend::CUDA; result.substeps=substeps;
    result.status=requested==Backend::Auto ? "auto_selected_cuda" : "cuda";
    result.elapsed_milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return result;
}

StepResult step_cuda_2d(GridState& state, const Parameters& parameters,
    const std::vector<SeedSample>& seeds, BoundaryMode boundary, int substeps,
    Backend requested) {
    Detail::validate_parameters(parameters);
    if (substeps < 0) throw std::invalid_argument("Negative substeps.");
    apply_seeds(state, seeds, boundary);
    return step_cuda_dense(state, parameters, 1, boundary, substeps, requested);
}

StepResult step_cuda_volume(VolumeState& state, const Parameters& parameters,
    const std::vector<VolumeSeedSample>& seeds, BoundaryMode boundary, int substeps,
    Backend requested) {
    Detail::validate_parameters(parameters);
    if (substeps < 0 || state.depth > INT_MAX) throw std::invalid_argument("Invalid CUDA volume dimensions or substeps.");
    apply_volume_seeds(state, seeds, boundary);
    return step_cuda_dense(state, parameters, static_cast<int>(state.depth), boundary, substeps, requested);
}

} // namespace ReactionDiffusionCore
} // namespace Takumi
