# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
trace.py - traceability: what a suite must cover, the tests that cover it, and
the suite's TRACEABILITY.md, which says so.

A suite lists its items (Item), most read from the component's sources so a
new one cannot be missed: the commands a dispatcher serves, the events a
device raises, the keys a prefs reader knows (function_body() and
case_labels() help). A test covers an item when one of its
@pytest.mark.covers() labels is the item's name, or matches one of the item's
patterns.
"""
from __future__ import annotations

import ast
import re
from dataclasses import dataclass, field
from pathlib import Path

SHOWN = 3                     # tests named per item; the rest counted


@dataclass
class Item:
    """One thing the suite must cover."""

    group: str
    name: str
    note: str = ""
    patterns: list[str] = field(default_factory=list)   # labels matching one cover it too
    tests: list[str] = field(default_factory=list)

    def covered_by(self, label: str) -> bool:
        """Whether @label covers this item."""
        return label == self.name or any(re.search(pattern, label) for pattern in self.patterns)


def labels(tests_dir: Path) -> dict[str, list[str]]:
    """Every covers() label in the suite at @tests_dir: label -> its tests, each as 'file::function'."""
    found: dict[str, list[str]] = {}
    for path in sorted(Path(tests_dir).rglob("test_*.py")):
        if ".venv" in path.parts:
            continue
        for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                for label in _covers(node):
                    found.setdefault(label, []).append(f"{path.relative_to(tests_dir)}::{node.name}")
    return found


def _covers(node) -> list[str]:
    """The covers() labels on a test function."""
    return [argument.value for decorator in node.decorator_list
            if isinstance(decorator, ast.Call) and getattr(decorator.func, "attr", None) == "covers"
            for argument in decorator.args if isinstance(argument, ast.Constant) and isinstance(argument.value, str)]


def function_body(source: Path, signature: str) -> str:
    """The body of the function whose definition starts with @signature in the C file @source."""
    text = Path(source).read_text(encoding="utf-8")
    start = text.index(signature)
    return text[start:text.index("\n}\n", start)]


def case_labels(source: Path, signature: str) -> list[str]:
    """The case labels of the dispatcher whose definition starts with @signature in @source."""
    return re.findall(r"case\s+(\w+):", function_body(source, signature))


def trace(items: list[Item], tests_dir: Path) -> list[Item]:
    """@items, each with the tests of the suite at @tests_dir that cover it."""
    found = labels(tests_dir)
    for item in items:
        item.tests = sorted({test for label, tests in found.items() if item.covered_by(label) for test in tests})
    return items


def uncovered(traced: list[Item]) -> list[str]:
    """The items no test covers, each as 'group: name'."""
    return [f"{item.group}: {item.name}" for item in traced if not item.tests]


def markdown(traced: list[Item], title: str) -> str:
    """TRACEABILITY.md's text, under @title (the component's name)."""
    lines = [f"# {title}: traceability", "",
             "Written by the suite's host tier (`run.sh host` rewrites it): every item the suite must cover, "
             "with the tests that cover it.", "", f"**Uncovered: {len(uncovered(traced))}** of {len(traced)} items.", ""]
    for group in dict.fromkeys(item.group for item in traced):
        lines += [f"## {group}", "", "| Item | Tests |", "|---|---|"]
        for item in (i for i in traced if i.group == group):
            shown = ", ".join(f"`{test}`" for test in item.tests[:SHOWN])
            more = f" and {len(item.tests) - SHOWN} more" if len(item.tests) > SHOWN else ""
            name = f"{item.name} ({item.note})" if item.note else item.name
            lines.append(f"| {name} | {shown + more if item.tests else '**none**'} |")
        lines.append("")
    return "\n".join(lines)
