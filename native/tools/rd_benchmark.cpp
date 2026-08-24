#include "ReactionDiffusionCore.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace RD = Takumi::ReactionDiffusionCore;

int main(int argc, char** argv) {
    const int resolution = argc > 1 ? std::max(32, std::atoi(argv[1])) : 1024;
    const int measured_steps = argc > 2 ? std::max(1, std::atoi(argv[2])) : 15;
    const int reference_steps_per_frame = argc > 3 ? std::max(1, std::atoi(argv[3])) : 15;
    const int repeats = argc > 4 ? std::max(3, std::atoi(argv[4])) : 7;

    RD::GridState state(static_cast<std::size_t>(resolution), static_cast<std::size_t>(resolution));
    RD::Parameters parameters;
    RD::SeedSample seed;
    seed.radius = 0.02f;

    RD::step(state, parameters, {seed}, RD::BoundaryMode::Periodic, 2, RD::Backend::CPU);
    std::vector<double> measurements;
    measurements.reserve(static_cast<std::size_t>(repeats));
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const RD::StepResult result = RD::step(
            state, parameters, {}, RD::BoundaryMode::Periodic, measured_steps, RD::Backend::CPU);
        measurements.push_back(result.elapsed_milliseconds);
    }
    std::sort(measurements.begin(), measurements.end());
    const double median_elapsed_ms = measurements[measurements.size() / 2];
    const double minimum_elapsed_ms = measurements.front();
    const double maximum_elapsed_ms = measurements.back();
    const double milliseconds_per_step = median_elapsed_ms / static_cast<double>(measured_steps);
    const double projected_frame_ms = milliseconds_per_step * static_cast<double>(reference_steps_per_frame);
    const double cells_per_second =
        (static_cast<double>(resolution) * static_cast<double>(resolution)) /
        (milliseconds_per_step * 0.001);

    std::string priority;
    if (projected_frame_ms > 100.0) {
        priority = "P0: CUDA should be implemented before broad visual validation.";
    } else if (projected_frame_ms > 33.0) {
        priority = "P1: CUDA should be the next milestone after numerical validation.";
    } else {
        priority = "P2: CPU is sufficient for early validation; CUDA can follow surface mode.";
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "ReactionDiffusionCore benchmark\n";
    std::cout << "resolution=" << resolution << "x" << resolution << "\n";
    std::cout << "measuredSteps=" << measured_steps << "\n";
    std::cout << "repeats=" << repeats << "\n";
    std::cout << "medianElapsedMs=" << median_elapsed_ms << "\n";
    std::cout << "minimumElapsedMs=" << minimum_elapsed_ms << "\n";
    std::cout << "maximumElapsedMs=" << maximum_elapsed_ms << "\n";
    std::cout << "millisecondsPerStep=" << milliseconds_per_step << "\n";
    std::cout << "cellsPerSecond=" << cells_per_second << "\n";
    std::cout << "projected" << reference_steps_per_frame << "StepFrameMs=" << projected_frame_ms << "\n";
    std::cout << "cudaPriority=" << priority << "\n";
#if defined(_OPENMP)
    std::cout << "openmpEnabled=true\n";
#else
    std::cout << "openmpEnabled=false\n";
#endif
    return 0;
}
