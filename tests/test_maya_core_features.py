"""Native output adapters, frame events and presets in a real Bifrost runtime."""
import os
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,os.environ.get('RD_TEST_PACKAGE_ROOT',str(ROOT/'maya_module/ReactionDiffusionBifrost/0.2.0/scripts')))
os.environ['MAYA_SKIP_USERSETUP_PY']='1'
import maya.standalone


def main():
    maya.standalone.initialize(name='python')
    try:
        if os.environ.get('RD_TEST_PACK'):
            configs=[p for p in os.environ.get('BIFROST_LIB_CONFIG_FILES','').split(os.pathsep) if p and 'ReactionDiffusion' not in p]
            os.environ['BIFROST_LIB_CONFIG_FILES']=os.pathsep.join(configs+[os.environ['RD_TEST_PACK']])
        import maya.cmds as cmds
        from reaction_diffusion_bifrost import graph_setup as setup, outputs, preview, seed_events, presets
        cmds.currentTime(0)
        made=setup.create_stateful_preview_graph(24,18,2,0)
        graph=made['graph']
        image=outputs.add_image(graph,made['outputs_node']+'.pattern',24,18)
        writer=setup._add_node_at(graph,'/','File::Image','write_texture')
        cmds.vnnConnect(graph,'/construct_texture.texture',writer+'.texture')
        setup._set_default(graph,writer,'file_path',str(ROOT/'build/core-texture.exr').replace('\\','/'))
        setup._set_default(graph,writer,'file_color_space','raw')
        cmds.vnnNode(graph,'/output',createInputPort=('texture_written','bool'))
        cmds.vnnConnect(graph,writer+'.success','/output.texture_written')
        info=setup._add_node_at(graph,'/','File::Image','get_image_structure')
        cmds.vnnConnect(graph,image+'.image',info+'.image')
        for port in ('pixel_count','width_pixels','height_pixels'):
            dtype=cmds.vnnNode(graph,info,queryPortDataType=port)
            cmds.vnnNode(graph,'/output',createInputPort=(port,dtype))
            cmds.vnnConnect(graph,info+'.'+port,'/output.'+port)
        setup._set_default(graph,made['samples_node'],'point_radius','0.06')
        adapters=outputs.add_volume_mesh(graph,made['points_source'],0.03)
        cmds.dgdirty(graph)
        assert cmds.getAttr(graph+'.pixel_count')==24*18
        assert cmds.getAttr(graph+'.width_pixels')==24
        assert cmds.getAttr(graph+'.height_pixels')==18
        assert cmds.getAttr(graph+'.texture_written') and (ROOT/'build/core-texture.exr').stat().st_size>0
        geometries=cmds.bifrostGraph(graph,createMayaGeometry='mesh')
        meshes=cmds.ls(type='mesh',long=True)
        assert meshes and any(cmds.polyEvaluate(m,vertex=True)>0 for m in meshes),geometries
        seed_events.set_events(graph,[dict(frame=1,position=[.1,.1,0],radius=.2,strength=1,mode=0)])
        history=[]
        for frame in (0,1,2,0,1,2):
            cmds.currentTime(frame)
            cmds.dgdirty(graph)
            history.append(preview.read_pattern(graph))
        assert history[:3]==history[3:],'Seed replay is not deterministic'
        assert history[1]!=history[0] and history[2]!=history[1]
        assert history[1][2*24+2] > .1,'Frame seed was not applied'
        # Authoring a future seed must not destroy the already computed State.
        seed_events.set_events(graph,[dict(frame=3,position=[.8,.8,0],radius=.1,strength=1,mode=0)])
        assert preview.read_pattern(graph)==history[-1], 'Future seed editing reset current State'
        cmds.currentTime(3)
        cmds.dgdirty(graph)
        assert preview.read_pattern(graph)[14*24+19]>.1,'Future seed did not reach the next frame'
        presets.apply(graph,'Coral')
        preset=setup._add_node_at(graph,'/',setup.NAMESPACE,'reaction_diffusion_preset')
        for port in ('feed_rate','kill_rate','diffusion_a','diffusion_b'):
            cmds.vnnNode(graph,'/output',createInputPort=('preset_'+port,'float'))
            cmds.vnnConnect(graph,preset+'.'+port,'/output.preset_'+port)
        for index,expected in enumerate(presets.PRESETS.values()):
            setup._set_default(graph,preset,'preset',str(index))
            cmds.dgdirty(graph)
            actual=[cmds.getAttr(graph+'.preset_'+p) for p in ('feed_rate','kill_rate','diffusion_a','diffusion_b')]
            assert max(abs(a-b) for a,b in zip(actual,expected))<1e-7,(actual,expected)
        print('Native image / points-to-volume-to-mesh / timed seeds / presets: PASS')
    finally:
        maya.standalone.uninitialize()


if __name__=='__main__':
    main()
