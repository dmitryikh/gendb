"""Turn Google Benchmark JSON with repetitions into a compact Markdown report."""

import argparse
import json
from pathlib import Path

CASES = ("Position", "AccountFull", "AccountSparse", "WideFull64", "WideSparse8of64")
FORMATS = ("Message", "FlatBuffers")
OPERATIONS = (
    ("Open", "Open"),
    ("ReadHot", "Read hot"),
    ("ReadCold", "Read cold"),
    ("Build", "Build"),
    ("SerializeOnly", "Serialize only"),
    ("PatchSameSize", "Patch same size"),
    ("RebuildVariable", "Rebuild variable"),
    ("EndToEnd", "End to end"),
    ("Verify", "Verify"),
)
NANOSECONDS = {"ns": 1, "us": 1000, "ms": 1000000, "s": 1000000000}


def summarize(path: Path) -> str:
    document = json.loads(path.read_text())
    context = document["context"]
    medians = {}
    for row in document["benchmarks"]:
        if row.get("error_occurred"):
            raise ValueError(f"Benchmark failed: {row}")
        if row.get("aggregate_name") == "median":
            medians[row["run_name"]] = row
    if not medians:
        raise ValueError("Expected repeated benchmarks with median aggregates")

    def cell(name):
        row = medians.get(name)
        return "—" if row is None else f"{row['cpu_time'] * NANOSECONDS[row['time_unit']]:.2f}"

    lines = [
        "# Message benchmarks",
        "",
        f"Recorded at `{context['date']}`. Median CPU nanoseconds per operation; "
        "[methodology](../README.md).",
        "",
        "| Record | Message bytes | FlatBuffers bytes |",
        "| --- | ---: | ---: |",
    ]
    for case in CASES:
        sizes = [int(medians[f"{case}/{name}/ReadHot"]["serialized_bytes"]) for name in FORMATS]
        lines.append(f"| {case} | {sizes[0]} | {sizes[1]} |")
    lines.append("")
    for case in CASES:
        lines += [
            f"## {case}",
            "",
            "| Format | " + " | ".join(label for _, label in OPERATIONS) + " |",
            "| --- |" + " ---: |" * len(OPERATIONS),
        ]
        for name in FORMATS:
            values = [cell(f"{case}/{name}/{op}") for op, _ in OPERATIONS]
            lines.append(f"| {name} | " + " | ".join(values) + " |")
        lines.append("")
    lines += [
        "## Generated classes",
        "",
        "| Operation | Position |",
        "| --- | ---: |",
        f"| Build | {cell('Position/Generated/Build')} |",
        f"| Read cold | {cell('Position/Generated/ReadCold')} |",
        "",
    ]
    maximum_cv = max(row["cpu_time"] for row in document["benchmarks"]
                     if row.get("aggregate_name") == "cv")
    lines += [
        "## Run context",
        "",
        f"- Logical CPUs: {context['num_cpus']}; the runner is pinned to one CPU.",
        f"- Repetitions per benchmark: {next(iter(medians.values()))['repetitions']}.",
        f"- Benchmark library build: `{context.get('library_build_type', 'not reported')}`.",
        f"- Largest CPU-time coefficient of variation: {maximum_cv:.1%}.",
        "- Warm-cache, single-threaded microbenchmarks, not database throughput.",
        "",
    ]
    return "\n".join(lines)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(summarize(args.input))
