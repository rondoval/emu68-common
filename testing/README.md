# The stack's common test harness

What the components' test suites share: one copy of the code that reaches the Amiga, reads the
test programs' output, judges measurements, runs m68k programs on the host, and gates the C. A
component keeps its own `tests/` with its tests, rigs and baselines, and builds on this.

## What is here

| Part | For |
|---|---|
| `emu68test/` | the host side, a Python package (below) |
| `include/emu68test/kv.h`, `src/kv.c` | `Emu68Common::kv`: the output grammar of an Amiga-side test program |
| `include/emu68test/logic.h`, `src/fake*.c`, `src/steps.c` | `Emu68Common::logic`: support for a logic program, a component's own sources run on the host under vamos |
| `cmake/Emu68CommonTesting.cmake` (one level up) | `emu68_logic_program()`: one logic program |
| `selftest/logicself.c` | LogicSelf, the harness's own logic program |
| `requirements.txt` | what the package needs; a suite's `run.sh` installs it into the suite's venv |

The stack installs the package to `<install>/share/emu68-testing/` and the libraries and headers
with the rest of emu68-common, so a suite finds them where it finds its artefacts, and no
component reaches into another's tree.

## The package

| Module | What it does |
|---|---|
| `plugin.py` | the pytest plugin a suite loads: the shared options, the `covers` marker, the report hooks, the fixtures `build_info`, `rundir`, `capture`, `debuglog`, `logic_program`, `test_dir`; `connect()` for the suite's own `device` fixture |
| `machine.py` | `Device`: commands, program runs, uploads with read-back, background jobs (`Job`), detached scripts, reboots |
| `install.py` | `Installer`: the build into its real places, the machine's own files kept on the host first, restore |
| `debuglog.py` | `DebugLog`: Emu68's serial output captured for the session, read by marks |
| `screen.py` | screenshots, a window's place, clicks; needs numpy, so only a suite that looks at the screen imports it |
| `operator.py` | `Operator`: the person at the machine, for tests that need hands or ears |
| `results.py` | the grammar's parser (`Result`, its checks), `expect()` and `forbid()` for prose |
| `baselines.py` | a suite's `baselines.json`: a figure against its bound, or near its median |
| `report.py` | the run's `report.md` |
| `perf.py` | a profile build's perf slots from the debug log |
| `build.py` | `BuildInfo`: a component's flavour from the stack build's `CMakeCache.txt` |
| `vamos.py` | logic programs: found in the build dir, checked against their sources, run under vamos |
| `gates.py` | kernel-doc and complexity over the files a suite calls its own |
| `trace.py` | traceability: a suite's items, the tests that cover them, known gaps with their reasons, `TRACEABILITY.md` |

## The grammar

A test program prints `format=1`, then `key=value` lines (prose indented two spaces), then
`PASS` or `FAIL`. A check is a group entry: `check.N.name` (the same text every run),
`check.N.ok=yes|no` and `check.N.detail` (what was seen); `checks` and `checks_failed` come
before the verdict. Return codes: 0 passed, 5 a check failed, 10 the environment (a device or
library did not open), 20 the API under test refused or a usage error, which is printed before
the format line. `kv.h` prints it, `results.py` reads it.

A program on a real machine may also mark its run in the debug log (`kv_debug_log()`), so a suite
can cut the log by runs. A logic program never does: nothing answers at the debug port under
vamos.

Two things follow from `kv.c` printing through stdio:

- **Ctrl-C.** libnix's stdio looks for Ctrl-C and ends the program on the spot, without its
  verdict and with whatever it holds left open. Linking `Emu68Common::kv` turns that check off,
  so a program a suite stops with `Job.stop()` has to look for `SIGBREAKF_CTRL_C` itself and wind
  down.
- **DOS output beside it.** A line the program prints with dos.library (`Printf()`,
  `PrintFault()`) sits in the file handle's buffer and would come out after later lines of the
  grammar: follow it with `Flush(Output())`.

## Starting a suite in a component

1. `tests/conftest.py` says what the suite is and loads the plugin:

   ```python
   pytest.register_assert_rewrite("emu68test")
   from emu68test import plugin
   plugin.configure(plugin.Suite(name="thing.device", component="emu68-thing-driver",
                                 tests_dir=TESTS, ram="RAM:tt", kv_programs=("ThingTest",)))
   pytest_plugins = ["emu68test.plugin"]
   ```

2. `tests/run.sh` puts `<install>/share/emu68-testing` on `PYTHONPATH` and installs its
   `requirements.txt` into `tests/.venv`. `emu68-common/tests/` is the smallest example of a
   suite (it takes the package from this checkout, since it is the one that tests it).
3. A rig for the Amiga links `Emu68Common::kv`. A logic program is one
   `emu68_logic_program(<name> SOURCES ...)` in the component's CMake: it includes the source
   under test, takes its steps from the command line and prints the state after each (`logic.h`).
4. The component's CMake states the emu68-common version it needs (`find_package(Emu68Common
   2.4 ...)`: 2.4 is the first with the harness).

The `device` fixture is the suite's own, built on `plugin.connect()`: what a suite installs and
proves first is its business. Two things a suite can add:

- **Its own `Device`.** `plugin.connect(request, capture, device_class=...)` takes a subclass
  (the VideoCore suites' `VchiqDevice` runs its rigs by name and adds the screen).
- **A package of its own on top,** when other components' suites need its fixtures. The vchiq
  library keeps its part in `tests/vchiqtest/` (a plugin, the device, the material) and installs
  it to `<install>/share/vchiq-testing/`; rpivid's conftest loads both plugins from the install
  tree. A suite never imports from a sibling's source tree, a test module included: what two
  suites share moves into such a package.

## Running a suite

- **The machine's own files.** `Installer` copies each file to `tests/.machine/<server>/` on the
  host before it first replaces it, and never overwrites that copy. A suite that runs after
  another has installed its build finds that build there, not the machine's original: restore
  from the directory of the suite that came first.
- **Options that name a directory** are written `--build-dir=PATH`, with the equals sign, in a
  suite whose `run.sh` names no test directory itself (vchiq's, rpivid's). pytest takes a
  separate, existing path for a place to collect from, and then finds neither the suite's
  `conftest.py` nor its `pytest.ini`. genet's `run.sh` names its `hw` directory, which avoids it.
- **A suite's `run.sh` changes into `tests/`** before it starts pytest, so a relative
  `--run-dir` is relative to that.
- **The agent can stall** for a minute or two. `Device.exec()` and the transfers wait and try
  once more; a test that calls the agent's client directly wraps the call in `device.retry()`.
  Judge a time by the test program's own clock, never by the host's around a call.

## Its own tests

`emu68-common/tests/run.sh host`: the package on recorded inputs and a scripted agent
(`unit/`), LogicSelf, memcheck and prefscheck under vamos (`logic/`), and the code gates over
the harness's C (`gates/`). No Amiga. It needs the stack built once (`scripts/docker-build.sh`),
for the programs it runs.

A logic program is checked against the files it is made of: the sources its objects were compiled
from and the stack's libraries it links. After a change to `src/`, a suite's host tier fails until
the stack is rebuilt.
