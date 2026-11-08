"""Shared locations: the fixed machine I, the base rule program p0, the TR front end and the engine."""
import hashlib
from pathlib import Path

HERE = Path(__file__).resolve().parent          # compiler/
ROOT = HERE.parent                              # repository root
MACHINE = ROOT / 'factorization' / 'I'          # the machine I (interpreter, models, memory, skills)
BASE_PROGRAM = ROOT / 'factorization' / 'p0'    # the rule program p0
TRC = ROOT / 'factorization' / 'trc' / 'trc.py' # TR front end
BUILD = HERE / 'build'                          # generated programs, binaries and runs (not versioned)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
