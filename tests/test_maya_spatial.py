"""Run against the new pack in a fresh Maya process, not a user's scene."""
import os
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,os.environ.get('RD_TEST_PACKAGE_ROOT',str(ROOT/'maya_module/ReactionDiffusionBifrost/0.2.0/scripts')))
os.environ['MAYA_SKIP_USERSETUP_PY']='1'
import maya.standalone


def main():
    maya.standalone.initialize(name="python")
    try:
        if os.environ.get('RD_TEST_PACK'):
            configs=os.environ.get('BIFROST_LIB_CONFIG_FILES','').split(os.pathsep)
            configs=[p for p in configs if p and 'ReactionDiffusion' not in p]
            os.environ['BIFROST_LIB_CONFIG_FILES']=os.pathsep.join(configs+[os.environ['RD_TEST_PACK']])
        import maya.cmds as cmds
        from reaction_diffusion_bifrost import spatial, preview
        for domain in ("surface", "volume"):
            cmds.currentTime(0)
            if domain=="surface":
                created=spatial.create_graph(domain,
                    positions=[0,0,0, 2,0,0, 2,2,0, 0,2,0], triangles=[0,1,2,0,2,3],
                    substeps=2, seed_radius=1)
                count=4
            else:
                created=spatial.create_volume_graph(dimensions=(9,8,7),substeps=2,seed_radius=0.3)
                count=9*8*7
            graph=created['graph']
            print('inputs',domain,cmds.getAttr(graph+'.feed_rate'),cmds.getAttr(graph+'.time_step'),flush=True)
            if domain=='surface':
                print('positions',cmds.getAttr(graph+'.positions'),'triangles',cmds.getAttr(graph+'.triangles'),flush=True)
            history={}
            for frame in (0,1,2,0):
                cmds.currentTime(frame)
                cmds.dgdirty(graph)
                pattern=preview.read_pattern(graph)
                assert len(pattern)==count,(domain,len(pattern))
                preview.update_preview(graph, normalize=True, refresh_viewport=False)
                for port in ('gradient_x','gradient_y','gradient_z'):
                    assert len(preview._flatten_numbers(cmds.getAttr(graph+'.'+port)))==count
                if frame==0 and history:
                    assert max(abs(a-b) for a,b in zip(pattern,history[0]))<1e-7
                history[frame]=pattern
            assert max(abs(a-b) for a,b in zip(history[1],history[2]))>1e-7
            backend=cmds.getAttr(graph+'.backend_used')
            assert backend=='CUDA',(domain,backend,cmds.getAttr(graph+'.status'))
            print(domain+' feedback/CUDA/reset/gradients: PASS',flush=True)
    finally:
        maya.standalone.uninitialize()


if __name__=='__main__':
    main()
