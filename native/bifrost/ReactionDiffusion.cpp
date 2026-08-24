#include "ReactionDiffusion.h"
#include "ReactionDiffusionCore.h"
#include "ReactionDiffusionVolumeCore.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace Core = Takumi::ReactionDiffusionCore;

namespace {

Core::BoundaryMode to_core_boundary(Takumi::ReactionDiffusion::BoundaryMode mode) {
    switch (mode) {
        case Takumi::ReactionDiffusion::BoundaryMode::Periodic: return Core::BoundaryMode::Periodic;
        case Takumi::ReactionDiffusion::BoundaryMode::NoFlux: return Core::BoundaryMode::NoFlux;
        case Takumi::ReactionDiffusion::BoundaryMode::FixedInitial: return Core::BoundaryMode::FixedInitial;
    }
    return Core::BoundaryMode::Periodic;
}

Core::Backend to_core_backend(Takumi::ReactionDiffusion::Backend backend) {
    switch (backend) {
        case Takumi::ReactionDiffusion::Backend::Auto: return Core::Backend::Auto;
        case Takumi::ReactionDiffusion::Backend::CPU: return Core::Backend::CPU;
        case Takumi::ReactionDiffusion::Backend::CUDA: return Core::Backend::CUDA;
    }
    return Core::Backend::Auto;
}

Core::SeedMode to_core_seed_mode(int mode) {
    switch (mode) {
        case 1: return Core::SeedMode::EraseB;
        case 2: return Core::SeedMode::SetB;
        default: return Core::SeedMode::AddB;
    }
}

template <typename T>
Amino::Ptr<Amino::Array<T>> to_amino_array(const std::vector<T>& values) {
    auto result = Amino::newMutablePtr<Amino::Array<T>>(values.begin(), values.end());
    return Amino::Ptr<Amino::Array<T>>(std::move(result));
}

template <typename T>
std::size_t array_size(const Amino::Ptr<Amino::Array<T>>& values) {
    return values ? values->size() : 0;
}

template <typename T>
Amino::Ptr<Amino::Array<T>> empty_amino_array() {
    return to_amino_array(std::vector<T>{});
}

void set_grid_error_outputs(
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray> const& concentration_a,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray> const& concentration_b,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& out_concentration_a,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& out_concentration_b,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& pattern,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& gradient_u,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& gradient_v,
    Amino::String& backend_used,
    Amino::String& status,
    float& elapsed_milliseconds,
    const char* message) {
    out_concentration_a = concentration_a ? concentration_a : empty_amino_array<float>();
    out_concentration_b = concentration_b ? concentration_b : empty_amino_array<float>();
    pattern = empty_amino_array<float>();
    gradient_u = empty_amino_array<float>();
    gradient_v = empty_amino_array<float>();
    backend_used = "ERROR";
    const std::string error_status = std::string("error: ") + message;
    status = error_status.c_str();
    elapsed_milliseconds = 0.0f;
}

void set_volume_error_outputs(
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray> const& concentration_a,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray> const& concentration_b,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& out_concentration_a,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& out_concentration_b,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& pattern,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& gradient_x,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& gradient_y,
    Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& gradient_z,
    Amino::String& backend_used,
    Amino::String& status,
    float& elapsed_milliseconds,
    const char* message) {
    out_concentration_a = concentration_a ? concentration_a : empty_amino_array<float>();
    out_concentration_b = concentration_b ? concentration_b : empty_amino_array<float>();
    pattern = empty_amino_array<float>();
    gradient_x = empty_amino_array<float>();
    gradient_y = empty_amino_array<float>();
    gradient_z = empty_amino_array<float>();
    backend_used = "ERROR";
    const std::string error_status = std::string("error: ") + message;
    status = error_status.c_str();
    elapsed_milliseconds = 0.0f;
}

std::vector<Core::SeedSample> make_grid_seeds(
    const Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& seed_u,
    const Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& seed_v,
    const Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& seed_radius,
    const Amino::Ptr<Takumi::ReactionDiffusion::FloatArray>& seed_strength,
    const Amino::Ptr<Takumi::ReactionDiffusion::IntArray>& seed_mode) {
    const std::size_t seed_count =
        std::min({array_size(seed_u), array_size(seed_v), array_size(seed_radius),
                  array_size(seed_strength)});
    std::vector<Core::SeedSample> seeds;
    seeds.reserve(seed_count);
    for (std::size_t index = 0; index < seed_count; ++index) {
        Core::SeedSample seed;
        seed.u = (*seed_u)[index];
        seed.v = (*seed_v)[index];
        seed.radius = (*seed_radius)[index];
        seed.strength = (*seed_strength)[index];
        seed.mode = seed_mode && index < seed_mode->size()
                        ? to_core_seed_mode((*seed_mode)[index])
                        : Core::SeedMode::AddB;
        seeds.push_back(seed);
    }
    return seeds;
}

} // namespace

