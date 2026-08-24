#include "ReactionDiffusion.h"
#include "ReactionDiffusionCore.h"
#include "ReactionDiffusionVolumeCore.h"

#include <algorithm>
#include <stdexcept>
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

} // namespace

namespace Takumi {
namespace ReactionDiffusion {

void reaction_diffusion_initialize_grid(
    int width,
    int height,
    Amino::Ptr<FloatArray>& concentration_a,
    Amino::Ptr<FloatArray>& concentration_b) {
    if (width < 3 || height < 3) {
        throw std::invalid_argument("width and height must be at least 3.");
    }
    Core::GridState state(static_cast<std::size_t>(width), static_cast<std::size_t>(height));
    concentration_a = to_amino_array(state.a);
    concentration_b = to_amino_array(state.b);
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
    if (width < 3 || height < 3) {
        throw std::invalid_argument("width and height must be at least 3.");
    }
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (!concentration_a || !concentration_b ||
        concentration_a->size() != expected || concentration_b->size() != expected) {
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

    const std::size_t seed_count = std::min({
        array_size(seed_u),
        array_size(seed_v),
        array_size(seed_radius),
        array_size(seed_strength)
    });
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

    const Core::StepResult result = Core::step(
        state,
        parameters,
        seeds,
        to_core_boundary(boundary_mode),
        substeps,
        to_core_backend(backend),
        true);

    std::vector<float> pattern_values;
    std::vector<float> gradient_u_values;
    std::vector<float> gradient_v_values;
    Core::compute_outputs(
        state,
        to_core_boundary(boundary_mode),
        pattern_values,
        gradient_u_values,
        gradient_v_values);

    out_concentration_a = to_amino_array(state.a);
    out_concentration_b = to_amino_array(state.b);
    pattern = to_amino_array(pattern_values);
    gradient_u = to_amino_array(gradient_u_values);
    gradient_v = to_amino_array(gradient_v_values);
    backend_used = result.actual_backend == Core::Backend::CPU ? "CPU" : "CUDA";
    status = result.status.c_str();
    elapsed_milliseconds = static_cast<float>(result.elapsed_milliseconds);
}

void reaction_diffusion_initialize_volume(
    int width,
    int height,
    int depth,
    Amino::Ptr<FloatArray>& concentration_a,
    Amino::Ptr<FloatArray>& concentration_b) {
    if (width < 3 || height < 3 || depth < 3) {
        throw std::invalid_argument("width, height and depth must be at least 3.");
    }
    Core::VolumeState state(
        static_cast<std::size_t>(width),
        static_cast<std::size_t>(height),
        static_cast<std::size_t>(depth));
    concentration_a = to_amino_array(state.a);
    concentration_b = to_amino_array(state.b);
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
    if (width < 3 || height < 3 || depth < 3) {
        throw std::invalid_argument("width, height and depth must be at least 3.");
    }
    Core::VolumeState state(
        static_cast<std::size_t>(width),
        static_cast<std::size_t>(height),
        static_cast<std::size_t>(depth));
    if (!concentration_a || !concentration_b ||
        concentration_a->size() != state.size() || concentration_b->size() != state.size()) {
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

    const std::size_t seed_count = std::min({
        array_size(seed_u),
        array_size(seed_v),
        array_size(seed_w),
        array_size(seed_radius),
        array_size(seed_strength)
    });
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

    const Core::StepResult result = Core::step_volume(
        state,
        parameters,
        seeds,
        to_core_boundary(boundary_mode),
        substeps,
        to_core_backend(backend),
        true);

    std::vector<float> pattern_values;
    std::vector<float> gradient_x_values;
    std::vector<float> gradient_y_values;
    std::vector<float> gradient_z_values;
    Core::compute_volume_outputs(
        state,
        to_core_boundary(boundary_mode),
        pattern_values,
        gradient_x_values,
        gradient_y_values,
        gradient_z_values);

    out_concentration_a = to_amino_array(state.a);
    out_concentration_b = to_amino_array(state.b);
    pattern = to_amino_array(pattern_values);
    gradient_x = to_amino_array(gradient_x_values);
    gradient_y = to_amino_array(gradient_y_values);
    gradient_z = to_amino_array(gradient_z_values);
    backend_used = result.actual_backend == Core::Backend::CPU ? "CPU" : "CUDA";
    status = result.status.c_str();
    elapsed_milliseconds = static_cast<float>(result.elapsed_milliseconds);
}

} // namespace ReactionDiffusion
} // namespace Takumi
