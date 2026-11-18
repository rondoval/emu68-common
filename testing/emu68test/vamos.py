# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
vamos.py - logic programs: a component's own sources built by the stack as
m68k CLI programs (emu68_logic_program()) and run on the host under amitools'
vamos. A program prints the output grammar (results.py) and the test judges
what it printed.

A program older than a file it is made of is stale: the test fails rather
than judge code that is no longer there (scripts/docker-build.sh rebuilds it).
The files are those its objects were compiled from (the compiler's depfiles in
the build dir name them) and the stack's libraries it links (CMake's link line
names those), so a harness library rebuilt after the program counts too.
"""
from __future__ import annotations

import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

from . import results

VAMOS = "vamos"
CPU = "68040"
RAM_KIB = 8192              # vamos refuses more with custom chips mapped
TIMEOUT_S = 60
CONTAINER_ROOT = "/work/"   # the stack checkout inside the build container (scripts/docker-build.sh)


def unavailable() -> str | None:
    """Why logic programs cannot run on this host, or None when they can."""
    if shutil.which(VAMOS) is None:
        return "vamos is not installed (pipx install amitools)"
    return None


def dependencies(depfiles: list[Path], stack_dir: Path) -> list[Path]:
    """The stack's files the compiler's @depfiles name (the toolchain's left out), as paths under @stack_dir."""
    found: set[Path] = set()
    for depfile in depfiles:
        for token in depfile.read_text().replace("\\\n", " ").split():
            if token.startswith(CONTAINER_ROOT):
                found.add(Path(stack_dir) / token[len(CONTAINER_ROOT):])
    return sorted(found)


def listed(facts: dict[str, str], name: str) -> list[str]:
    """The values of @name.0, @name.1, ... in @facts (a step's), in index order."""
    found = {int(key[len(name) + 1:]): value for key, value in facts.items()
             if key.startswith(name + ".") and key[len(name) + 1:].isdigit()}
    return [found[index] for index in sorted(found)]


def grouped(facts: dict[str, str], name: str) -> list[dict[str, str]]:
    """The groups @name.N.* in @facts, each as a dict of what follows @name.N., in index order."""
    found: dict[int, dict[str, str]] = {}
    for key, value in facts.items():
        head, _, rest = key.partition(".")
        if head != name:
            continue
        index, _, field = rest.partition(".")
        if index.isdigit() and field:
            found.setdefault(int(index), {})[field] = value
    return [found[index] for index in sorted(found)]


@dataclass
class Run:
    """One run of a program: its result, and what vamos said on stderr."""

    result: results.Result
    stderr: str = ""

    def steps(self) -> list[dict[str, str]]:
        """The facts of each step, step.N.*, without the prefix; HarnessError when a step was not taken."""
        if "fail" in self.result.kv:
            raise results.HarnessError(f"the program refused its steps: {self.result.kv['fail']}")
        return self.result.group("step")


@dataclass
class Program:
    """A logic program in the stack's build dir."""

    path: Path
    stack_dir: Path             # the stack checkout its depfiles' /work/ stands for
    ram_kib: int = RAM_KIB

    def linked(self) -> list[str] | None:
        """What CMake links the program from, its link line's words; None when the build dir does not say."""
        line = self.path.parent / "CMakeFiles" / f"{self.path.name}.dir" / "link.txt"
        return line.read_text().split() if line.exists() else None

    def sources(self) -> list[Path]:
        """The stack's files the program is made of: what its objects were compiled from (a depfile left
        by an object no longer linked is passed over), and the stack's libraries on its link line."""
        directory = self.path.parent / "CMakeFiles" / f"{self.path.name}.dir"
        depfiles = sorted(directory.rglob("*.d"))
        linked = self.linked()
        if linked is None:
            return dependencies(depfiles, self.stack_dir)
        objects = {str(depfile.relative_to(self.path.parent))[:-len(".d")] for depfile in depfiles}
        kept = [depfile for depfile in depfiles if str(depfile.relative_to(self.path.parent))[:-len(".d")] in
                objects.intersection(linked)]
        libraries = [self.stack_dir / word[len(CONTAINER_ROOT):] for word in linked
                     if word.startswith(CONTAINER_ROOT) and word.endswith(".a")]
        return sorted(set(dependencies(kept, self.stack_dir)) | set(libraries))

    def stale(self) -> Path | None:
        """A file the program is made of that changed since it was built, or went; None when it is current."""
        built = self.path.stat().st_mtime
        for source in self.sources():
            if not source.exists() or source.stat().st_mtime > built:
                return source
        return None

    def command(self, args: list[str], volumes: dict[str, Path], assigns: dict[str, str]) -> list[str]:
        """vamos's command line for @args, with @volumes (name: host drawer) and @assigns (name: Amiga path)."""
        command = [VAMOS, "-q", "-C", CPU, "-m", str(self.ram_kib)]
        for name, drawer in volumes.items():
            command += ["-V", f"{name}:{Path(drawer).resolve()}"]
        for name, target in assigns.items():
            command += ["-a", f"{name}:{target}"]
        return command + [self.path.name, *args]

    def run(self, *args: str, volumes: dict[str, Path] | None = None, assigns: dict[str, str] | None = None) -> Run:
        """Run the program in its own drawer; AssertionError when it ends without a verdict (it crashed)."""
        command = self.command(list(args), volumes or {}, assigns or {})
        done = subprocess.run(command, cwd=self.path.parent, capture_output=True, text=True, timeout=TIMEOUT_S)
        result = results.parse(done.stdout, done.returncode)
        if result.verdict is None:
            raise AssertionError(f"{self.path.name} {' '.join(args)}: no verdict, rc {done.returncode} "
                                 f"(crashed under vamos?): {done.stderr.strip()[-500:]}")
        return Run(result, done.stderr)

    def run_plain(self, *args: str) -> subprocess.CompletedProcess:
        """Run a program that does not print the grammar: its return code, stdout and stderr."""
        return subprocess.run(self.command(list(args), {}, {}), cwd=self.path.parent, capture_output=True,
                              text=True, timeout=TIMEOUT_S)


def find(where: Path, name: str, stack_dir: Path) -> tuple[Program | None, str]:
    """The program @name in the build drawer @where: (it, '') when it is built and current, else
    (None, why): 'missing: ...' or 'stale: ...'."""
    path = Path(where) / name
    if not path.is_file():
        return None, f"missing: {name} is not built in {where} (scripts/docker-build.sh from the stack root)"
    program = Program(path, Path(stack_dir))
    changed = program.stale()
    if changed is not None:
        return None, f"stale: {name} is older than {changed}: rebuild (scripts/docker-build.sh from the stack root)"
    return program, ""
