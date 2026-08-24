#ifndef TAKUMI_REACTION_DIFFUSION_CORE_H
#define TAKUMI_REACTION_DIFFUSION_CORE_H

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Takumi {
namespace ReactionDiffusionCore {

enum class BoundaryMode : int {
    Periodic = 0,
    NoFlux = 1,
    FixedInitial = 2
};

enum class Backend : int {
    Auto = 0,
    CPU = 1,
    CUDA = 2
};

enum class SeedMode : int {
    AddB = 0,
    EraseB = 1,
    SetB = 2
};

struct Parameters {
    float feed_rate = 0.055f;
    float kill_rate = 0.062f;
    float diffusion_a = 1.0f;
    float diffusion_b = 0.5f;
    float time_step = 1.0f;
    bool clamp_concentrations = true;
};

struct SeedSample {
    float u = 0.5f;
    float v = 0.5f;
    float radius = 0.02f;
    float strength = 1.0f;
    SeedMode mode = SeedMode::AddB;
};

struct GridState {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<float> a;
    std::vector<float> b;
    std::vector<float> scratch_a;
    std::vector<float> scratch_b;

    GridState() = default;

    GridState(std::size_t grid_width, std::size_t grid_height) {
        reset(grid_width, grid_height);
    }

    void reset(std::size_t grid_width, std::size_t grid_height) {
        if (grid_width < 3 || grid_height < 3) {
            throw std::invalid_argument("Reaction-diffusion grids must be at least 3x3.");
        }
        if (grid_width > std::numeric_limits<std::size_t>::max() / grid_height) {
            throw std::overflow_error("Reaction-diffusion grid size overflow.");
        }
        width = grid_width;
        height = grid_height;
        const std::size_t count = width * height;
        a.assign(count, 1.0f);
        b.assign(count, 0.0f);
        scratch_a.assign(count, 1.0f);
        scratch_b.assign(count, 0.0f);
    }

    std::size_t size() const noexcept { return width * height; }

    bool valid() const noexcept {
        const std::size_t count = size();
        return width >= 3 && height >= 3 && a.size() == count && b.size() == count;
    }

    void ensure_scratch() {
        const std::size_t count = size();
        scratch_a.resize(count);
        scratch_b.resize(count);
    }
};

// Feedback compounds cache a single value between graph executions. Keep A
// and B in one explicit array so the state can be connected as one Bifrost
// feedback port without hidden native globals. Layout is [A0..An, B0..Bn].
inline std::vector<float> pack_grid_state(const GridState& state) {
    if (!state.valid()) {
        throw std::invalid_argument("Invalid reaction-diffusion state.");
    }
    if (state.size() > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::overflow_error("Packed reaction-diffusion state size overflow.");
    }
    std::vector<float> packed;
    packed.reserve(state.size() * 2);
    packed.insert(packed.end(), state.a.begin(), state.a.end());
    packed.insert(packed.end(), state.b.begin(), state.b.end());
    return packed;
}

inline GridState unpack_grid_state(
    const std::vector<float>& packed,
    std::size_t width,
    std::size_t height) {
    GridState state(width, height);
    const std::size_t count = state.size();
    if (count > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::overflow_error("Packed reaction-diffusion state size overflow.");
    }
    if (packed.size() != count * 2) {
        throw std::invalid_argument(
            "Packed state length must equal 2 * width * height.");
    }
    state.a.assign(packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(count));
    state.b.assign(packed.begin() + static_cast<std::ptrdiff_t>(count), packed.end());
    return state;
}

struct StepResult {
    Backend requested_backend = Backend::CPU;
    Backend actual_backend = Backend::CPU;
    int substeps = 0;
    double elapsed_milliseconds = 0.0;
    std::string status = "ok";
};

inline bool cuda_backend_compiled() noexcept {
#if defined(RD_HAS_CUDA)
    return true;
#else
    return false;
#endif
}

#if defined(RD_HAS_CUDA)
bool cuda_device_available() noexcept;

StepResult step_cuda_2d(
    GridState& state,
    const Parameters& parameters,
    const std::vector<SeedSample>& seeds,
    BoundaryMode boundary,
    int substeps,
    Backend requested_backend);
#endif

inline bool cuda_backend_available() noexcept {
#if defined(RD_HAS_CUDA)
    return cuda_device_available();
#else
    return false;
#endif
}

namespace Detail {

inline std::size_t wrap_index(long value, std::size_t extent) noexcept {
    const long e = static_cast<long>(extent);
    long wrapped = value % e;
    if (wrapped < 0) {
        wrapped += e;
    }
    return static_cast<std::size_t>(wrapped);
}

inline std::size_t clamp_index(long value, std::size_t extent) noexcept {
    if (value < 0) {
        return 0;
    }
    const auto converted = static_cast<std::size_t>(value);
    return std::min(converted, extent - 1);
}

inline float clamp01(float value) noexcept {
    return std::max(0.0f, std::min(1.0f, value));
}

inline void validate_parameters(const Parameters& parameters) {
    const float values[] = {
        parameters.feed_rate,
        parameters.kill_rate,
        parameters.diffusion_a,
        parameters.diffusion_b,
        parameters.time_step
    };
    for (const float value : values) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Reaction-diffusion parameters must be finite.");
        }
    }
    if (parameters.feed_rate < 0.0f || parameters.kill_rate < 0.0f ||
        parameters.diffusion_a < 0.0f || parameters.diffusion_b < 0.0f ||
        parameters.time_step <= 0.0f) {
        throw std::invalid_argument("Reaction-diffusion rates must be non-negative and time_step must be positive.");
    }
}

inline float sample(
    const std::vector<float>& values,
    long x,
    long y,
    std::size_t width,
    std::size_t height,
    BoundaryMode boundary,
    float fixed_value) noexcept {
    if (boundary == BoundaryMode::Periodic) {
        return values[wrap_index(y, height) * width + wrap_index(x, width)];
    }
    if (boundary == BoundaryMode::NoFlux) {
        return values[clamp_index(y, height) * width + clamp_index(x, width)];
    }
    if (x < 0 || y < 0 || x >= static_cast<long>(width) || y >= static_cast<long>(height)) {
        return fixed_value;
    }
    return values[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
}

inline float laplacian(
    const std::vector<float>& values,
    long x,
    long y,
    std::size_t width,
    std::size_t height,
    BoundaryMode boundary,
    float fixed_value) noexcept {
    float result = sample(values, x, y, width, height, boundary, fixed_value) * -1.0f;
    result += sample(values, x - 1, y, width, height, boundary, fixed_value) * 0.2f;
    result += sample(values, x + 1, y, width, height, boundary, fixed_value) * 0.2f;
    result += sample(values, x, y - 1, width, height, boundary, fixed_value) * 0.2f;
    result += sample(values, x, y + 1, width, height, boundary, fixed_value) * 0.2f;
    result += sample(values, x - 1, y - 1, width, height, boundary, fixed_value) * 0.05f;
    result += sample(values, x + 1, y - 1, width, height, boundary, fixed_value) * 0.05f;
    result += sample(values, x - 1, y + 1, width, height, boundary, fixed_value) * 0.05f;
    result += sample(values, x + 1, y + 1, width, height, boundary, fixed_value) * 0.05f;
    return result;
}

} // namespace Detail

inline void apply_seeds(
    GridState& state,
    const std::vector<SeedSample>& seeds,
    BoundaryMode boundary) {
    if (!state.valid()) {
        throw std::invalid_argument("Invalid reaction-diffusion state.");
    }

    const float minimum_extent = static_cast<float>(std::min(state.width, state.height));
    for (const SeedSample& seed : seeds) {
        if (!std::isfinite(seed.u) || !std::isfinite(seed.v) ||
            !std::isfinite(seed.radius) || !std::isfinite(seed.strength)) {
            continue;
        }
        const float normalized_u = boundary == BoundaryMode::Periodic
            ? seed.u - std::floor(seed.u)
            : Detail::clamp01(seed.u);
        const float normalized_v = boundary == BoundaryMode::Periodic
            ? seed.v - std::floor(seed.v)
            : Detail::clamp01(seed.v);
        const long center_x = static_cast<long>(std::min(
            static_cast<std::size_t>(std::floor(normalized_u * static_cast<float>(state.width))),
            state.width - 1));
        const long center_y = static_cast<long>(std::min(
            static_cast<std::size_t>(std::floor(normalized_v * static_cast<float>(state.height))),
            state.height - 1));
        const float radius_cells = std::max(0.5f, std::abs(seed.radius) * minimum_extent);
        const long integer_radius = static_cast<long>(std::ceil(radius_cells));
        const float strength = Detail::clamp01(seed.strength);

        for (long offset_y = -integer_radius; offset_y <= integer_radius; ++offset_y) {
            for (long offset_x = -integer_radius; offset_x <= integer_radius; ++offset_x) {
                const float distance = std::sqrt(static_cast<float>(offset_x * offset_x + offset_y * offset_y));
                if (distance > radius_cells) {
                    continue;
                }
                long raw_x = center_x + offset_x;
                long raw_y = center_y + offset_y;
                if (boundary != BoundaryMode::Periodic &&
                    (raw_x < 0 || raw_y < 0 || raw_x >= static_cast<long>(state.width) || raw_y >= static_cast<long>(state.height))) {
                    continue;
                }
                const std::size_t x = boundary == BoundaryMode::Periodic
                    ? Detail::wrap_index(raw_x, state.width)
                    : static_cast<std::size_t>(raw_x);
                const std::size_t y = boundary == BoundaryMode::Periodic
                    ? Detail::wrap_index(raw_y, state.height)
                    : static_cast<std::size_t>(raw_y);
                const std::size_t index = y * state.width + x;
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

inline StepResult step_cpu(
    GridState& state,
    const Parameters& parameters,
    const std::vector<SeedSample>& seeds,
    BoundaryMode boundary,
    int substeps) {
    if (!state.valid()) {
        throw std::invalid_argument("Invalid reaction-diffusion state.");
    }
    if (substeps < 0) {
        throw std::invalid_argument("substeps must be zero or greater.");
    }
    Detail::validate_parameters(parameters);
    state.ensure_scratch();
    apply_seeds(state, seeds, boundary);

    const auto started = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < substeps; ++iteration) {
        const long height = static_cast<long>(state.height);
#if defined(RD_HAS_OPENMP) || defined(_OPENMP)
#pragma omp parallel for schedule(static)
#endif
        for (long y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < state.width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * state.width + x;
                const float a = state.a[index];
                const float b = state.b[index];
                const float lap_a = Detail::laplacian(
                    state.a, static_cast<long>(x), y, state.width, state.height, boundary, 1.0f);
                const float lap_b = Detail::laplacian(
                    state.b, static_cast<long>(x), y, state.width, state.height, boundary, 0.0f);
                const float reaction = a * b * b;
                float next_a = a + (
                    parameters.diffusion_a * lap_a - reaction + parameters.feed_rate * (1.0f - a)
                ) * parameters.time_step;
                float next_b = b + (
                    parameters.diffusion_b * lap_b + reaction -
                    (parameters.kill_rate + parameters.feed_rate) * b
                ) * parameters.time_step;
                if (parameters.clamp_concentrations) {
                    next_a = Detail::clamp01(next_a);
                    next_b = Detail::clamp01(next_b);
                }
                state.scratch_a[index] = next_a;
                state.scratch_b[index] = next_b;
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
    result.elapsed_milliseconds = std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

inline StepResult step(
    GridState& state,
    const Parameters& parameters,
    const std::vector<SeedSample>& seeds,
    BoundaryMode boundary,
    int substeps,
    Backend requested_backend = Backend::Auto,
    bool allow_cpu_fallback = true) {
#if defined(RD_HAS_CUDA)
    if ((requested_backend == Backend::CUDA || requested_backend == Backend::Auto) &&
        cuda_backend_available()) {
        return step_cuda_2d(
            state, parameters, seeds, boundary, substeps, requested_backend);
    }
#endif

    if (requested_backend == Backend::CUDA && !allow_cpu_fallback) {
        if (!cuda_backend_compiled()) {
            throw std::runtime_error(
                "The CUDA backend was requested but this build does not contain CUDA support.");
        }
        throw std::runtime_error(
            "The CUDA backend was requested but no CUDA device is available.");
    }

    StepResult result = step_cpu(state, parameters, seeds, boundary, substeps);
    result.requested_backend = requested_backend;
    if (requested_backend == Backend::CUDA) {
        result.status = cuda_backend_compiled()
            ? "cuda_no_device_fallback_cpu"
            : "cuda_not_built_fallback_cpu";
    } else if (requested_backend == Backend::Auto) {
        result.status = cuda_backend_compiled()
            ? "auto_selected_cpu_no_cuda_device"
            : "auto_selected_cpu";
    }
    return result;
}

inline void compute_outputs(
    const GridState& state,
    BoundaryMode boundary,
    std::vector<float>& pattern,
    std::vector<float>& gradient_u,
    std::vector<float>& gradient_v) {
    if (!state.valid()) {
        throw std::invalid_argument("Invalid reaction-diffusion state.");
    }
    pattern = state.b;
    gradient_u.resize(state.size());
    gradient_v.resize(state.size());

    const long height = static_cast<long>(state.height);
#if defined(RD_HAS_OPENMP) || defined(_OPENMP)
#pragma omp parallel for schedule(static)
#endif
    for (long y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < state.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * state.width + x;
            gradient_u[index] = 0.5f * (
                Detail::sample(state.b, static_cast<long>(x) + 1, y, state.width, state.height, boundary, 0.0f) -
                Detail::sample(state.b, static_cast<long>(x) - 1, y, state.width, state.height, boundary, 0.0f)
            );
            gradient_v[index] = 0.5f * (
                Detail::sample(state.b, static_cast<long>(x), y + 1, state.width, state.height, boundary, 0.0f) -
                Detail::sample(state.b, static_cast<long>(x), y - 1, state.width, state.height, boundary, 0.0f)
            );
        }
    }
}

} // namespace ReactionDiffusionCore
} // namespace Takumi

#endif // TAKUMI_REACTION_DIFFUSION_CORE_H
