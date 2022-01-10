# Fulup(TBD) probably not the cleanest option to load a module
# ------------------------------------------------------------
import sys
import os.path
import inspect

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../build/src'))
import _afbpyglue as libafb