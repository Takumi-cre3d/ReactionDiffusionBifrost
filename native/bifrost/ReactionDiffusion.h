#ifndef TAKUMI_REACTION_DIFFUSION_BIFROST_H
#define TAKUMI_REACTION_DIFFUSION_BIFROST_H

// Bifrost 2.15's cpp2json tool embeds an older Clang frontend.  When Visual
// Studio 2022 17.14 (MSVC 14.44) is installed, that frontend reads the current
// MSVC STL headers and yvals_core.h rejects it with STL1000 before it can parse
// this operator declaration.  The operator itself is still compiled by MSVC;
// this opt-in only lets cpp2json inspect the declarations with the SDK parser.
#if defined(__clang__) && \
    !defined(_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
#define _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH
#endif

#include <Amino/Core/Array.h>
#include <Amino/Core/Ptr.h>
#include <Amino/Core/String.h>
#include <Amino/Cpp/Annotate.h>

// Export the operator entry points directly. The Bifrost SDK sample normally
// supplies this through a generated *Export.h header, but making it explicit
// prevents a generated-header/target-definition mismatch from producing a DLL
// whose JSON definitions are visible while its operator symbols are not.
#if defined(_WIN32)
#define REACTION_DIFFUSION_NODE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define REACTION_DIFFUSION_NODE_EXPORT __attribute__((visibility("default")))
#else
#define REACTION_DIFFUSION_NODE_EXPORT
#endif

namespace Takumi {
namespace ReactionDiffusion {

enum class AMINO_ANNOTATE("Amino::Enum") BoundaryMode {
    Periodic,
    NoFlux,
    FixedInitial
};

enum class AMINO_ANNOTATE("Amino::Enum") Backend {
    Auto,
    CPU,
    CUDA
};

enum class AMINO_ANNOTATE("Amino::Enum") SeedMode {
    AddB,
    EraseB,
    SetB
};

using FloatArray = Amino::Array<float>;
using IntArray = Amino::Array<int>;

REACTION_DIFFUSION_NODE_EXPORT void reaction_diffusion_initialize_grid(
    int width,
    int height,
    Amino::Ptr<FloatArray>& concentration_a,
    Amino::Ptr<FloatArray>& concentration_b)
    AMINO_ANNOTATE("Amino::Node");

REACTION_DIFFUSION_NODE_EXPORT void reaction_diffusion_grid_step(
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
    float& elapsed_milliseconds)
    AMINO_ANNOTATE("Amino::Node");

REACTION_DIFFUSION_NODE_EXPORT void reaction_diffusion_initialize_state(
    int width,
    int height,
    BoundaryMode boundary_mode,
    Amino::Ptr<FloatArray> const& seed_u,
    Amino::Ptr<FloatArray> const& seed_v,
    Amino::Ptr<FloatArray> const& seed_radius,
    Amino::Ptr<FloatArray> const& seed_strength,
    Amino::Ptr<IntArray> const& seed_mode,
    Amino::Ptr<FloatArray>& state)
    AMINO_ANNOTATE("Amino::Node");

REACTION_DIFFUSION_NODE_EXPORT void reaction_diffusion_state_step(
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
    float& elapsed_milliseconds)
    AMINO_ANNOTATE("Amino::Node");

REACTION_DIFFUSION_NODE_EXPORT void reaction_diffusion_state_outputs(
    Amino::Ptr<FloatArray> const& state,
    int width,
    int height,
    BoundaryMode boundary_mode,
    Amino::Ptr<FloatArray>& concentration_a,
    Amino::Ptr<FloatArray>& concentration_b,
    Amino::Ptr<FloatArray>& pattern,
    Amino::Ptr<FloatArray>& gradient_u,
    Amino::Ptr<FloatArray>& gradient_v)
    AMINO_ANNOTATE("Amino::Node");

REACTION_DIFFUSION_NODE_EXPORT void reaction_diffusion_initialize_volume(
    int width,
    int height,
    int depth,
    Amino::Ptr<FloatArray>& concentration_a,
    Amino::Ptr<FloatArray>& concentration_b)
    AMINO_ANNOTATE("Amino::Node");

REACTION_DIFFUSION_NODE_EXPORT void reaction_diffusion_volume_step(
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
    float& elapsed_milliseconds)
    AMINO_ANNOTATE("Amino::Node");

} // namespace ReactionDiffusion
} // namespace Takumi

#endif // TAKUMI_REACTION_DIFFUSION_BIFROST_H
