#ifndef TAKUMI_REACTION_DIFFUSION_VOLUME_CORE_H
#define TAKUMI_REACTION_DIFFUSION_VOLUME_CORE_H

#include "ReactionDiffusionCore.h"

namespace Takumi {
namespace ReactionDiffusionCore {

struct VolumeSeedSample {
    float u = 0.5f;
    float v = 0.5f;
    float w = 0.5f;
    float radius = 0.02f;
    float strength = 1.0f;
    SeedMode mode = SeedMode::AddB;
};

struct VolumeState {
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t depth = 0;
    std::vector<float> a;
    std::vector<float> b;
    std::vector<float> scratch_a;
    std::vector<float> scratch_b;

    VolumeState() = default;

    VolumeState(std::size_t grid_width, std::size_t grid_height, std::size_t grid_depth) {
        reset(grid_width, grid_height, grid_depth);
    }

    void reset(std::size_t grid_width, std::size_t grid_height, std::size_t grid_depth) {
        if (grid_width < 3 || grid_height < 3 || grid_depth < 3) {
            throw std::invalid_argument("Reaction-diffusion volumes must be at least 3x3x3.");
        }
        if (grid_width > std::numeric_limits<std::size_t>::max() / grid_height) {
            throw std::overflow_error("Reaction-diffusion volume size overflow.");
        }
        const std::size_t slice_size = grid_width * grid_height;
        if (slice_size > std::numeric_limits<std::size_t>::max() / grid_depth) {
            throw std::overflow_error("Reaction-diffusion volume size overflow.");
        }
        width = grid_width;
        height = grid_height;
        depth = grid_depth;
        const std::size_t count = slice_size * depth;
        a.assign(count, 1.0f);
        b.assign(count, 0.0f);
        scratch_a.assign(count, 1.0f);
        scratch_b.assign(count, 0.0f);
    }

    std::size_t size() const noexcept { return width * height * depth; }

    bool valid() const noexcept {
        const std::size_t count = size();
        return width >= 3 && height >= 3 && depth >= 3 &&
            a.size() == count && b.size() == count;
    }

