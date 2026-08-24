#include "ReactionDiffusionCore.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace RD = Takumi::ReactionDiffusionCore;

namespace {

struct Measurements {
    double median_elapsed_ms = 0.0;
    double minimum_elapsed_ms = 0.0;
    double maximum_elapsed_ms = 0.0;
    double milliseconds_per_step = 0.0;
    double cells_per_second = 0.0;
};

Measurements measure_backend(
    int resolution,
    int measured_steps,
    int repeats,
    RD::Backend backend) {
    RD::GridState state(
        static_cast<std::size_t>(resolution), static_cast<std::size_t>(resolution));
    RD::Parameters parameters;
    RD::SeedSample seed;
    seed.radius = 0.02f;

    // Initialize the OpenMP pool or CUDA context outside the measured region.
    RD::step(state, parameters, {seed}, RD::BoundaryMode::Periodic, 2, backend, false);
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repeats));
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const RD::StepResult result = RD::step(
            state, parameters, {}, RD::BoundaryMode::Periodic, measured_steps, backend, false);
        if (result.actual_backend != backend) {
            throw std::runtime_error("Benchmark backend unexpectedly fell back.");
        }
        samples.push_back(result.elapsed_milliseconds);
    }
    std::sort(samples.begin(), samples.end());

    Measurements result;
    result.median_elapsed_ms = samples[samples.size() / 2];
    result.minimum_elapsed_ms = samples.front();
    result.maximum_elapsed_ms = samples.back();
    result.milliseconds_per_step =
        result.median_elapsed_ms / static_cast<double>(measured_steps);
    result.cells_per_second =
        (static_cast<double>(resolution) * static_cast<double>(resolution)) /
        (result.milliseconds_per_step * 0.001);
    return result;
}

void print_measurements(const char* prefix, const Measurements& measurements) {
    std::cout << prefix << "MedianElapsedMs=" << measurements.median_elapsed_ms << "\n";
    std::cout << prefix << "MinimumElapsedMs=" << measurements.minimum_elapsed_ms << "\n";
    std::cout << prefix << "MaximumElapsedMs=" << measurements.maximum_elapsed_ms << "\n";
    std::cout << prefix << "MillisecondsPerStep=" << measurements.milliseconds_per_step << "\n";
    std::cout << prefix << "CellsPerSecond=" << measurements.cells_per_second << "\n";
}

} // namespace

int main(int argc, char** argv) {
    const int resolution = argc > 1 ? std::max(32, std::atoi(argv[1])) : 1024;
    const int measured_steps = argc > 2 ? std::max(1, std::atoi(argv[2])) : 15;
    const int reference_steps_per_frame = argc > 3 ? std::max(1, std::atoi(argv[3])) : 15;
    const int repeats = argc > 4 ? std::max(3, std::atoi(argv[4])) : 7;

    const Measurements cpu = measure_backend(
        resolution, measured_steps, repeats, RD::Backend::CPU);
    const double cpu_projected_frame_ms =
        cpu.milliseconds_per_step * static_cast<double>(reference_steps_per_frame);

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "ReactionDiffusionCore benchmark\n";
    std::cout << "resolution=" << resolution << "x" << resolution << "\n";
    std::cout << "measuredSteps=" << measured_steps << "\n";
    std::cout << "repeats=" << repeats << "\n";
    print_measurements("cpu", cpu);
    std::cout << "cpuProjected" << reference_steps_per_frame
              << "StepFrameMs=" << cpu_projected_frame_ms << "\n";
    std::cout << "cudaBackendCompiled=" << (RD::cuda_backend_compiled() ? "true" : "false") << "\n";
    std::cout << "cudaBackendAvailable=" << (RD::cuda_backend_available() ? "true" : "false") << "\n";
    if (RD::cuda_backend_available()) {
        const Measurements cuda = measure_backend(
            resolution, measured_steps, repeats, RD::Backend::CUDA);
        const double cuda_projected_frame_ms =
            cuda.milliseconds_per_step * static_cast<double>(reference_steps_per_frame);
        print_measurements("cuda", cuda);
        std::cout << "cudaProjected" << reference_steps_per_frame
                  << "StepFrameMs=" << cuda_projected_frame_ms << "\n";
        std::cout << "cudaSpeedup="
                  << cpu.milliseconds_per_step / cuda.milliseconds_per_step << "\n";
    }
#if defined(_OPENMP)
    std::cout << "openmpEnabled=true\n";
#else
    std::cout << "openmpEnabled=false\n";
#endif
    return 0;
}
