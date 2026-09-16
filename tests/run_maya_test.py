"""Run a legacy integration check against a chosen installed package/pack."""
import os
import runpy
import sys
from pathlib import Path
os.environ['MAYA_SKIP_USERSETUP_PY']='1'
import maya.standalone
maya.standalone.initialize(name='python')
try:
    if os.environ.get('RD_TEST_PACK'):
        configs=[p for p in os.environ.get('BIFROST_LIB_CONFIG_FILES','').split(os.pathsep) if p and 'ReactionDiffusion' not in p]
        os.environ['BIFROST_LIB_CONFIG_FILES']=os.pathsep.join(configs+[os.environ['RD_TEST_PACK']])
    os.environ['RD_MAYA_ALREADY_INITIALIZED']='1'
    test=Path(__file__).parent/sys.argv[1]
    runpy.run_path(str(test),run_name='__main__')
finally:
    maya.standalone.uninitialize()
