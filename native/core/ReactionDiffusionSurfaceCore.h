#ifndef TAKUMI_REACTION_DIFFUSION_SURFACE_CORE_H
#define TAKUMI_REACTION_DIFFUSION_SURFACE_CORE_H

#include "ReactionDiffusionCore.h"
#include <array>
#include <map>
#include <climits>

namespace Takumi { namespace ReactionDiffusionCore {

using Vec3 = std::array<double, 3>;
inline Vec3 subtract(const Vec3& a, const Vec3& b) {
    return {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
}
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
inline double dot(const Vec3& a, const Vec3& b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}

// Lumped mass inverse times cotangent stiffness in CSR form. Open boundaries
// use the natural (no-flux) FEM condition. UV seams do not change connectivity.
struct SurfaceTopology {
    std::vector<Vec3> positions;
    std::vector<int> triangles, offsets, neighbours;
    std::vector<float> weights;
    std::vector<double> mass;
    float max_row_sum = 0;
};

inline SurfaceTopology build_surface(const std::vector<float>& xyz,
                                    const std::vector<int>& triangles) {
    if (xyz.empty() || xyz.size()%3 || triangles.empty() || triangles.size()%3 || xyz.size()/3 > INT_MAX)
        throw std::invalid_argument("Surface requires XYZ triples and triangle index triples.");
    SurfaceTopology mesh;
    const auto n = xyz.size()/3;
    mesh.positions.resize(n); mesh.mass.resize(n); mesh.triangles = triangles;
    std::vector<std::map<int, double>> rows(n);
    std::map<std::pair<int,int>, int> edge_count;
    for (std::size_t i=0; i<n; ++i) for (int c=0; c<3; ++c) {
        if (!std::isfinite(xyz[3*i+c])) throw std::invalid_argument("Non-finite surface position.");
        mesh.positions[i][c] = xyz[3*i+c];
    }
    for (std::size_t f=0; f<triangles.size(); f+=3) {
        for (int k=0; k<3; ++k) if (triangles[f+k]<0 || static_cast<std::size_t>(triangles[f+k])>=n)
            throw std::invalid_argument("Surface index out of range.");
        const int a=triangles[f], b=triangles[f+1], c=triangles[f+2];
        const auto normal = cross(subtract(mesh.positions[b],mesh.positions[a]), subtract(mesh.positions[c],mesh.positions[a]));
        const double twice_area = std::sqrt(dot(normal,normal));
        if (!(twice_area > 1e-20)) throw std::invalid_argument("Degenerate surface triangle.");
        for (int k=0; k<3; ++k) {
            const int i=triangles[f+k], j=triangles[f+(k+1)%3], v=triangles[f+(k+2)%3];
            if (++edge_count[std::minmax(i,j)] > 2) throw std::invalid_argument("Non-manifold surface edge.");
            const double w=0.5*dot(subtract(mesh.positions[i],mesh.positions[v]), subtract(mesh.positions[j],mesh.positions[v]))/twice_area;
            rows[i][j]+=w; rows[j][i]+=w;
            mesh.mass[i]+=twice_area/6.0;
        }
    }
    mesh.offsets.push_back(0);
    for (std::size_t i=0; i<n; ++i) {
        if (!(mesh.mass[i]>0)) throw std::invalid_argument("Isolated surface vertex.");
        double row_sum=0;
        for (const auto& entry: rows[i]) {
            const double weight=entry.second/mesh.mass[i];
            if (!std::isfinite(weight) || std::abs(weight)>1e20) throw std::invalid_argument("Ill-conditioned surface.");
            mesh.neighbours.push_back(entry.first); mesh.weights.push_back(static_cast<float>(weight));
            row_sum+=std::abs(weight);
        }
        if (mesh.neighbours.size()>INT_MAX) throw std::invalid_argument("Surface adjacency too large.");
        mesh.offsets.push_back(static_cast<int>(mesh.neighbours.size()));
        mesh.max_row_sum=std::max(mesh.max_row_sum,static_cast<float>(row_sum));
    }
    return mesh;
}

struct SurfaceState { std::vector<float> a, b; };
struct SurfaceSeed {
    Vec3 position{0,0,0};
    float radius=1, strength=1;
    SeedMode mode=SeedMode::AddB;
};
inline void validate_surface_state(const SurfaceTopology& mesh, const SurfaceState& state) {
    if (state.a.size()!=mesh.positions.size() || state.b.size()!=state.a.size())
        throw std::invalid_argument("Surface concentration length must equal vertex count.");
    for (std::size_t i=0; i<state.a.size(); ++i)
        if (!std::isfinite(state.a[i]) || !std::isfinite(state.b[i])) throw std::invalid_argument("Non-finite surface state.");
}
inline void apply_surface_seeds(const SurfaceTopology& mesh, SurfaceState& state, const std::vector<SurfaceSeed>& seeds) {
    validate_surface_state(mesh,state);
    for (const auto& seed: seeds) {
        if (!std::isfinite(seed.radius) || seed.radius<=0 || !std::isfinite(seed.strength) ||
            !std::isfinite(seed.position[0]) || !std::isfinite(seed.position[1]) || !std::isfinite(seed.position[2]))
            throw std::invalid_argument("Invalid surface seed.");
        for (std::size_t i=0;i<state.b.size();++i) {
            const auto delta=subtract(mesh.positions[i],seed.position);
            const float falloff=Detail::clamp01(1-static_cast<float>(std::sqrt(dot(delta,delta)))/seed.radius);
            const float strength=Detail::clamp01(seed.strength), amount=strength*falloff;
            if (seed.mode==SeedMode::AddB) state.b[i]=std::max(state.b[i],amount);
            else if (seed.mode==SeedMode::EraseB) state.b[i]*=1-amount;
            else state.b[i]+=(strength-state.b[i])*falloff;
        }
    }
}
inline int surface_splits(const SurfaceTopology& mesh, const Parameters& p) {
    // Preserve elapsed simulation time while subdividing the explicit diffusion step.
    const double value=std::max(1.0, std::ceil(2.0*p.time_step*std::max(p.diffusion_a,p.diffusion_b)*mesh.max_row_sum));
    if (!std::isfinite(value) || value>100000) throw std::invalid_argument("Surface timestep requires too many subdivisions; reduce time_step or rescale mesh.");
    return static_cast<int>(value);
}
#if defined(RD_HAS_CUDA)
StepResult step_cuda_surface(const SurfaceTopology&, SurfaceState&, const Parameters&, int, Backend);
#endif
inline StepResult step_surface(const SurfaceTopology& mesh, SurfaceState& state,
    const Parameters& p, const std::vector<SurfaceSeed>& seeds, int substeps,
    Backend backend=Backend::Auto, bool fallback=true) {
    Detail::validate_parameters(p);
    if (substeps<0) throw std::invalid_argument("Negative substeps.");
    validate_surface_state(mesh,state);
    const int splits=surface_splits(mesh,p);
    if (substeps>INT_MAX/splits) throw std::invalid_argument("Surface iteration overflow.");
    if (backend==Backend::CUDA && !cuda_backend_available() && !fallback)
        throw std::runtime_error("CUDA surface backend unavailable.");
    apply_surface_seeds(mesh,state,seeds);
#if defined(RD_HAS_CUDA)
    if (backend!=Backend::CPU && cuda_backend_available()) return step_cuda_surface(mesh,state,p,substeps,backend);
#endif
    const auto started=std::chrono::steady_clock::now();
    std::vector<float> next_a(state.a.size()), next_b(state.b.size());
    const float dt=p.time_step/static_cast<float>(splits);
    for (int step=0;step<substeps*splits;++step) {
        for (std::size_t i=0;i<state.a.size();++i) {
            float la=0,lb=0;
            for (int e=mesh.offsets[i];e<mesh.offsets[i+1];++e) {
                const int j=mesh.neighbours[e];
                la+=mesh.weights[e]*(state.a[j]-state.a[i]);
                lb+=mesh.weights[e]*(state.b[j]-state.b[i]);
            }
            const float a=state.a[i], b=state.b[i], reaction=a*b*b;
            next_a[i]=a+(p.diffusion_a*la-reaction+p.feed_rate*(1-a))*dt;
            next_b[i]=b+(p.diffusion_b*lb+reaction-(p.feed_rate+p.kill_rate)*b)*dt;
            if (p.clamp_concentrations) { next_a[i]=Detail::clamp01(next_a[i]); next_b[i]=Detail::clamp01(next_b[i]); }
        }
        state.a.swap(next_a); state.b.swap(next_b);
    }
    StepResult result;
    result.requested_backend=backend; result.actual_backend=Backend::CPU; result.substeps=substeps;
    result.status=backend==Backend::CUDA ? "cuda_unavailable_fallback_cpu" : "cpu_surface";
    result.elapsed_milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    return result;
}

inline std::vector<float> surface_gradient(const SurfaceTopology& mesh, const std::vector<float>& values) {
    if (values.size()!=mesh.positions.size()) throw std::invalid_argument("Surface gradient size mismatch.");
    std::vector<float> out(3*values.size(),0);
    for (std::size_t f=0;f<mesh.triangles.size();f+=3) {
        const int a=mesh.triangles[f],b=mesh.triangles[f+1],c=mesh.triangles[f+2];
        const auto n=cross(subtract(mesh.positions[b],mesh.positions[a]), subtract(mesh.positions[c],mesh.positions[a]));
        const double n2=dot(n,n), area=std::sqrt(n2)/2;
        Vec3 gradient{0,0,0};
        for (int k=0;k<3;++k) {
            const int i=mesh.triangles[f+k],j=mesh.triangles[f+(k+1)%3],v=mesh.triangles[f+(k+2)%3];
            const auto g=cross(n,subtract(mesh.positions[v],mesh.positions[j]));
            for (int d=0;d<3;++d) gradient[d]+=values[i]*g[d]/n2;
        }
        for (int k=0;k<3;++k) {
            const int i=mesh.triangles[f+k];
            for (int d=0;d<3;++d) out[3*i+d]+=static_cast<float>(gradient[d]*area/(3*mesh.mass[i]));
        }
    }
    return out;
}
}} // namespace
#endif
