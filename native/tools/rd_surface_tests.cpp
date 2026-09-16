#include "ReactionDiffusionSurfaceCore.h"
#include <iostream>
namespace RD=Takumi::ReactionDiffusionCore;
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        const std::vector<float> xyz={0,0,0, 1,0,0, 1,1,0, 0,1,0};
        const std::vector<int> faces={0,1,2, 0,2,3};
        const auto mesh=RD::build_surface(xyz,faces);
        auto gradient=RD::surface_gradient(mesh,{0,1,1,0});
        for (int i=0;i<4;++i) {
            check(std::abs(gradient[3*i]-1)<1e-6f,"Linear field gradient X mismatch");
            check(std::abs(gradient[3*i+1])<1e-6f && std::abs(gradient[3*i+2])<1e-6f,"Non-tangent gradient");
        }
        RD::Parameters p;
        RD::SurfaceState uniform{{1,1,1,1},{0,0,0,0}};
        RD::step_surface(mesh,uniform,p,{},4,RD::Backend::CPU);
        check(uniform.a==std::vector<float>(4,1) && uniform.b==std::vector<float>(4,0),"Uniform surface drift");
        RD::SurfaceSeed seed; seed.radius=1.2f;
        auto cpu=uniform, split=uniform;
        RD::step_surface(mesh,cpu,p,{seed},12,RD::Backend::CPU);
        RD::step_surface(mesh,split,p,{seed},6,RD::Backend::CPU);
        RD::step_surface(mesh,split,p,{},6,RD::Backend::CPU);
        check(cpu.a==split.a && cpu.b==split.b,"Surface feedback split mismatch");
        if (RD::cuda_backend_available()) {
            auto gpu=uniform;
            const auto result=RD::step_surface(mesh,gpu,p,{seed},12,RD::Backend::CUDA,false);
            check(result.actual_backend==RD::Backend::CUDA,"Surface CUDA not selected");
            float error=0;
            for (int i=0;i<4;++i) { error=std::max(error,std::abs(cpu.a[i]-gpu.a[i])); error=std::max(error,std::abs(cpu.b[i]-gpu.b[i])); }
            check(error<2e-5f,"Surface CUDA parity mismatch");
            std::cout << "surfaceCudaMaximumError=" << error << '\n';
        }
        auto erased=uniform;
        RD::apply_surface_seeds(mesh,erased,{seed});
        seed.mode=RD::SeedMode::EraseB;
        RD::apply_surface_seeds(mesh,erased,{seed});
        check(erased.b[0]==0,"Surface erase seed failed");
        for (const auto& invalid: std::vector<std::vector<int>>{{0,1,7},{0,0,1},{0,1}}) {
            bool rejected=false;
            try { RD::build_surface(xyz,invalid); } catch (const std::invalid_argument&) { rejected=true; }
            check(rejected,"Invalid mesh accepted");
        }
        // A translated/deformed mesh retains the vertex-indexed state.
        auto moved=xyz; for (std::size_t i=2;i<moved.size();i+=3) moved[i]=2;
        auto deformed=RD::build_surface(moved,faces);
        auto continued=split;
        RD::step_surface(deformed,continued,p,{},1,RD::Backend::CPU);
        RD::step_surface(mesh,split,p,{},1,RD::Backend::CPU);
        check(continued.a==split.a && continued.b==split.b,"Translation changed solver");
        // Curved, irregular triangles exercise signed cotangents and nonuniform mass.
        std::vector<float> curved;
        std::vector<int> cells;
        for (int y=0;y<9;++y) for (int x=0;x<11;++x) {
            curved.insert(curved.end(),{x*.4f+(y%2)*.07f,y*.35f,.2f*std::sin(x*.7f)*std::cos(y*.6f)});
            if (x<10 && y<8) {
                const int i=y*11+x;
                cells.insert(cells.end(),{i,i+1,i+12,i,i+12,i+11});
            }
        }
        const auto curved_mesh=RD::build_surface(curved,cells);
        RD::SurfaceState curved_cpu;
        curved_cpu.a.assign(99,1);curved_cpu.b.assign(99,0);
        auto curved_gpu=curved_cpu;
        seed.mode=RD::SeedMode::SetB;seed.position={2,1.4,0};seed.radius=.8f;
        p.time_step=.05f;
        RD::step_surface(curved_mesh,curved_cpu,p,{seed},100,RD::Backend::CPU);
        if (RD::cuda_backend_available()) {
            RD::step_surface(curved_mesh,curved_gpu,p,{seed},100,RD::Backend::CUDA,false);
            float error=0;
            for (int i=0;i<99;++i) {
                error=std::max(error,std::abs(curved_cpu.a[i]-curved_gpu.a[i]));
                error=std::max(error,std::abs(curved_cpu.b[i]-curved_gpu.b[i]));
            }
            check(error<2e-5f,"Curved surface CUDA mismatch");
            std::cout<<"Curved surface CPU/CUDA maximum error: "<<error<<'\n';
        }
        std::cout << "Surface tests: PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