namespace Takumi {
namespace ReactionDiffusion {

void reaction_diffusion_initialize_grid(
    int width,
    int height,
    Amino::Ptr<FloatArray>& concentration_a,
    Amino::Ptr<FloatArray>& concentration_b) {
    try {
        if (width < 3 || height < 3) {
            throw std::invalid_argument("width and height must be at least 3.");
        }
        Core::GridState state(static_cast<std::size_t>(width), static_cast<std::size_t>(height));
        concentration_a = to_amino_array(state.a);
        concentration_b = to_amino_array(state.b);
    } catch (...) {
        concentration_a = empty_amino_array<float>();
        concentration_b = empty_amino_array<float>();
    }
}

void reaction_diffusion_grid_step(
    Amino::Ptr<FloatArray> const& concentration_a,
    Amino::Ptr<FloatArray> const& concentration_b,
    int width,
    int height,
    float feed_rate,
    float kill_rate,
    float diffusion_a,
    float diffusion_b,
    float time_step,
    int substeps,
    BoundaryMode boundary_mode,
    Backend backend,
    Amino::Ptr<FloatArray> const& seed_u,
    Amino::Ptr<FloatArray> const& seed_v,
    Amino::Ptr<FloatArray> const& seed_radius,
    Amino::Ptr<FloatArray> const& seed_strength,
    Amino::Ptr<IntArray> const& seed_mode,
    Amino::Ptr<FloatArray>& out_concentration_a,
    Amino::Ptr<FloatArray>& out_concentration_b,
    Amino::Ptr<FloatArray>& pattern,
    Amino::Ptr<FloatArray>& gradient_u,
    Amino::Ptr<FloatArray>& gradient_v,
    Amino::String& backend_used,
    Amino::String& status,
    float& elapsed_milliseconds) {
    try {
        if (width < 3 || height < 3) {
            throw std::invalid_argument("width and height must be at least 3.");
        }
        const std::size_t expected =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (!concentration_a || !concentration_b || concentration_a->size() != expected ||
            concentration_b->size() != expected) {
            throw std::invalid_argument("A and B array lengths must equal width * height.");
        }

        Core::GridState state(static_cast<std::size_t>(width), static_cast<std::size_t>(height));
        state.a.assign(concentration_a->begin(), concentration_a->end());
        state.b.assign(concentration_b->begin(), concentration_b->end());

        Core::Parameters parameters;
        parameters.feed_rate = feed_rate;
        parameters.kill_rate = kill_rate;
        parameters.diffusion_a = diffusion_a;
        parameters.diffusion_b = diffusion_b;
        parameters.time_step = time_step;

        const std::size_t seed_count =
            std::min({array_size(seed_u), array_size(seed_v), array_size(seed_radius),
                      array_size(seed_strength)});
        std::vector<Core::SeedSample> seeds;
        seeds.reserve(seed_count);
        for (std::size_t index = 0; index < seed_count; ++index) {
            Core::SeedSample seed;
            seed.u = (*seed_u)[index];
            seed.v = (*seed_v)[index];
            seed.radius = (*seed_radius)[index];
            seed.strength = (*seed_strength)[index];
            seed.mode = seed_mode && index < seed_mode->size()
                            ? to_core_seed_mode((*seed_mode)[index])
                            : Core::SeedMode::AddB;
            seeds.push_back(seed);
        }

        const Core::StepResult result =
            Core::step(state, parameters, seeds, to_core_boundary(boundary_mode), substeps,
                       to_core_backend(backend), true);

        std::vector<float> pattern_values;
        std::vector<float> gradient_u_values;
        std::vector<float> gradient_v_values;
        Core::compute_outputs(state, to_core_boundary(boundary_mode), pattern_values,
                              gradient_u_values, gradient_v_values);

        out_concentration_a = to_amino_array(state.a);
        out_concentration_b = to_amino_array(state.b);
        pattern = to_amino_array(pattern_values);
        gradient_u = to_amino_array(gradient_u_values);
        gradient_v = to_amino_array(gradient_v_values);
        backend_used = result.actual_backend == Core::Backend::CPU ? "CPU" : "CUDA";
        status = result.status.c_str();
        elapsed_milliseconds = static_cast<float>(result.elapsed_milliseconds);
    } catch (const std::exception& exception) {
        set_grid_error_outputs(concentration_a, concentration_b, out_concentration_a,
                               out_concentration_b, pattern, gradient_u, gradient_v, backend_used,
                               status, elapsed_milliseconds, exception.what());
    } catch (...) {
        set_grid_error_outputs(concentration_a, concentration_b, out_concentration_a,
                               out_concentration_b, pattern, gradient_u, gradient_v, backend_used,
                               status, elapsed_milliseconds, "unknown native exception");
    }
}

void reaction_diffusion_initialize_state(
    int width,
    int height,
    BoundaryMode boundary_mode,
    Amino::Ptr<FloatArray> const& seed_u,
    Amino::Ptr<FloatArray> const& seed_v,
    Amino::Ptr<FloatArray> const& seed_radius,
    Amino::Ptr<FloatArray> const& seed_strength,
    Amino::Ptr<IntArray> const& seed_mode,
    Amino::Ptr<FloatArray>& state) {
    try {
        if (width < 3 || height < 3) {
            throw std::invalid_argument("width and height must be at least 3.");
        }
        Core::GridState grid(static_cast<std::size_t>(width), static_cast<std::size_t>(height));
        Core::apply_seeds(
            grid,
            make_grid_seeds(seed_u, seed_v, seed_radius, seed_strength, seed_mode),
            to_core_boundary(boundary_mode));
        state = to_amino_array(Core::pack_grid_state(grid));
    } catch (...) {
        state = empty_amino_array<float>();
    }
}

void reaction_diffusion_state_step(
    Amino::Ptr<FloatArray> const& state,
    int width,
    int height,
    float feed_rate,
    float kill_rate,
    float diffusion_a,
    float diffusion_b,
    float time_step,
    int substeps,
    BoundaryMode boundary_mode,
    Backend backend,
    Amino::Ptr<FloatArray> const& seed_u,
    Amino::Ptr<FloatArray> const& seed_v,
    Amino::Ptr<FloatArray> const& seed_radius,
    Amino::Ptr<FloatArray> const& seed_strength,
    Amino::Ptr<IntArray> const& seed_mode,
    Amino::Ptr<FloatArray>& out_state,
    Amino::String& backend_used,
    Amino::String& status,
    float& elapsed_milliseconds) {
    try {
        if (width < 3 || height < 3 || !state) {
            throw std::invalid_argument("A valid state and dimensions of at least 3x3 are required.");
        }
        const std::vector<float> packed(state->begin(), state->end());
        Core::GridState grid = Core::unpack_grid_state(
            packed, static_cast<std::size_t>(width), static_cast<std::size_t>(height));
        Core::Parameters parameters;
        parameters.feed_rate = feed_rate;
        parameters.kill_rate = kill_rate;
        parameters.diffusion_a = diffusion_a;
        parameters.diffusion_b = diffusion_b;
        parameters.time_step = time_step;
        const Core::StepResult result = Core::step(
            grid,
            parameters,
            make_grid_seeds(seed_u, seed_v, seed_radius, seed_strength, seed_mode),
            to_core_boundary(boundary_mode),
            substeps,
            to_core_backend(backend),
            true);
        out_state = to_amino_array(Core::pack_grid_state(grid));
        backend_used = result.actual_backend == Core::Backend::CPU ? "CPU" : "CUDA";
        status = result.status.c_str();
        elapsed_milliseconds = static_cast<float>(result.elapsed_milliseconds);
    } catch (const std::exception& exception) {
        out_state = state ? state : empty_amino_array<float>();
        backend_used = "ERROR";
        const std::string error_status = std::string("error: ") + exception.what();
        status = error_status.c_str();
        elapsed_milliseconds = 0.0f;
    } catch (...) {
        out_state = state ? state : empty_amino_array<float>();
        backend_used = "ERROR";
        status = "error: unknown native exception";
        elapsed_milliseconds = 0.0f;
    }
}

void reaction_diffusion_state_outputs(
    Amino::Ptr<FloatArray> const& state,
    int width,
    int height,
    BoundaryMode boundary_mode,
    Amino::Ptr<FloatArray>& concentration_a,
    Amino::Ptr<FloatArray>& concentration_b,
    Amino::Ptr<FloatArray>& pattern,
    Amino::Ptr<FloatArray>& gradient_u,
    Amino::Ptr<FloatArray>& gradient_v) {
    try {
        if (width < 3 || height < 3 || !state) {
            throw std::invalid_argument("A valid state and dimensions of at least 3x3 are required.");
        }
        const std::vector<float> packed(state->begin(), state->end());
        const Core::GridState grid = Core::unpack_grid_state(
            packed, static_cast<std::size_t>(width), static_cast<std::size_t>(height));
        std::vector<float> pattern_values;
        std::vector<float> gradient_u_values;
        std::vector<float> gradient_v_values;
        Core::compute_outputs(
            grid, to_core_boundary(boundary_mode), pattern_values,
            gradient_u_values, gradient_v_values);
        concentration_a = to_amino_array(grid.a);
        concentration_b = to_amino_array(grid.b);
        pattern = to_amino_array(pattern_values);
        gradient_u = to_amino_array(gradient_u_values);
        gradient_v = to_amino_array(gradient_v_values);
    } catch (...) {
        concentration_a = empty_amino_array<float>();
        concentration_b = empty_amino_array<float>();
        pattern = empty_amino_array<float>();
        gradient_u = empty_amino_array<float>();
        gradient_v = empty_amino_array<float>();
    }
}

void reaction_diffusion_initialize_volume(
    int width,
    int height,
    int depth,
    Amino::Ptr<FloatArray>& concentration_a,
    Amino::Ptr<FloatArray>& concentration_b) {
    try {
        if (width < 3 || height < 3 || depth < 3) {
            throw std::invalid_argument("width, height and depth must be at least 3.");
        }
        Core::VolumeState state(
            static_cast<std::size_t>(width),
            static_cast<std::size_t>(height),
            static_cast<std::size_t>(depth));
        concentration_a = to_amino_array(state.a);
        concentration_b = to_amino_array(state.b);
    } catch (...) {
        concentration_a = empty_amino_array<float>();
        concentration_b = empty_amino_array<float>();
    }
}

void reaction_diffusion_volume_step(
    Amino::Ptr<FloatArray> const& concentration_a,
    Amino::Ptr<FloatArray> const& concentration_b,
    int width,
    int height,
    int depth,
    float feed_rate,
    float kill_rate,
    float diffusion_a,
    float diffusion_b,
    float time_step,
    int substeps,
    BoundaryMode boundary_mode,
    Backend backend,
    Amino::Ptr<FloatArray> const& seed_u,
    Amino::Ptr<FloatArray> const& seed_v,
    Amino::Ptr<FloatArray> const& seed_w,
    Amino::Ptr<FloatArray> const& seed_radius,
    Amino::Ptr<FloatArray> const& seed_strength,
    Amino::Ptr<IntArray> const& seed_mode,
    Amino::Ptr<FloatArray>& out_concentration_a,
    Amino::Ptr<FloatArray>& out_concentration_b,
    Amino::Ptr<FloatArray>& pattern,
    Amino::Ptr<FloatArray>& gradient_x,
    Amino::Ptr<FloatArray>& gradient_y,
    Amino::Ptr<FloatArray>& gradient_z,
    Amino::String& backend_used,
    Amino::String& status,
    float& elapsed_milliseconds) {
    try {
        if (width < 3 || height < 3 || depth < 3) {
            throw std::invalid_argument("width, height and depth must be at least 3.");
        }
        Core::VolumeState state(static_cast<std::size_t>(width), static_cast<std::size_t>(height),
                                static_cast<std::size_t>(depth));
        if (!concentration_a || !concentration_b || concentration_a->size() != state.size() ||
            concentration_b->size() != state.size()) {
            throw std::invalid_argument("A and B array lengths must equal width * height * depth.");
        }
        state.a.assign(concentration_a->begin(), concentration_a->end());
        state.b.assign(concentration_b->begin(), concentration_b->end());

        Core::Parameters parameters;
        parameters.feed_rate = feed_rate;
        parameters.kill_rate = kill_rate;
        parameters.diffusion_a = diffusion_a;
        parameters.diffusion_b = diffusion_b;
        parameters.time_step = time_step;

        const std::size_t seed_count =
            std::min({array_size(seed_u), array_size(seed_v), array_size(seed_w),
                      array_size(seed_radius), array_size(seed_strength)});
        std::vector<Core::VolumeSeedSample> seeds;
        seeds.reserve(seed_count);
        for (std::size_t index = 0; index < seed_count; ++index) {
            Core::VolumeSeedSample seed;
            seed.u = (*seed_u)[index];
            seed.v = (*seed_v)[index];
            seed.w = (*seed_w)[index];
            seed.radius = (*seed_radius)[index];
            seed.strength = (*seed_strength)[index];
            seed.mode = seed_mode && index < seed_mode->size()
                            ? to_core_seed_mode((*seed_mode)[index])
                            : Core::SeedMode::AddB;
            seeds.push_back(seed);
        }

        const Core::StepResult result =
            Core::step_volume(state, parameters, seeds, to_core_boundary(boundary_mode), substeps,
                              to_core_backend(backend), true);

        std::vector<float> pattern_values;
        std::vector<float> gradient_x_values;
        std::vector<float> gradient_y_values;
        std::vector<float> gradient_z_values;
        Core::compute_volume_outputs(state, to_core_boundary(boundary_mode), pattern_values,
                                     gradient_x_values, gradient_y_values, gradient_z_values);

        out_concentration_a = to_amino_array(state.a);
        out_concentration_b = to_amino_array(state.b);
        pattern = to_amino_array(pattern_values);
        gradient_x = to_amino_array(gradient_x_values);
        gradient_y = to_amino_array(gradient_y_values);
        gradient_z = to_amino_array(gradient_z_values);
        backend_used = result.actual_backend == Core::Backend::CPU ? "CPU" : "CUDA";
        status = result.status.c_str();
        elapsed_milliseconds = static_cast<float>(result.elapsed_milliseconds);
    } catch (const std::exception& exception) {
        set_volume_error_outputs(concentration_a, concentration_b, out_concentration_a,
                                 out_concentration_b, pattern, gradient_x, gradient_y, gradient_z,
                                 backend_used, status, elapsed_milliseconds, exception.what());
    } catch (...) {
        set_volume_error_outputs(concentration_a, concentration_b, out_concentration_a,
                                 out_concentration_b, pattern, gradient_x, gradient_y, gradient_z,
                                 backend_used, status, elapsed_milliseconds,
                                 "unknown native exception");
    }
}

} // namespace ReactionDiffusion
} // namespace Takumi
