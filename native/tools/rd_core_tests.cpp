#include "ReactionDiffusionCore.h"
#include "ReactionDiffusionVolumeCore.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>

namespace RD = Takumi::ReactionDiffusionCore;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_uniform_state_is_stationary() {
    RD::GridState state(32, 32);
    RD::Parameters parameters;
    RD::step(state, parameters, {}, RD::BoundaryMode::Periodic, 10, RD::Backend::CPU);
    for (std::size_t i = 0; i < state.size(); ++i) {
        require(std::abs(state.a[i] - 1.0f) < 1.0e-6f, "Uniform A state changed.");
        require(std::abs(state.b[i]) < 1.0e-6f, "Uniform B state changed.");
    }
}

void test_seed_wraps_periodically() {
    RD::GridState state(64, 64);
    RD::SeedSample seed;
    seed.u = 0.995f;
    seed.v = 0.5f;
    seed.radius = 0.08f;
    seed.strength = 1.0f;
    RD::apply_seeds(state, {seed}, RD::BoundaryMode::Periodic);
    const std::size_t opposite_edge = 32 * state.width;
    require(state.b[opposite_edge] > 0.0f, "Periodic seed did not cross the U boundary.");
}

void test_seed_at_nonperiodic_upper_boundary() {
    RD::GridState state(32, 32);
    RD::SeedSample seed;
    seed.u = 1.0f;
    seed.v = 1.0f;
    seed.radius = 0.05f;
    RD::apply_seeds(state, {seed}, RD::BoundaryMode::NoFlux);
    require(state.b.back() > 0.9f, "Seed at UV 1,1 did not affect the last cell.");
}

void test_concentrations_remain_bounded() {
    RD::GridState state(96, 96);
    RD::Parameters parameters;
    RD::SeedSample seed;
    seed.radius = 0.05f;
    RD::step(state, parameters, {seed}, RD::BoundaryMode::Periodic, 120, RD::Backend::CPU);
    for (std::size_t i = 0; i < state.size(); ++i) {
        require(std::isfinite(state.a[i]) && std::isfinite(state.b[i]), "Non-finite concentration produced.");
        require(state.a[i] >= 0.0f && state.a[i] <= 1.0f, "A concentration escaped [0, 1].");
        require(state.b[i] >= 0.0f && state.b[i] <= 1.0f, "B concentration escaped [0, 1].");
    }
    const float total_b = std::accumulate(state.b.begin(), state.b.end(), 0.0f);
    require(total_b > 0.1f, "Seed disappeared unexpectedly.");
}

void test_deterministic_result() {
    RD::GridState first(80, 64);
    RD::GridState second(80, 64);
    RD::Parameters parameters;
    RD::SeedSample seed;
    seed.u = 0.21f;
    seed.v = 0.73f;
    seed.radius = 0.04f;
    RD::step(first, parameters, {seed}, RD::BoundaryMode::NoFlux, 80, RD::Backend::CPU);
    RD::step(second, parameters, {seed}, RD::BoundaryMode::NoFlux, 80, RD::Backend::CPU);
    require(first.a == second.a && first.b == second.b, "CPU solver is not deterministic.");
}

void test_erase_seed() {
    RD::GridState state(32, 32);
    RD::SeedSample add;
    add.radius = 0.1f;
    RD::apply_seeds(state, {add}, RD::BoundaryMode::NoFlux);
    const std::size_t center = 16 * state.width + 16;
    require(state.b[center] > 0.9f, "Add seed did not set B.");
    RD::SeedSample erase = add;
    erase.mode = RD::SeedMode::EraseB;
    RD::apply_seeds(state, {erase}, RD::BoundaryMode::NoFlux);
    require(state.b[center] < 0.1f, "Erase seed did not remove B.");
}

void test_output_sizes() {
    RD::GridState state(24, 18);
    std::vector<float> pattern;
    std::vector<float> gradient_u;
    std::vector<float> gradient_v;
    RD::compute_outputs(state, RD::BoundaryMode::Periodic, pattern, gradient_u, gradient_v);
    require(pattern.size() == state.size(), "Pattern output size mismatch.");
    require(gradient_u.size() == state.size(), "U gradient output size mismatch.");
    require(gradient_v.size() == state.size(), "V gradient output size mismatch.");
}

