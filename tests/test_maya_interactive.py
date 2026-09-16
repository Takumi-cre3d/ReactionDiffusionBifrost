"""Run in a fresh interactive Maya via -command, never in a user's scene."""
import json
import os
import sys
import traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DOMAIN=os.environ.get('RD_TEST_DOMAIN','grid')
RESULT=ROOT/('build/interactive-'+DOMAIN+'.json')
sys.path.insert(0,os.environ.get('RD_TEST_PACKAGE_ROOT',str(ROOT/'maya_module/ReactionDiffusionBifrost/0.2.0/scripts')))
import maya.cmds as cmds
from PySide6.QtCore import QTimer
WARNINGS=[]
original_warning=cmds.warning
def warning(message):
    WARNINGS.append(str(message))
    original_warning(message)
cmds.warning=warning


def run():
    if os.environ.get('RD_TEST_PACK'):
        configs=[p for p in os.environ.get('BIFROST_LIB_CONFIG_FILES','').split(os.pathsep) if p and 'ReactionDiffusion' not in p]
        os.environ['BIFROST_LIB_CONFIG_FILES']=os.pathsep.join(configs+[os.environ['RD_TEST_PACK']])
    from reaction_diffusion_bifrost import ui, preview
    cmds.evaluationManager(mode='off' if os.environ.get('RD_TEST_DG') else 'parallel')
    if os.environ.get('RD_TEST_NO_CACHE'):
        cmds.evaluator(name='cache',enable=False)
    ui.show()
    cmds.intField(ui.CONTROLS['width'],edit=True,value=24)
    cmds.intField(ui.CONTROLS['height'],edit=True,value=18)
    cmds.intField(ui.CONTROLS['step_size'],edit=True,value=8)
    cmds.playbackOptions(minTime=0,maxTime=8,loop='once',playbackSpeed=0,maxPlaybackSpeed=1)
    if DOMAIN=='grid':
        ui._create_stateful_graph()
    else:
        if DOMAIN=='surface':
            source=cmds.polyPlane(width=4,height=4,subdivisionsX=8,subdivisionsY=8)[0]
            shape=cmds.listRelatives(source,shapes=True)[0]
            cmds.setKeyframe(shape+'.pnts[40].pnty',time=0,value=0)
            cmds.setKeyframe(shape+'.pnts[40].pnty',time=8,value=0.2)
        ui._create_spatial_graph(DOMAIN)
    graph=ui._graph_name()
    print('RD_TIME_ATTRIBUTES', [a for a in cmds.listAttr(graph) if 'time' in a.lower()], flush=True)
    print('RD_CONNECTIONS', cmds.listConnections(graph,connections=True,plugs=True),flush=True)
    baseline=preview.read_pattern(graph)
    initial_positions=cmds.getAttr(graph+'.surface_positions') if DOMAIN=='surface' else None
    observations=[]
    frames={}
    original=preview.update_preview

    def record(*args,**kwargs):
        result=original(*args,**kwargs)
        values=preview.read_pattern(graph)
        frame=float(cmds.currentTime(query=True))
        frames.setdefault(frame,values)
        observations.append((frame,max(abs(a-b) for a,b in zip(values,baseline))))
        return result

    preview.update_preview=record

    def finish():
        try:
            cmds.play(state=False)
            playback=list(observations)
            if DOMAIN=='surface':
                assert cmds.getAttr(graph+'.surface_positions') != initial_positions,'Animated mesh input did not update'
            cmds.currentTime(0)
            reset=preview.read_pattern(graph)
            error=max(abs(a-b) for a,b in zip(reset,baseline))
            assert set(range(1,9)).issubset({f for f,d in playback}), playback
            assert all(d>1e-6 for f,d in playback if 1<=f<=8), playback
            assert error<1e-7,error
            ui._remove_time_callback()
            from reaction_diffusion_bifrost import graph_setup as setup
            parity=0
            if DOMAIN=='grid':
                reference=setup.create_preview_graph(24,18,0)
                setup._set_default(reference['graph'], reference['step_node'], 'backend', '1')
                for frame,values in sorted(frames.items()):
                    if not 1<=frame<=8:
                        continue
                    setup._set_default(reference['graph'], reference['step_node'], 'substeps', str(int(frame)*8))
                    cmds.dgdirty(reference['graph'])
                    expected=preview.read_pattern(reference['graph'])
                    parity=max(parity,max(abs(a-b) for a,b in zip(values,expected)))
            else:
                step='/simulation_example/reaction_diffusion_'+DOMAIN+'_step'
                setup._set_default(graph,step,'backend','1')
                for frame in range(9):
                    cmds.currentTime(frame)
                    original(graph,refresh_viewport=False)
                    if frame:
                        expected=preview.read_pattern(graph)
                        parity=max(parity,max(abs(a-b) for a,b in zip(frames[frame],expected)))
            assert parity<2e-5,parity
            result={'passed':True,'playback':playback,'reset_error':error,'cpu_parity_error':parity}
        except Exception:
            result={'passed':False,'error':traceback.format_exc(),'observations':observations}
        finally:
            preview.update_preview=original
            ui._remove_time_callback()
        result['time_attributes']=[a for a in cmds.listAttr(graph) if 'time' in a.lower()]
        result['evaluation_mode']=cmds.evaluationManager(query=True,mode=True)
        result['package_path']=preview.__file__
        result['connections']=cmds.listConnections(graph,connections=True,plugs=True)
        RESULT.write_text(json.dumps(result,indent=2),encoding='utf-8')
        cmds.quit(force=True)

    QTimer.singleShot(5000,finish)
    cmds.play(forward=True)


def guarded_run():
    try:
        run()
    except Exception:
        RESULT.write_text(
            json.dumps({'passed':False,'setup_error':traceback.format_exc(),'warnings':WARNINGS},indent=2), encoding='utf-8')
        cmds.quit(force=True)


cmds.evalDeferred(guarded_run)
