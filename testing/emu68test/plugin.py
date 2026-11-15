# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
plugin.py - the pytest plugin every stack suite loads: the options, hooks and
fixtures they share. A suite's conftest.py says what it is, then loads it:

    pytest.register_assert_rewrite("emu68test")      # before the first import: its asserts explain themselves
    from emu68test import plugin
    plugin.configure(plugin.Suite(name="genet.device", component="emu68-genet-driver-netdev",
                                  tests_dir=TESTS, ram="RAM:gt", kv_programs=("GenetTest",)))
    pytest_plugins = ["emu68test.plugin"]

Options: --server, --amimcp, --install-dir, --build-dir, --run-dir,
--no-install, --serial, --require-debug-log. The marker covers(*items).

Fixtures:
  build_info       the stack build's flavour for the suite's component
  rundir           this run's directory: report.md, run.json, measured.json, a dir per test
  capture          the debug log, captured for the session (only when a test or fixture asks)
  debuglog         the same for a test that reads it: skipped without a capture
  logic_program    a logic program by name, run under vamos
  test_dir         (autouse) a hardware test's own dir; names the test to the device

The device fixture is the suite's own, built on connect(): what it installs
and checks first differs from suite to suite. A suite hands its Baselines to
the report with session(config).baselines.
"""
from __future__ import annotations

import json
import os
import re
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import pytest

from . import vamos
from .build import BuildInfo
from .debuglog import ADAPTERS, DebugLog, garbled
from .machine import Device, Unreachable, client
from .report import Report, Row

EXIT_ENVIRONMENT = 3        # pytest.exit()'s code when the machine or the install is not as needed
C_COMMANDS = "C:Reboot C:MakeDir C:Break C:Join C:Status C:Avail C:Version C:List C:Delete C:Execute"     # what the harness itself runs; Run is the shell's own


@dataclass
class Suite:
    """What a suite tells the plugin about itself."""

    name: str                           # what it tests, the report's title: genet.device
    component: str                      # its component in the stack: emu68-genet-driver-netdev
    tests_dir: Path                     # its tests/ directory
    ram: str                            # its drawer in RAM: on the Amiga: RAM:gt
    kv_programs: tuple[str, ...] = ()   # its Amiga-side programs that print the grammar
    end_marks: bool = False             # they mark their runs in the debug log (kv_debug_log())
    boot_settle_s: float = 10           # the wait after a boot before the machine is used
    run_options: tuple[str, ...] = ()   # its own options run.json records, by their dest names
    logic_dir: str = "tests/logic"      # where its logic programs are built, under the component's build dir

    @property
    def stack_dir(self) -> Path:
        """The stack checkout: <stack>/components/<component>/tests."""
        return Path(self.tests_dir).resolve().parents[2]


class Session:
    """What the hooks share with the fixtures."""

    def __init__(self, suite: Suite, build_info: BuildInfo):
        self.suite = suite
        self.build_info = build_info
        self.report: Report | None = None
        self.baselines = None               # the suite's Baselines, for the report and measured.json
        self.capture: DebugLog | None = None
        self.rundir: Path | None = None


_suite: Suite | None = None


def configure(suite: Suite) -> None:
    """Say which suite this pytest run is; a conftest calls it before the plugin loads."""
    global _suite
    _suite = suite


def session(config) -> Session:
    """The run's Session."""
    return config._emu68


def _configured() -> Suite:
    if _suite is None:
        raise pytest.UsageError("emu68test.plugin: the suite's conftest.py must call plugin.configure() first")
    return _suite


def pytest_addoption(parser):
    suite = _configured()
    stack = suite.stack_dir
    g = parser.getgroup("emu68test", "the stack's common test harness")
    g.addoption("--server", default="amiga-pi4",
                help="the amimcp server name in ~/.claude.json (amiga-pi4, amiga-cm4); also the baselines key")
    g.addoption("--amimcp", default=os.environ.get("AMIMCP", str(stack.parent / "amimcp")),
                help="the amimcp checkout (its server/amiga.py); default $AMIMCP, else beside the stack")
    g.addoption("--install-dir", default=str(stack / "install"), help="the stack's install tree")
    g.addoption("--build-dir", default=str(stack / "build"), help="the stack's build dir")
    g.addoption("--run-dir", default=None,
                help="where this run's report and outputs go (default tests/runs/<timestamp>)")
    g.addoption("--no-install", action="store_true",
                help="do not replace the files on the Amiga; check they are the build's")
    g.addoption("--serial", default="/dev/ttyUSB0", help="the debug log's serial device, or none")
    g.addoption("--require-debug-log", action="store_true",
                help="fail, not skip, the tests that need the debug log when it is not captured")


def pytest_configure(config):
    suite = _configured()
    config.addinivalue_line("markers", "covers(*items): what the test covers (TRACEABILITY.md)")
    config._emu68 = Session(suite, BuildInfo.read(Path(config.getoption("--build-dir")), suite.component))


def _reason(rep) -> str:
    """Why a test did not pass, in one line: a skip's reason or the failure's last line."""
    if rep.outcome == "skipped" and isinstance(rep.longrepr, tuple):
        return rep.longrepr[2]
    if rep.outcome != "passed" and rep.longrepr:
        return str(rep.longrepr).splitlines()[-1]
    return ""