    void ensure_scratch() {
        const std::size_t count = size();
        scratch_a.resize(count);
        scratch_b.resize(count);
    }
};

namespace VolumeDetail {

inline float sample(
    const std::vector<float>& values,
    long x,
    long y,
    long z,
    std::size_t width,
    std::size_t height,
    std::size_t depth,
    BoundaryMode boundary,
    float fixed_value) noexcept {
    if (boundary == BoundaryMode::Periodic) {
        const std::size_t sx = Detail::wrap_index(x, width);
        const std::size_t sy = Detail::wrap_index(y, height);
        const std::size_t sz = Detail::wrap_index(z, depth);
        return values[(sz * height + sy) * width + sx];
    }
    if (boundary == BoundaryMode::NoFlux) {
        const std::size_t sx = Detail::clamp_index(x, width);
        const std::size_t sy = Detail::clamp_index(y, height);
        const std::size_t sz = Detail::clamp_index(z, depth);
        return values[(sz * height + sy) * width + sx];
    }
    if (x < 0 || y < 0 || z < 0 ||
        x >= static_cast<long>(width) ||
        y >= static_cast<long>(height) ||
        z >= static_cast<long>(depth)) {
        return fixed_value;
    }
    return values[(static_cast<std::size_t>(z) * height + static_cast<std::size_t>(y)) * width +
        static_cast<std::size_t>(x)];
}

// A normalized 6-neighbour stencil keeps the diffusion scale comparable to
// the normalized 2D stencil while remaining isotropic along the voxel axes.
inline float laplacian(
    const std::vector<float>& values,
    long x,
    long y,
    long z,
    std::size_t width,
    std::size_t height,
    std::size_t depth,
    BoundaryMode boundary,
    float fixed_value) noexcept {
    constexpr float neighbour_weight = 1.0f / 6.0f;
    float result = sample(values, x, y, z, width, height, depth, boundary, fixed_value) * -1.0f;
    result += sample(values, x - 1, y, z, width, height, depth, boundary, fixed_value) * neighbour_weight;
    result += sample(values, x + 1, y, z, width, height, depth, boundary, fixed_value) * neighbour_weight;
    result += sample(values, x, y - 1, z, width, height, depth, boundary, fixed_value) * neighbour_weight;
    result += sample(values, x, y + 1, z, width, height, depth, boundary, fixed_value) * neighbour_weight;
    result += sample(values, x, y, z - 1, width, height, depth, boundary, fixed_value) * neighbour_weight;
    result += sample(values, x, y, z + 1, width, height, depth, boundary, fixed_value) * neighbour_weight;
    return result;
}

inline std::size_t seed_center(float normalized, std::size_t extent) noexcept {
    const auto cell = static_cast<std::size_t>(
        std::floor(normalized * static_cast<float>(extent)));
    return std::min(cell, extent - 1);
}

} // namespace VolumeDetail

inline void apply_volume_seeds(
    VolumeState& state,
    const std::vector<VolumeSeedSample>& seeds,
    BoundaryMode boundary) {
    if (!state.valid()) {
        throw std::invalid_argument("Invalid reaction-diffusion volume state.");
    }

    const float minimum_extent = static_cast<float>(std::min({state.width, state.height, state.depth}));
    for (const VolumeSeedSample& seed : seeds) {
        if (!std::isfinite(seed.u) || !std::isfinite(seed.v) || !std::isfinite(seed.w) ||
            !std::isfinite(seed.radius) || !std::isfinite(seed.strength)) {
            continue;
        }
        const float normalized_u = boundary == BoundaryMode::Periodic
            ? seed.u - std::floor(seed.u) : Detail::clamp01(seed.u);
        const float normalized_v = boundary == BoundaryMode::Periodic
            ? seed.v - std::floor(seed.v) : Detail::clamp01(seed.v);
        const float normalized_w = boundary == BoundaryMode::Periodic
            ? seed.w - std::floor(seed.w) : Detail::clamp01(seed.w);
        const long center_x = static_cast<long>(VolumeDetail::seed_center(normalized_u, state.width));
        const long center_y = static_cast<long>(VolumeDetail::seed_center(normalized_v, state.height));
        const long center_z = static_cast<long>(VolumeDetail::seed_center(normalized_w, state.depth));
        const float radius_cells = std::max(0.5f, std::abs(seed.radius) * minimum_extent);
        const long integer_radius = static_cast<long>(std::ceil(radius_cells));
        const float strength = Detail::clamp01(seed.strength);

        for (long offset_z = -integer_radius; offset_z <= integer_radius; ++offset_z) {
            for (long offset_y = -integer_radius; offset_y <= integer_radius; ++offset_y) {
                for (long offset_x = -integer_radius; offset_x <= integer_radius; ++offset_x) {
                    const float distance = std::sqrt(static_cast<float>(
                        offset_x * offset_x + offset_y * offset_y + offset_z * offset_z));
                    if (distance > radius_cells) {
                        continue;
                    }
                    const long raw_x = center_x + offset_x;
                    const long raw_y = center_y + offset_y;
                    const long raw_z = center_z + offset_z;
                    if (boundary != BoundaryMode::Periodic &&
                        (raw_x < 0 || raw_y < 0 || raw_z < 0 ||
                         raw_x >= static_cast<long>(state.width) ||
                         raw_y >= static_cast<long>(state.height) ||
                         raw_z >= static_cast<long>(state.depth))) {
                        continue;
                    }
                    const std::size_t x = boundary == BoundaryMode::Periodic
                        ? Detail::wrap_index(raw_x, state.width) : static_cast<std::size_t>(raw_x);
                    const std::size_t y = boundary == BoundaryMode::Periodic
                        ? Detail::wrap_index(raw_y, state.height) : static_cast<std::size_t>(raw_y);
                    const std::size_t z = boundary == BoundaryMode::Periodic
                        ? Detail::wrap_index(raw_z, state.depth) : static_cast<std::size_t>(raw_z);
                    const std::size_t index = (z * state.height + y) * state.width + x;
                    const float falloff = Detail::clamp01(1.0f - distance / radius_cells);
                    const float amount = strength * falloff;
                    switch (seed.mode) {
                        case SeedMode::AddB:
                            state.b[index] = std::max(state.b[index], amount);
                            break;
                        case SeedMode::EraseB:
                            state.b[index] *= (1.0f - amount);
                            break;
                        case SeedMode::SetB:
                            state.b[index] += (strength - state.b[index]) * falloff;
                            break;
                    }
                }
            }
        }
    }
}

inline StepResult step_volume_cpu(
    VolumeState& state,
    const Parameters& parameters,
    const std::vector<VolumeSeedSample>& seeds,
    BoundaryMode boundary,
    int substeps) {
    if (!state.valid()) {
        throw std::invalid_argument("Invalid reaction-diffusion volume state.");
    }
    if (substeps < 0) {
        throw std::invalid_argument("substeps must be zero or greater.");
    }
    Detail::validate_parameters(parameters);
    state.ensure_scratch();
    apply_volume_seeds(state, seeds, boundary);

    const auto started = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < substeps; ++iteration) {
        const long depth = static_cast<long>(state.depth);
#if defined(RD_HAS_OPENMP) || defined(_OPENMP)
#pragma omp parallel for schedule(static)
#endif
        for (long z = 0; z < depth; ++z) {
            for (std::size_t y = 0; y < state.height; ++y) {
                for (std::size_t x = 0; x < state.width; ++x) {
                    const std::size_t index =
                        (static_cast<std::size_t>(z) * state.height + y) * state.width + x;
                    const float a = state.a[index];
                    const float b = state.b[index];
                    const float lap_a = VolumeDetail::laplacian(
                        state.a, static_cast<long>(x), static_cast<long>(y), z,
                        state.width, state.height, state.depth, boundary, 1.0f);
                    const float lap_b = VolumeDetail::laplacian(
                        state.b, static_cast<long>(x), static_cast<long>(y), z,
                        state.width, state.height, state.depth, boundary, 0.0f);
                    const float reaction = a * b * b;
                    float next_a = a + (
                        parameters.diffusion_a * lap_a - reaction +
                        parameters.feed_rate * (1.0f - a)) * parameters.time_step;
                    float next_b = b + (
                        parameters.diffusion_b * lap_b + reaction -
                        (parameters.kill_rate + parameters.feed_rate) * b) * parameters.time_step;
                    if (parameters.clamp_concentrations) {
                        next_a = Detail::clamp01(next_a);
                        next_b = Detail::clamp01(next_b);
                    }
                    state.scratch_a[index] = next_a;
                    state.scratch_b[index] = next_b;
                }
            }
        }
        state.a.swap(state.scratch_a);
        state.b.swap(state.scratch_b);
    }
    const auto finished = std::chrono::steady_clock::now();

