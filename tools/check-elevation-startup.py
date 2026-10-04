#!/usr/bin/env python3
"""Keep Lapy's startup exchange ahead of every ProsperoEden worker thread."""
from pathlib import Path


source = (Path(__file__).resolve().parents[1] / "headless/main.cpp").read_text()
main = source.index("int main(")
request = source.index("elevation::request", main)
prefix = source[main:request]
for forbidden in ("Stall::Start", "std::thread", "std::jthread", "pthread_create"):
    assert forbidden not in prefix, f"{forbidden} starts before Lapy elevation"
print("Single-threaded Lapy startup order PASS")
