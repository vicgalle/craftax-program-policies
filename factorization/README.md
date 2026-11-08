# Factorization: P\* = t = I(p0) = mix(I, p0)

The boss-refined C policy P\* (`../policies/optimized_extension/`) is factored into
a fixed machine **I** and an editable rule program **p0**. Each step is checked by
exact parity: the same action and the same skill label at every decision.

| Path | Role |
|---|---|
| `target/` | **t**: the C++ translation of P\* that is factored (one translation unit, `full_policy.cpp`). |
| `I/` | **I**: the machine. It holds the TR runtime (`tr_runtime.hpp`: a dynamic evaluator and a template evaluator over one node image), observation processing, spatial memory, world and resource models, the safety shield, and navigation/crafting/combat skills. `exports/*.txt` lists the natives that programs may call, the machine constants and the queries that I asks the program; `*.gen.hpp`, `natives.json` and `schema.json` are generated from them by `tools/bind.py`. `FACTS.md` lists the game facts that remain in I. |
| `p0/` | **p0**: the rule program in TR, a teleo-reactive rule language. It has 149 ordered rules and 152 definitions in seven files; the definitions include the answers to I's 24 queries. `floor8.tr` holds the rule table. |
| `trc/trc.py` | The TR front end. It parses and type-checks a program against I's record layouts and natives, and lowers it to a typed node image (`program.gen.hpp`, plus a binary `.trb` image). |
| `tools/` | `bind.py` (with `csrc.py` and `schema.py`) regenerates I's bindings after an edit to `I/exports/` or to I's sources. |
| `harness/` | `bench.c` runs live engine episodes and records or checks action/skill streams. `full_policy_test.c` holds the policy unit tests. |
| `parity.py` | Builds everything and runs the parity checks. |

A program runs in one of two ways:

- **Interpreted, I(p0):** the image is opaque run-time data. It is compiled in through a volatile pointer or loaded from `TR_PROGRAM=path/program.trb`.
- **Specialized, mix(I, p0):** with `#define TR_SPEC 1`, every node index becomes a template argument, so clang unfolds the interpreter over the program.

## Reproduce

From the repository root:

```sh
python3 factorization/parity.py check --jobs 5
```

This records P\*'s reference streams with `bench --dump` on the dev set (64 worlds,
seed 2100000000) and the held-out set (256 worlds, seed 2110000000). It then
replays every episode from reset for P\*, t, I(p0) and mix(I, p0), with the scratch
memory poisoned before every decision, and prints the first divergent step of
any episode. It also runs I(p0) with p0 loaded from `program.trb`, and the unit
tests for every candidate. Expected output:

```
{"record": "dev", "policy": "P*", "episodes": 64, "steps": 3209631}
{"candidate": "t", "set": "dev", ..., "diverged": 0, "parity": true}     (likewise P_star, I_p0_interp, mix_I_p0_spec)
{"record": "holdout", "policy": "P*", "episodes": 256, "steps": 12354934}
... "diverged": 0, "parity": true ...
PARITY CHECK PASS
```

All outputs go to `factorization/build/`.

Other commands:

```sh
python3 factorization/parity.py assemble [PROGRAM_DIR]   # compile any TR program against I (interp + spec sources)
python3 factorization/parity.py parity DIR --set dev     # check one policy directory against the recorded streams
python3 factorization/parity.py time                     # replay timing of t, I(p0), mix(I, p0)
python3 factorization/tools/bind.py factorization/I      # regenerate I's bindings
```

`time` first records a replay trace of eight dev episodes (about 220k decisions)
from P\*. It then alternates the candidates over three rounds and reports the best
ns and instructions per decision. Instruction counts are available only on macOS.

Tested only on macOS (arm64) with Apple clang 21 and Python 3.12. The policies need
C11 and C++23. Python needs only its standard library.