    StepResult result;
    result.requested_backend = Backend::CPU;
    result.actual_backend = Backend::CPU;
    result.substeps = substeps;
    result.elapsed_milliseconds =
        std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

#if defined(RD_HAS_CUDA)
StepResult step_cuda_volume(VolumeState&, const Parameters&,
    const std::vector<VolumeSeedSample>&, BoundaryMode, int, Backend);
#endif

inline StepResult step_volume(
    VolumeState& state,
    const Parameters& parameters,
    const std::vector<VolumeSeedSample>& seeds,
    BoundaryMode boundary,
    int substeps,
    Backend requested_backend = Backend::Auto,
    bool allow_cpu_fallback = true) {
#if defined(RD_HAS_CUDA)
    if (requested_backend != Backend::CPU && cuda_backend_available()) {
        return step_cuda_volume(state, parameters, seeds, boundary, substeps, requested_backend);
    }
#endif
    if (requested_backend == Backend::CUDA && !allow_cpu_fallback) {
        throw std::runtime_error("CUDA volume backend unavailable in this build or device.");
    }
    StepResult result = step_volume_cpu(state, parameters, seeds, boundary, substeps);
    result.requested_backend = requested_backend;
    if (requested_backend == Backend::CUDA) {
        result.status = cuda_backend_compiled()
            ? "cuda_no_device_fallback_cpu" : "cuda_not_built_fallback_cpu";
    } else if (requested_backend == Backend::Auto) {
        result.status = "auto_selected_cpu_volume";
    }
    return result;
}

inline void compute_volume_outputs(
    const VolumeState& state,
    BoundaryMode boundary,
    std::vector<float>& pattern,
    std::vector<float>& gradient_x,
    std::vector<float>& gradient_y,
    std::vector<float>& gradient_z) {
    if (!state.valid()) {
        throw std::invalid_argument("Invalid reaction-diffusion volume state.");
    }
    pattern = state.b;
    gradient_x.resize(state.size());
    gradient_y.resize(state.size());
    gradient_z.resize(state.size());

    const long depth = static_cast<long>(state.depth);
#if defined(RD_HAS_OPENMP) || defined(_OPENMP)
#pragma omp parallel for schedule(static)
#endif
    for (long z = 0; z < depth; ++z) {
        for (std::size_t y = 0; y < state.height; ++y) {
            for (std::size_t x = 0; x < state.width; ++x) {
                const std::size_t index =
                    (static_cast<std::size_t>(z) * state.height + y) * state.width + x;
                gradient_x[index] = 0.5f * (
                    VolumeDetail::sample(state.b, static_cast<long>(x) + 1, static_cast<long>(y), z,
                        state.width, state.height, state.depth, boundary, 0.0f) -
                    VolumeDetail::sample(state.b, static_cast<long>(x) - 1, static_cast<long>(y), z,
                        state.width, state.height, state.depth, boundary, 0.0f));
                gradient_y[index] = 0.5f * (
                    VolumeDetail::sample(state.b, static_cast<long>(x), static_cast<long>(y) + 1, z,
                        state.width, state.height, state.depth, boundary, 0.0f) -
                    VolumeDetail::sample(state.b, static_cast<long>(x), static_cast<long>(y) - 1, z,
                        state.width, state.height, state.depth, boundary, 0.0f));
                gradient_z[index] = 0.5f * (
                    VolumeDetail::sample(state.b, static_cast<long>(x), static_cast<long>(y), z + 1,
                        state.width, state.height, state.depth, boundary, 0.0f) -
                    VolumeDetail::sample(state.b, static_cast<long>(x), static_cast<long>(y), z - 1,
                        state.width, state.height, state.depth, boundary, 0.0f));
            }
        }
    }
}

} // namespace ReactionDiffusionCore
} // namespace Takumi

#endif // TAKUMI_REACTION_DIFFUSION_VOLUME_CORE_H
