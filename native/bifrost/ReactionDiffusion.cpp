#include "ReactionDiffusion.h"
#include "ReactionDiffusionCore.h"
#include "ReactionDiffusionVolumeCore.h"
#include "ReactionDiffusionSurfaceCore.h"

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

void reaction_diffusion_samples(Amino::Ptr<FloatArray> const& pattern,
    Amino::Ptr<FloatArray> const& positions, int width,int height,int depth,
    float threshold,float point_radius,Amino::Ptr<VectorArray>& point_position,
    Amino::Ptr<FloatArray>& point_size,Amino::Ptr<FloatArray>& point_pattern) {
    try {
        if (!pattern || !std::isfinite(threshold) || !std::isfinite(point_radius) || point_radius<0)
            throw std::invalid_argument("Invalid sample parameters.");
        const bool surface=array_size(positions)>0;
        const auto count=pattern->size();
        if (surface ? array_size(positions)/3!=count || array_size(positions)%3 :
            width<1 || height<1 || depth<1 || count%width ||
            count/width%height || count/width/height!=static_cast<std::size_t>(depth))
            throw std::invalid_argument("Sample dimensions mismatch.");
        if (surface) for (float v : *positions)
            if (!std::isfinite(v)) throw std::invalid_argument("Non-finite position.");
        std::vector<Bifrost::Math::float3> points;
        std::vector<float> sizes,values;
        for (std::size_t i=0;i<pattern->size();++i) {
            const float value=(*pattern)[i];
            if (!std::isfinite(value)) throw std::invalid_argument("Non-finite sample.");
            if (value<threshold) continue;
            Bifrost::Math::float3 p;
            if (surface) { p.x=(*positions)[3*i];p.y=(*positions)[3*i+1];p.z=(*positions)[3*i+2]; }
            else { p.x=static_cast<float>(i%width)/std::max(1,width-1);
                   p.y=static_cast<float>((i/width)%height)/std::max(1,height-1);
                   p.z=static_cast<float>(i/(static_cast<std::size_t>(width)*height))/std::max(1,depth-1); }
            points.push_back(p);sizes.push_back(point_radius);values.push_back(value);
        }
        point_position=to_amino_array(points);point_size=to_amino_array(sizes);point_pattern=to_amino_array(values);
    } catch (...) {
        point_position=empty_amino_array<Bifrost::Math::float3>();
        point_size=empty_amino_array<float>();point_pattern=empty_amino_array<float>();
    }
}

