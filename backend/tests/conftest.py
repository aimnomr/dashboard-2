"""Shared test fixtures.

The GEN3 corpus lives in `tests/fixtures/` rather than `logs/raw/`, deliberately.

`logs/` is gitignored: it holds per-run evidence, generated data that accumulates. These
two files are the opposite — they are *promoted* out of that directory because tests
depend on them, so they must exist on every clone. The line is provenance, not content:
`logs/raw/` is what a run happened to produce, `tests/fixtures/` is what we decided to
keep forever.

Loading is session-scoped. Parsing 1013 packets twenty times over would make the suite
slow for no gain, since nothing here mutates the corpus.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from dashboard.parser import ParseResult, parse_line

FIXTURES = Path(__file__).parent / "fixtures"

#: Both captures are bench runs on the GEN3 flight unit, recorded to SD on 2026-08-19.
#: Neither is a flight: altitude never leaves +/-1.2 m, the chute is never commanded, and
#: the GPS reports zero satellites throughout (ISS-14). They are a corpus for the *parser*
#: and for link accounting, not for flight profiles.
CORPUS_FILES = ("FLIGHT21.CSV", "FLIGHT22.CSV")


@dataclass(frozen=True, slots=True)
class Corpus:
    """One captured SD log, parsed once."""

    name: str
    lines: list[str]
    #: Every non-blank line, in file order, as the parser classified it.
    results: list[ParseResult]

    @property
    def frames(self) -> list[ParseResult]:
        return [r for r in self.results if r.kind == "frame" and r.ok]

    @property
    def failures(self) -> list[ParseResult]:
        return [r for r in self.results if r.kind == "frame" and not r.ok]

    @property
    def status_lines(self) -> list[ParseResult]:
        return [r for r in self.results if r.kind == "status"]


def _load(name: str) -> Corpus:
    # errors="replace" mirrors serial_source: corruption becomes a visible U+FFFD rather
    # than vanishing. A fixture read more leniently than the real input would hide a
    # decoding bug the live path would hit.
    text = (FIXTURES / name).read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    return Corpus(name=name, lines=lines, results=[parse_line(ln) for ln in lines])


@pytest.fixture(scope="session", params=CORPUS_FILES, ids=CORPUS_FILES)
def corpus(request: pytest.FixtureRequest) -> Corpus:
    """Each captured log in turn. Tests using this run once per file."""
    return _load(request.param)


@pytest.fixture(scope="session")
def all_corpora() -> list[Corpus]:
    """Both logs at once, for assertions about the corpus as a whole."""
    return [_load(name) for name in CORPUS_FILES]


@pytest.fixture(scope="session")
def real_line(all_corpora: list[Corpus]) -> str:
    """A single real packet, straight off the SD card, for the mutation tests.

    Damaging a captured line is worth more than inventing a broken one: the undamaged
    parts stay exactly as the firmware emits them, so a test that passes is not passing
    because the rest of the line was also made up.
    """
    return next(ln for ln in all_corpora[0].lines if ln.startswith("$"))
