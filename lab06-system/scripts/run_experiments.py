"""Runs the Variant 10 experiments and collects the results into data/results.csv.

Standard experiment (configurations A, B, C = 1, 7, 30 days):
    python scripts/run_experiments.py
Own data set / teacher's extra experiment, for example:
    python scripts/run_experiments.py --seed 777 --now 2026-06-15T12:00:00Z --record-counts 5000 --output data/results_own.csv
"""
import argparse
import sys
from pathlib import Path

from lab06_common import (DEFAULT_EXE_PATH, DEFAULT_STUDENT_ID, PROJECT_ROOT,
                          extract_csv_rows, run_program)


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--student", type=int, default=DEFAULT_STUDENT_ID)
    parser.add_argument("--run", type=int, default=1, help="RUN_ID used in the registry key name")
    parser.add_argument("--validity-days", type=int, nargs="+", default=[1, 7, 30])
    parser.add_argument("--record-counts", type=int, nargs="+", default=[100, 1000, 10000])
    parser.add_argument("--repeat", type=int, default=3, help="runs per configuration (at least 3)")
    parser.add_argument("--seed", type=int, default=None, help="default: 1000 + STUDENT_ID * 97")
    parser.add_argument("--now", default=None, help="reference time, e.g. 2026-03-01T00:00:00Z")
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE_PATH)
    parser.add_argument("--output", type=Path, default=PROJECT_ROOT / "data" / "results.csv")
    return parser.parse_args()


def main():
    options = parse_arguments()
    options.output.parent.mkdir(parents=True, exist_ok=True)

    header = None
    all_rows = []
    for validity_days in options.validity_days:
        for record_count in options.record_counts:
            print(f"validity_days={validity_days} record_count={record_count} ...", flush=True)
            arguments = ["--mode", "selftest", "--student", options.student, "--run", options.run,
                         "--ttl", validity_days, "--count", record_count,
                         "--repeat", options.repeat, "--log", "build/lab06.log"]
            if options.seed is not None:
                arguments += ["--seed", options.seed]
            if options.now is not None:
                arguments += ["--now", options.now]

            result = run_program(arguments, options.exe)
            if result.returncode != 0:
                sys.stderr.write(result.stdout + result.stderr)
                sys.exit(f"selftest FAILED (validity_days={validity_days}, record_count={record_count})")

            rows = extract_csv_rows(result.stdout)
            header = header or rows[0]
            all_rows += [row for row in rows if not row.startswith("validity_days")]

    options.output.write_text("\n".join([header] + all_rows) + "\n", encoding="utf-8")
    print(f"Results written to {options.output} ({len(all_rows)} rows)")


if __name__ == "__main__":
    main()