def _row(item, rep) -> Row:
    """The report's row for a test's call, or for a setup that did not pass."""
    notes = [f"{k}: {v}" for k, v in item.user_properties]
    state = rep.outcome if rep.when == "call" or rep.outcome == "skipped" else f"{rep.outcome} in setup"
    return Row(item.nodeid, state, rep.duration, _reason(rep), notes)


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    if call.excinfo is not None and isinstance(call.excinfo.value, Unreachable):
        pytest.exit(f"the Amiga is unreachable: {call.excinfo.value}", returncode=EXIT_ENVIRONMENT)
    report = session(item.config).report
    if report is not None and (rep.when == "call" or (rep.when == "setup" and rep.outcome != "passed")):
        report.add(_row(item, rep))


def pytest_sessionfinish(session, exitstatus):
    run = session.config._emu68
    if run.report is None:
        return
    if run.baselines is not None:
        run.baselines.write_measured(run.rundir / "measured.json")
    if run.capture is not None and run.capture.ok:
        bad, total = garbled(run.capture.text(0))
        run.report.header.append(f"Capture quality: {bad} of {total} lines garbled "
                                 f"({100 * bad / max(1, total):.1f}%: Emu68's own output interleaved with the drivers')")
    path = run.report.write(run.baselines, run.capture)
    session.config.pluginmanager.get_plugin("terminalreporter").write_line(f"report: {path}")


def connect(request, capture: DebugLog | None = None) -> Device:
    """The Amiga the run was pointed at, checked and ready: the agent answers, C: is as expected, the
    suite's RAM: drawers exist. The run ends (exit code 3) when it is not."""
    config = request.config
    suite = session(config).suite
    mod, ami = client(config.getoption("--amimcp"), config.getoption("--server"))
    device = Device(mod, ami, request.getfixturevalue("rundir"), suite.ram, suite.kv_programs, capture,
                    suite.end_marks, suite.boot_settle_s)
    try:
        ami.ping()
    except Exception as e:
        pytest.exit(f"the agent on {config.getoption('--server')} does not answer: {e}", returncode=EXIT_ENVIRONMENT)
    rc, text = device.exec(f"List >NIL: {C_COMMANDS}", timeout=15)
    if rc != 0:
        pytest.exit(f"C: is not as expected (Assign C: EXISTS): {text.strip()}", returncode=EXIT_ENVIRONMENT)
    device.ram_dirs()
    return device


@pytest.fixture(scope="session")
def build_info(request) -> BuildInfo:
    """The stack build's flavour for the suite's component."""
    return session(request.config).build_info


@pytest.fixture(scope="session")
def rundir(request) -> Path:
    """This run's directory: report.md, run.json, measured.json, one dir per test."""
    run = session(request.config)
    chosen = request.config.getoption("--run-dir")
    path = Path(chosen) if chosen else Path(run.suite.tests_dir) / "runs" / time.strftime("%Y%m%d-%H%M%S")
    path.mkdir(parents=True, exist_ok=True)
    run.rundir = path
    run.report = Report(path, f"{run.suite.name} tests, {time.strftime('%Y-%m-%d %H:%M')}")
    recorded = ("server", "no_install", "serial") + tuple(run.suite.run_options)
    options = {k: v for k, v in vars(request.config.option).items() if k in recorded}
    (path / "run.json").write_text(json.dumps({"build": run.build_info.summary(), "options": options},
                                              indent=2, default=str) + "\n")
    return path


@pytest.fixture(scope="session")
def capture(request, rundir) -> DebugLog:
    """The debug log, captured for the whole session; its .ok says whether the port opened."""
    config = request.config
    log = DebugLog(rundir / "serial.log", config.getoption("--serial"), ADAPTERS.get(config.getoption("--server")))
    log.start()
    session(config).capture = log
    yield log
    log.stop()


@pytest.fixture
def debuglog(request, capture) -> DebugLog:
    """The debug log, for a test that reads it: skipped (failed with --require-debug-log) without a capture."""
    if not capture.ok:
        if request.config.getoption("--require-debug-log"):
            pytest.fail(f"no debug log capture: {capture.why}")
        pytest.skip(f"no debug log capture: {capture.why}")
    return capture


@pytest.fixture(scope="session")
def logic_program(request) -> Callable[[str], vamos.Program]:
    """A logic program by name: skipped without vamos or a build, failed when older than its sources."""
    why = vamos.unavailable()
    if why is not None:
        pytest.skip(why)
    suite = session(request.config).suite
    where = Path(request.config.getoption("--build-dir")) / suite.component / suite.logic_dir
    checked: dict[str, vamos.Program] = {}

    def program(name: str) -> vamos.Program:
        if name not in checked:
            found, why_not = vamos.find(where, name, suite.stack_dir)
            if found is None:
                (pytest.skip if why_not.startswith("missing") else pytest.fail)(why_not)
            checked[name] = found
        return checked[name]

    return program


@pytest.fixture(autouse=True)
def test_dir(request):
    """A hardware test's own directory in the run dir, where its programs' outputs and its slice of the
    debug log are kept; the device is told which test runs. None for a test that uses no device."""
    if "device" not in request.fixturenames:
        yield None
        return
    device = request.getfixturevalue("device")
    name = re.sub(r"[^A-Za-z0-9_.\[\]-]+", "_", request.node.name)
    path = request.getfixturevalue("rundir") / request.node.module.__name__.split(".")[-1] / name
    path.mkdir(parents=True, exist_ok=True)
    device.test_name, device.artifacts, device.runs = request.node.name, path, 0
    log = session(request.config).capture
    mark = log.mark() if log is not None else 0
    yield path
    device.test_name, device.artifacts = "session", None
    if log is not None and log.ok:
        text = log.text(mark)
        if text:
            (path / "debug.log").write_text(text, encoding="latin-1")