void reaction_diffusion_mesh_data(Amino::Ptr<VectorArray> const& point_position,
    Amino::Ptr<Amino::Array<unsigned int>> const& face_vertex,
    Amino::Ptr<FloatArray>& positions, Amino::Ptr<IntArray>& triangles) {
    try {
        if (!point_position || !face_vertex || face_vertex->size()%3)
            throw std::invalid_argument("Triangulated mesh required.");
        std::vector<float> xyz;
        std::vector<int> indices;
        for (const auto& p : *point_position) {
            if (!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
                throw std::invalid_argument("Non-finite mesh position.");
            xyz.insert(xyz.end(),{p.x,p.y,p.z});
        }
        for (auto i : *face_vertex) {
            if (i>=point_position->size() || i>INT_MAX) throw std::invalid_argument("Mesh index out of range.");
            indices.push_back(static_cast<int>(i));
        }
        positions=to_amino_array(xyz);triangles=to_amino_array(indices);
    } catch (...) { positions=empty_amino_array<float>();triangles=empty_amino_array<int>(); }
}

void reaction_diffusion_pixels(Amino::Ptr<FloatArray> const& pattern,
    Amino::Ptr<Amino::Array<Bifrost::Math::float4>>& pixels) {
    try {
        if (!pattern) throw std::invalid_argument("Missing pattern.");
        std::vector<Bifrost::Math::float4> values;
        values.reserve(pattern->size());
        for (float v : *pattern) {
            if (!std::isfinite(v)) throw std::invalid_argument("Non-finite pixel.");
            Bifrost::Math::float4 p;
            p.x=p.y=p.z=v; p.w=1;
            values.push_back(p);
        }
        pixels=to_amino_array(values);
    } catch (...) { pixels=empty_amino_array<Bifrost::Math::float4>(); }
}

void reaction_diffusion_preset(int preset, float& feed_rate, float& kill_rate,
    float& diffusion_a, float& diffusion_b, bool& valid) {
    // TuringPattern_Generator/main/index.html, retrieved 2026-09-17.
    static const float table[5][4]={{.031f,.066f,1.10f,.40f}, {.0545f,.062f,1,.5f},
        {.010f,.055f,1.17f,.24f}, {.025f,.055f,1.10f,.39f}, {.032f,.059f,1.06f,.64f}};
    valid=preset>=0 && preset<5;
    if (!valid) { feed_rate=kill_rate=diffusion_a=diffusion_b=0; return; }
    feed_rate=table[preset][0]; kill_rate=table[preset][1];
    diffusion_a=table[preset][2]; diffusion_b=table[preset][3];
}

void reaction_diffusion_seed_events(float frame, Amino::Ptr<FloatArray> const& frames,
    Amino::Ptr<FloatArray> const& positions, Amino::Ptr<FloatArray> const& radii,
    Amino::Ptr<FloatArray> const& strengths, Amino::Ptr<IntArray> const& modes,
    Amino::Ptr<FloatArray>& seed_positions, Amino::Ptr<FloatArray>& seed_u,
    Amino::Ptr<FloatArray>& seed_v, Amino::Ptr<FloatArray>& seed_w,
    Amino::Ptr<FloatArray>& seed_radius, Amino::Ptr<FloatArray>& seed_strength,
    Amino::Ptr<IntArray>& seed_mode, Amino::String& status) {
    std::vector<float> xyz,u,v,w,r,s; std::vector<int> m;
    try {
        const auto n=array_size(frames);
        if (!std::isfinite(frame) || array_size(positions)!=3*n || array_size(radii)!=n ||
            array_size(strengths)!=n || array_size(modes)!=n) throw std::invalid_argument("Seed event lengths or frame invalid.");
        for (std::size_t i=0;i<n;++i) {
            if (!std::isfinite((*frames)[i]) || !std::isfinite((*radii)[i]) || (*radii)[i]<=0 ||
                !std::isfinite((*strengths)[i]) || (*modes)[i]<0 || (*modes)[i]>2 ||
                !std::isfinite((*positions)[3*i]) || !std::isfinite((*positions)[3*i+1]) || !std::isfinite((*positions)[3*i+2]))
                throw std::invalid_argument("Invalid seed event.");
            if (std::abs(frame-(*frames)[i])>1e-4f) continue;
            u.push_back((*positions)[3*i]); v.push_back((*positions)[3*i+1]); w.push_back((*positions)[3*i+2]);
            for (int d=0;d<3;++d) xyz.push_back((*positions)[3*i+d]);
            r.push_back((*radii)[i]); s.push_back((*strengths)[i]); m.push_back((*modes)[i]);
        }
        status="ok";
    } catch (const std::exception& e) {
        xyz.clear();u.clear();v.clear();w.clear();r.clear();s.clear();m.clear();
        status=(std::string("error: ")+e.what()).c_str();
    } catch (...) {
        xyz.clear();u.clear();v.clear();w.clear();r.clear();s.clear();m.clear(); status="error: seed events";
    }
    seed_positions=to_amino_array(xyz); seed_u=to_amino_array(u); seed_v=to_amino_array(v); seed_w=to_amino_array(w);
    seed_radius=to_amino_array(r); seed_strength=to_amino_array(s); seed_mode=to_amino_array(m);
}

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

void reaction_diffusion_surface_step(
    Amino::Ptr<FloatArray> const& positions, Amino::Ptr<IntArray> const& triangles,
    Amino::Ptr<FloatArray> const& concentration_a, Amino::Ptr<FloatArray> const& concentration_b,
    float feed_rate, float kill_rate, float diffusion_a, float diffusion_b,
    float time_step, int substeps, Backend backend,
    Amino::Ptr<FloatArray> const& seed_positions, Amino::Ptr<FloatArray> const& seed_radius,
    Amino::Ptr<FloatArray> const& seed_strength, Amino::Ptr<IntArray> const& seed_mode,
    Amino::Ptr<FloatArray>& out_concentration_a, Amino::Ptr<FloatArray>& out_concentration_b,
    Amino::Ptr<FloatArray>& pattern, Amino::Ptr<FloatArray>& gradient_x,
    Amino::Ptr<FloatArray>& gradient_y, Amino::Ptr<FloatArray>& gradient_z,
    Amino::String& backend_used, Amino::String& status, float& elapsed_milliseconds) {
    try {
        if (!positions || !triangles) throw std::invalid_argument("Surface geometry is required.");
        const auto mesh=Core::build_surface(
            std::vector<float>(positions->begin(),positions->end()),
            std::vector<int>(triangles->begin(),triangles->end()));
        Core::SurfaceState state;
        if (array_size(concentration_a)==0 && array_size(concentration_b)==0) {
            state.a.assign(mesh.positions.size(),1); state.b.assign(mesh.positions.size(),0);
        } else {
            if (!concentration_a || !concentration_b) throw std::invalid_argument("Both surface concentrations are required.");
            state.a.assign(concentration_a->begin(),concentration_a->end());
            state.b.assign(concentration_b->begin(),concentration_b->end());
        }
        const auto count=array_size(seed_radius);
        if (array_size(seed_positions)!=3*count || array_size(seed_strength)!=count ||
            (array_size(seed_mode)!=0 && array_size(seed_mode)!=count))
            throw std::invalid_argument("Surface seed array lengths mismatch.");
        std::vector<Core::SurfaceSeed> seeds;
        for (std::size_t i=0;i<count;++i) {
            Core::SurfaceSeed s;
            s.position={(*seed_positions)[3*i],(*seed_positions)[3*i+1],(*seed_positions)[3*i+2]};
            s.radius=(*seed_radius)[i]; s.strength=(*seed_strength)[i];
            s.mode=array_size(seed_mode) ? to_core_seed_mode((*seed_mode)[i]) : Core::SeedMode::AddB;
            seeds.push_back(s);
        }
        Core::Parameters p;
        p.feed_rate=feed_rate; p.kill_rate=kill_rate; p.diffusion_a=diffusion_a;
        p.diffusion_b=diffusion_b; p.time_step=time_step;
        const auto result=Core::step_surface(mesh,state,p,seeds,substeps,to_core_backend(backend));
        const auto gradient=Core::surface_gradient(mesh,state.b);
        std::vector<float> gx(state.b.size()),gy(gx.size()),gz(gx.size());
        for (std::size_t i=0;i<gx.size();++i) { gx[i]=gradient[3*i]; gy[i]=gradient[3*i+1]; gz[i]=gradient[3*i+2]; }
        out_concentration_a=to_amino_array(state.a); out_concentration_b=to_amino_array(state.b);
        pattern=out_concentration_b;
        gradient_x=to_amino_array(gx); gradient_y=to_amino_array(gy); gradient_z=to_amino_array(gz);
        backend_used=result.actual_backend==Core::Backend::CUDA ? "CUDA" : "CPU";
        status=result.status.c_str(); elapsed_milliseconds=static_cast<float>(result.elapsed_milliseconds);
    } catch (const std::exception& e) {
        set_volume_error_outputs(concentration_a,concentration_b,out_concentration_a,out_concentration_b,
            pattern,gradient_x,gradient_y,gradient_z,backend_used,status,elapsed_milliseconds,e.what());
    } catch (...) {
        set_volume_error_outputs(concentration_a,concentration_b,out_concentration_a,out_concentration_b,
            pattern,gradient_x,gradient_y,gradient_z,backend_used,status,elapsed_milliseconds,"unknown surface exception");
    }
}

void reaction_diffusion_pack_state(Amino::Ptr<FloatArray> const& a,
    Amino::Ptr<FloatArray> const& b, Amino::Ptr<FloatArray>& state) {
    try {
        if (!a || !b || a->size()!=b->size()) throw std::invalid_argument("A/B size mismatch.");
        std::vector<float> values(a->begin(),a->end());
        values.insert(values.end(),b->begin(),b->end());
        state=to_amino_array(values);
    } catch (...) { state=empty_amino_array<float>(); }
}
void reaction_diffusion_unpack_state(Amino::Ptr<FloatArray> const& state,
    Amino::Ptr<FloatArray>& a, Amino::Ptr<FloatArray>& b) {
    try {
        if (!state || state->size()%2) throw std::invalid_argument("Packed state size must be even.");
        std::vector<float> values(state->begin(),state->end());
        const auto middle=values.begin()+values.size()/2;
        a=to_amino_array(std::vector<float>(values.begin(),middle));
        b=to_amino_array(std::vector<float>(middle,values.end()));
    } catch (...) { a=empty_amino_array<float>(); b=empty_amino_array<float>(); }
}

} // namespace ReactionDiffusion
} // namespace Takumi
