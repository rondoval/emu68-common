# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
gates.py - the code gates on a component's own C: every function's kernel-doc
and its cyclomatic complexity.

A suite says which files are the component's own (Gates.sources): what it
wrote and maintains, tests' C included. Files kept diffable against an
upstream are left out: a clean diff is worth more there than our comments.

- Kernel-doc: a /** comment right above every function, "name() - what it
  does", an "@param:" line for each parameter, and "Return:" unless it
  returns void.
- Complexity (lizard's cyclomatic complexity): at most COMPLEXITY_LIMIT. A
  function in the suite's allow list may have at most the ceiling given
  there, which is at most COMPLEXITY_ALLOWED with a reason, or what the code
  had when the gate was introduced (a reason starting with the suite's
  inherited mark, such as "5.0:"). A ceiling stays at what the function needs:
  one above it is a finding until lowered, so the list only goes down.

The allow list, one function per line:

    <file>:<function> <ceiling> <reason>
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

import lizard

COMPLEXITY_LIMIT = 10
COMPLEXITY_ALLOWED = 15
REGISTER_BINDING = re.compile(r'asm\s*\("[^"]*"\)')
VOID_RESULT = re.compile(r"\b(void|VOID)\s*$")
ATTRIBUTE_MACRO = re.compile(r"^\s*[A-Z][A-Z0-9_]*\s*\([^;{}]*\)\s*$")   # EMU68_INTSERVER(bcmgenet_isr0)


@dataclass
class Ceiling:
    """An allow-list entry: the most a function may have, and why."""

    ceiling: int
    reason: str


def allowed(path: Path) -> dict[str, Ceiling]:
    """<file>:<function> -> its ceiling, from the allow list at @path (none when there is no file);
    ValueError for a line that is not an entry."""
    entries = {}
    for number, line in enumerate(path.read_text().splitlines() if path.exists() else [], 1):
        if not line.strip() or line.startswith("#"):
            continue
        found = re.match(r"^(\S+:\S+)\s+(\d+)\s+(\S.*)$", line)
        if found is None:
            raise ValueError(f"{path.name}:{number}: not '<file>:<function> <ceiling> <reason>'")
        entries[found.group(1)] = Ceiling(int(found.group(2)), found.group(3))
    return entries


@dataclass
class Gates:
    """The gates over one component."""

    root: Path                      # the component's checkout: findings name files relative to it
    sources: list[Path]             # every C file the gates cover
    allow_list: Path                # the complexity allow list
    inherited: str = ""             # the reason prefix of a ceiling the code had before the gate, e.g. "5.0:"

    def where(self, path: Path, function) -> str:
        """A function as findings name it: <file>:<line> <name>."""
        return f"{path.relative_to(self.root)}:{function.start_line} {function.name}"

    def functions(self):
        """(file, its lines, lizard's function) for every function in the gates' sources."""
        for path in self.sources:
            lines = path.read_text(encoding="latin-1").splitlines()
            for function in lizard.analyze_file(str(path)).function_list:
                yield path, lines, function

    def ceiling_finding(self, complexity: int, entry: Ceiling | None) -> str | None:
        """What is wrong with a function's complexity against its allow-list entry, or None."""
        if entry is None:
            return None if complexity <= COMPLEXITY_LIMIT else f"complexity {complexity} > {COMPLEXITY_LIMIT}"
        if entry.ceiling > COMPLEXITY_ALLOWED and not (self.inherited and entry.reason.startswith(self.inherited)):
            return f"ceiling {entry.ceiling} > {COMPLEXITY_ALLOWED}: only inherited ceilings are higher"
        if complexity > entry.ceiling:
            return f"complexity {complexity} > its ceiling {entry.ceiling}"
        if complexity <= COMPLEXITY_LIMIT:
            return f"complexity {complexity}: needs no entry in {self.allow_list.name} any more"
        if complexity < entry.ceiling:
            return f"complexity {complexity} < its ceiling {entry.ceiling}: lower the ceiling"
        return None

    def complexity_findings(self, allow: dict[str, Ceiling] | None = None) -> list[str]:
        """Functions over their limit, ceilings to lower, and entries for functions that are gone."""
        allow = allowed(self.allow_list) if allow is None else allow
        findings, seen = [], set()
        for path, _, function in self.functions():
            name = f"{path.relative_to(self.root)}:{function.name}"
            seen.add(name)
            problem = self.ceiling_finding(function.cyclomatic_complexity, allow.get(name))
            if problem:
                findings.append(f"{self.where(path, function)}: {problem}")
        return findings + [f"{self.allow_list.name}: {name} is no function of the gates' sources" for name in allow
                           if name not in seen]

    def kerneldoc_findings(self) -> list[str]:
        """Functions whose comment is missing or does not follow kernel-doc."""
        return [f"{self.where(path, function)}: {problem}" for path, lines, function in self.functions()
                for problem in kerneldoc_problems(lines, function)]


def comment_before(lines: list[str], start: int) -> str:
    """The comment block that ends right above line @start (1-based), past blank lines and an
    attribute macro such as EMU68_INTSERVER(name), or ''."""
    index = start - 2
    while index >= 0 and (not lines[index].strip() or ATTRIBUTE_MACRO.match(lines[index])):
        index -= 1
    if index < 0 or not lines[index].rstrip().endswith("*/"):
        return ""
    end = index
    while index >= 0 and "/*" not in lines[index]:
        index -= 1
    return "\n".join(lines[index:end + 1]) if index >= 0 else ""


def declaration(lines: list[str], function) -> str:
    """The function's declaration, from its first line to the parameter list's ')'."""
    text = " ".join(lines[function.start_line - 1:function.start_line + 6])
    return text[:text.index("{")] if "{" in text else text


def parameter_names(lines: list[str], function) -> list[str]:
    """A function's parameter names from its source: lizard takes a register binding (asm("d0")) for a name."""
    text = declaration(lines, function)
    inside, depth, parameters, current = text[text.index("(") + 1:], 0, [], ""
    for character in inside:
        if character == ")" and depth == 0:
            break
        depth += {"(": 1, ")": -1}.get(character, 0)
        if character == "," and depth == 0:
            parameters, current = parameters + [current], ""
        else:
            current += character
    names = [re.findall(r"(\w+)\s*(?:\[[^]]*\])?$", REGISTER_BINDING.sub("", p).strip()) for p in parameters + [current]]
    return [found[0] for found in names if found and found[0] != "void"]


def returns_void(lines: list[str], function) -> bool:
    """Whether the function's declaration says it returns void."""
    before = declaration(lines, function).split(function.name + "(", 1)[0]
    return VOID_RESULT.search(before) is not None


def kerneldoc_problems(lines: list[str], function) -> list[str]:
    """What the function's comment lacks, by kernel-doc."""
    comment = comment_before(lines, function.start_line)
    if not comment.lstrip().startswith("/**") or f"{function.name}() - " not in comment:
        return ["no kernel-doc comment"]
    problems = [f"@{name} not described" for name in parameter_names(lines, function) if f"@{name}:" not in comment]
    if not returns_void(lines, function) and "Return:" not in comment:
        problems.append("no Return: line")
    return problems
