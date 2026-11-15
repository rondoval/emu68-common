# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""The gates themselves (emu68test/gates.py, emu68test/trace.py): each fails on what it is there to catch."""
from pathlib import Path

import lizard
import pytest

from emu68test import gates, trace

pytestmark = pytest.mark.unit

DOCUMENTED = """/**
 * add() - Add two numbers.
 * @a: the first.
 * @b: the second.
 *
 * Return: their sum.
 */
static int add(int a, int b)
{
    return a + b;
}
"""


def analysed(text: str):
    """(lines, the first function) of a C source given as text."""
    return text.splitlines(), lizard.analyze_file.analyze_source_code("x.c", text).function_list[0]


def test_a_complete_kernel_doc_passes():
    assert gates.kerneldoc_problems(*analysed(DOCUMENTED)) == []


def test_a_function_without_kernel_doc_fails():
    assert gates.kerneldoc_problems(*analysed("/* a plain comment */\nstatic void f(void)\n{\n}\n")) == \
        ["no kernel-doc comment"]


def test_a_parameter_or_a_return_left_out_fails():
    text = DOCUMENTED.replace(" * @b: the second.\n", "").replace(" * Return: their sum.\n", "")
    assert gates.kerneldoc_problems(*analysed(text)) == ["@b not described", "no Return: line"]


def test_register_bound_parameters_are_read_by_name_and_an_attribute_line_is_skipped():
    text = ("/**\n * isr() - A server.\n * @unit: the unit.\n * @irq: unused.\n *\n * Return: 1.\n */\n"
            "EMU68_INTSERVER(isr)\nULONG isr(struct GenetUnit *unit asm(\"a1\"), ULONG irq asm(\"d0\"))\n{\n"
            "    return 1;\n}\n")
    assert gates.kerneldoc_problems(*analysed(text)) == []


def gates_over(tmp_path: Path, source: str, allow: str = "", inherited: str = "5.0:") -> gates.Gates:
    """Gates over one C file with @source in tmp_path, and an allow list holding @allow."""
    path = tmp_path / "src" / "x.c"
    path.parent.mkdir(exist_ok=True)
    path.write_text(source)
    (tmp_path / "complexity-allow.txt").write_text(allow)
    return gates.Gates(tmp_path, [path], tmp_path / "complexity-allow.txt", inherited)


@pytest.mark.parametrize("complexity, entry, problem", [
    (10, None, None),
    (11, None, "complexity 11 > 10"),
    (14, gates.Ceiling(14, "a reason"), None),
    (15, gates.Ceiling(14, "a reason"), "complexity 15 > its ceiling 14"),
    (12, gates.Ceiling(14, "a reason"), "complexity 12 < its ceiling 14: lower the ceiling"),
    (9, gates.Ceiling(14, "a reason"), "complexity 9: needs no entry in complexity-allow.txt any more"),
    (20, gates.Ceiling(20, "a reason"), "ceiling 20 > 15: only inherited ceilings are higher"),
    (20, gates.Ceiling(20, "5.0: inherited"), None),
], ids=["at the limit", "over it", "at its ceiling", "over its ceiling", "under its ceiling", "simple again",
        "a new ceiling over 15", "an inherited one"])
def test_the_complexity_rules(tmp_path, complexity, entry, problem):
    assert gates_over(tmp_path, DOCUMENTED).ceiling_finding(complexity, entry) == problem


def test_a_suite_without_an_inherited_mark_allows_no_ceiling_over_15(tmp_path):
    found = gates_over(tmp_path, DOCUMENTED, inherited="").ceiling_finding(20, gates.Ceiling(20, "5.0: inherited"))
    assert found == "ceiling 20 > 15: only inherited ceilings are higher"


def test_the_gates_name_a_file_s_findings_relative_to_the_component(tmp_path):
    undocumented = gates_over(tmp_path, "static int f(int a)\n{\n    return a;\n}\n")
    assert undocumented.kerneldoc_findings() == ["src/x.c:1 f: no kernel-doc comment"]
    assert gates_over(tmp_path, DOCUMENTED).kerneldoc_findings() == []
    assert gates_over(tmp_path, DOCUMENTED).complexity_findings() == []


def test_an_allow_list_entry_for_a_function_that_is_gone_is_a_finding(tmp_path):
    found = gates_over(tmp_path, DOCUMENTED, "src/x.c:no_such_function 12 5.0: x\n").complexity_findings()
    assert found == ["complexity-allow.txt: src/x.c:no_such_function is no function of the gates' sources"]


def test_a_malformed_allow_list_line_is_refused(tmp_path):
    path = tmp_path / "allow.txt"
    path.write_text("# a comment\nfile.c:f 12 a reason\nnot an entry\n")
    with pytest.raises(ValueError, match="allow.txt:3"):
        gates.allowed(path)


def test_an_item_is_covered_by_its_own_label_or_a_pattern():
    item = trace.Item("group", "S2_ONLINE", patterns=[r"^claim: "])
    assert item.covered_by("S2_ONLINE") and item.covered_by("claim: netdev holds")
    assert not item.covered_by("S2_ONLINE_WAIT") and not item.covered_by("S2_OFFLINE")


def test_the_table_counts_and_marks_what_no_test_covers():
    traced = [trace.Item("g", "covered", tests=["a::t"]), trace.Item("g", "bare")]
    text = trace.markdown(traced, "thing.device")
    assert trace.uncovered(traced) == ["g: bare"] and text.startswith("# thing.device: traceability")
    assert "**Uncovered: 1** of 2 items." in text and "| bare | **none** |" in text and "| covered | `a::t` |" in text


def test_the_labels_come_from_the_suite_s_test_files_and_the_items_from_a_dispatcher(tmp_path):
    (tmp_path / "unit").mkdir()
    (tmp_path / "unit" / "test_a.py").write_text(
        "import pytest\n\n@pytest.mark.covers('CMD_ONE', 'a rule')\ndef test_one():\n    pass\n")
    source = tmp_path / "dispatch.c"
    source.write_text("int dispatch(int command)\n{\n    switch (command)\n    {\n    case CMD_ONE:\n        return 1;\n"
                      "    case CMD_TWO:\n        return 2;\n    }\n    return 0;\n}\n")
    items = [trace.Item("commands", name) for name in trace.case_labels(source, "int dispatch(")]
    traced = trace.trace(items, tmp_path)
    assert [(item.name, item.tests) for item in traced] == [("CMD_ONE", ["unit/test_a.py::test_one"]), ("CMD_TWO", [])]
