#include "ReactionDiffusionCore.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace RD = Takumi::ReactionDiffusionCore;

int main(int argc, char** argv) {
    const int resolution = argc > 1 ? std::max(32, std::atoi(argv[1])) : 512;
    const int steps = argc > 2 ? std::max(1, std::atoi(argv[2])) : 1000;
    const std::string output = argc > 3 ? argv[3] : "reaction_diffusion_reference.pgm";

    try {
        RD::GridState state(static_cast<std::size_t>(resolution), static_cast<std::size_t>(resolution));
        RD::Parameters parameters;
        RD::SeedSample center;
        center.radius = 0.025f;
        RD::SeedSample offset = center;
        offset.u = 0.53f;
        offset.v = 0.47f;
        RD::step(state, parameters, {center, offset}, RD::BoundaryMode::Periodic, steps, RD::Backend::CPU);

        std::ofstream stream(output, std::ios::binary);
        if (!stream) {
            throw std::runtime_error("Could not open output image.");
        }
        stream << "P5\n" << resolution << " " << resolution << "\n255\n";
        for (const float value : state.b) {
            const float contrast = std::max(0.0f, std::min(1.0f, (value - 0.1f) / 0.25f));
            const unsigned char pixel = static_cast<unsigned char>(contrast * 255.0f + 0.5f);
            stream.write(reinterpret_cast<const char*>(&pixel), 1);
        }
        std::cout << "Wrote " << output << " at " << resolution << "x" << resolution
                  << " after " << steps << " steps.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Reference image generation failed: " << exception.what() << "\n";
        return 1;
    }
}