void test_packed_state_incremental_equivalence() {
    RD::GridState direct(48, 40);
    RD::GridState incremental(48, 40);
    RD::Parameters parameters;
    RD::SeedSample seed;
    seed.u = 0.41f;
    seed.v = 0.57f;
    seed.radius = 0.07f;
    RD::step(direct, parameters, {seed}, RD::BoundaryMode::Periodic, 90, RD::Backend::CPU);
    RD::step(incremental, parameters, {seed}, RD::BoundaryMode::Periodic, 45, RD::Backend::CPU);
    incremental = RD::unpack_grid_state(
        RD::pack_grid_state(incremental), incremental.width, incremental.height);
    RD::step(incremental, parameters, {}, RD::BoundaryMode::Periodic, 45, RD::Backend::CPU);
    require(direct.a == incremental.a && direct.b == incremental.b,
            "Packed feedback state changed the incremental result.");

    bool rejected = false;
    try {
        RD::unpack_grid_state(std::vector<float>(12, 0.0f), 8, 8);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Invalid packed state length was not rejected.");
}

void test_cuda_fallback_contract() {
#if !defined(RD_HAS_CUDA)
    require(!RD::cuda_backend_compiled(), "CPU-only build incorrectly reports compiled CUDA.");
    require(!RD::cuda_backend_available(), "CPU-only build incorrectly reports an available CUDA device.");
    RD::GridState state(16, 16);
    RD::Parameters parameters;
    const RD::StepResult fallback = RD::step(
        state,
        parameters,
        {},
        RD::BoundaryMode::Periodic,
        1,
        RD::Backend::CUDA,
        true);
    require(fallback.actual_backend == RD::Backend::CPU, "CUDA fallback did not use CPU.");
    require(
        fallback.status == "cuda_not_built_fallback_cpu",
        "CUDA fallback status does not explain that CUDA was not built.");
    bool rejected = false;
    try {
        RD::step(
            state,
            parameters,
            {},
            RD::BoundaryMode::Periodic,
            1,
            RD::Backend::CUDA,
            false);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "CUDA request without fallback was not rejected.");
#endif
}

void test_cuda_matches_cpu_when_available() {
#if defined(RD_HAS_CUDA)
    if (!RD::cuda_backend_available()) {
        return;
    }
    RD::GridState cpu_state(64, 48);
    RD::GridState cuda_state(64, 48);
    RD::Parameters parameters;
    RD::SeedSample seed;
    seed.u = 0.37f;
    seed.v = 0.61f;
    seed.radius = 0.08f;
    const RD::StepResult cpu_result = RD::step(
        cpu_state, parameters, {seed}, RD::BoundaryMode::Periodic, 80, RD::Backend::CPU, false);
    const RD::StepResult cuda_result = RD::step(
        cuda_state, parameters, {seed}, RD::BoundaryMode::Periodic, 80, RD::Backend::CUDA, false);
    require(cpu_result.actual_backend == RD::Backend::CPU, "CPU parity path used the wrong backend.");
    require(cuda_result.actual_backend == RD::Backend::CUDA, "CUDA parity path used the wrong backend.");
    float maximum_error = 0.0f;
    for (std::size_t index = 0; index < cpu_state.size(); ++index) {
        maximum_error = std::max(maximum_error, std::abs(cpu_state.a[index] - cuda_state.a[index]));
        maximum_error = std::max(maximum_error, std::abs(cpu_state.b[index] - cuda_state.b[index]));
    }
    require(maximum_error <= 5.0e-4f, "CUDA result differs from the CPU reference.");
    std::cout << "cudaMaximumError=" << maximum_error << "\n";

    RD::GridState auto_state(16, 16);
    const RD::StepResult auto_result = RD::step(
        auto_state, parameters, {}, RD::BoundaryMode::Periodic, 1, RD::Backend::Auto, false);
    require(auto_result.actual_backend == RD::Backend::CUDA, "Auto did not select CUDA.");
    require(auto_result.status == "auto_selected_cuda", "Auto CUDA status is incorrect.");
#endif
}

void test_uniform_volume_is_stationary() {
    RD::VolumeState state(12, 10, 8);
    RD::Parameters parameters;
    RD::step_volume(state, parameters, {}, RD::BoundaryMode::Periodic, 8, RD::Backend::CPU);
    for (std::size_t i = 0; i < state.size(); ++i) {
        require(std::abs(state.a[i] - 1.0f) < 1.0e-6f, "Uniform volume A state changed.");
        require(std::abs(state.b[i]) < 1.0e-6f, "Uniform volume B state changed.");
    }
}

void test_volume_seed_wraps_periodically() {
    RD::VolumeState state(24, 24, 24);
    RD::VolumeSeedSample seed;
    seed.u = 0.99f;
    seed.v = 0.5f;
    seed.w = 0.5f;
    seed.radius = 0.12f;
    RD::apply_volume_seeds(state, {seed}, RD::BoundaryMode::Periodic);
    const std::size_t opposite_edge = (12 * state.height + 12) * state.width;
    require(state.b[opposite_edge] > 0.0f, "Periodic volume seed did not cross the X boundary.");
}

void test_volume_concentrations_and_outputs() {
    RD::VolumeState state(20, 18, 16);
    RD::Parameters parameters;
    RD::VolumeSeedSample seed;
    seed.radius = 0.12f;
    RD::step_volume(state, parameters, {seed}, RD::BoundaryMode::NoFlux, 30, RD::Backend::CPU);
    for (std::size_t i = 0; i < state.size(); ++i) {
        require(std::isfinite(state.a[i]) && std::isfinite(state.b[i]),
            "Non-finite volume concentration produced.");
        require(state.a[i] >= 0.0f && state.a[i] <= 1.0f,
            "Volume A concentration escaped [0, 1].");
        require(state.b[i] >= 0.0f && state.b[i] <= 1.0f,
            "Volume B concentration escaped [0, 1].");
    }
    std::vector<float> pattern;
    std::vector<float> gradient_x;
    std::vector<float> gradient_y;
    std::vector<float> gradient_z;
    RD::compute_volume_outputs(
        state, RD::BoundaryMode::NoFlux, pattern, gradient_x, gradient_y, gradient_z);
    require(pattern.size() == state.size(), "Volume pattern output size mismatch.");
    require(gradient_x.size() == state.size(), "Volume X gradient output size mismatch.");
    require(gradient_y.size() == state.size(), "Volume Y gradient output size mismatch.");
    require(gradient_z.size() == state.size(), "Volume Z gradient output size mismatch.");
}

void test_volume_cuda_contract_is_explicit() {
    RD::VolumeState state(8, 8, 8);
    RD::Parameters parameters;
    const RD::StepResult fallback = RD::step_volume(
        state,
        parameters,
        {},
        RD::BoundaryMode::Periodic,
        1,
        RD::Backend::CUDA,
        true);
    require(fallback.actual_backend == RD::Backend::CPU, "Volume CUDA fallback did not use CPU.");
    require(
        fallback.status == "cuda_volume_not_implemented_fallback_cpu",
        "Volume CUDA fallback status is ambiguous.");
}

} // namespace

int main() {
    try {
        test_uniform_state_is_stationary();
        test_seed_wraps_periodically();
        test_seed_at_nonperiodic_upper_boundary();
        test_concentrations_remain_bounded();
        test_deterministic_result();
        test_erase_seed();
        test_output_sizes();
        test_packed_state_incremental_equivalence();
        test_cuda_fallback_contract();
        test_cuda_matches_cpu_when_available();
        test_uniform_volume_is_stationary();
        test_volume_seed_wraps_periodically();
        test_volume_concentrations_and_outputs();
        test_volume_cuda_contract_is_explicit();
        std::cout << "ReactionDiffusionCore tests: PASS\n";
        std::cout << "cudaBackendCompiled=" << (RD::cuda_backend_compiled() ? "true" : "false") << "\n";
        std::cout << "cudaBackendAvailable=" << (RD::cuda_backend_available() ? "true" : "false") << "\n";
#if defined(_OPENMP)
        std::cout << "openmpEnabled=true\n";
#else
        std::cout << "openmpEnabled=false\n";
#endif
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ReactionDiffusionCore tests: FAIL: " << exception.what() << "\n";
        return 1;
    }
}
