"""Functional tests for Variant 10 (registry cleanup of expired records).
Run from anywhere:  python tests/run_tests.py
Build first:        python scripts/build.py
The tests use Run_99, so they never touch the data of the experiment runs.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
from lab06_common import (DEFAULT_EXE_PATH, DEFAULT_STUDENT_ID, extract_csv_rows,  # noqa: E402
                          find_log_line, parse_key_values, run_program)

TEST_RUN_ID = 99
REFERENCE_TIME = "2026-03-01T00:00:00Z"


def program(*arguments):
    """Runs the program in the test learning area (Run_99)."""
    return run_program(["--student", DEFAULT_STUDENT_ID, "--run", TEST_RUN_ID,
                        "--log", "build/tests.log", *arguments])


@unittest.skipUnless(sys.platform == "win32", "registry tests need Windows")
@unittest.skipUnless(DEFAULT_EXE_PATH.exists(), "build the program first: python scripts/build.py")
class RegistryCleanupTests(unittest.TestCase):

    def setUp(self):
        program("--mode", "reset")

    def tearDown(self):
        program("--mode", "reset")

    def clean_statistics(self, *extra_arguments):
        result = program("--mode", "clean", "--now", REFERENCE_TIME, *extra_arguments)
        self.assertEqual(result.returncode, 0, result.stdout)
        return parse_key_values(find_log_line(result.stdout, "REG_CLEAN"))

    def test_selftest_all_validity_periods(self):
        """Expired records are deleted, valid ones are kept (boundary records included)."""
        for validity_days in (1, 7, 30):
            for record_count in (3, 500):
                with self.subTest(validity_days=validity_days, record_count=record_count):
                    result = program("--mode", "selftest", "--ttl", validity_days,
                                     "--count", record_count, "--repeat", 1)
                    self.assertEqual(result.returncode, 0, result.stdout)
                    row = extract_csv_rows(result.stdout)[1].split(",")
                    self.assertEqual(row[-2:], ["0", "0"])   # nothing wrongly kept / deleted

    def test_dry_run_deletes_nothing(self):
        program("--mode", "gen", "--ttl", 7, "--count", 50, "--now", REFERENCE_TIME)
        statistics = self.clean_statistics("--dry-run")
        self.assertGreater(statistics["expired"], 0)
        self.assertEqual(statistics["deleted"], 0)
        self.assertEqual(statistics["kept"] + statistics["expired"], 50)

    def test_cleanup_deletes_expired_and_is_idempotent(self):
        program("--mode", "gen", "--ttl", 7, "--count", 50, "--now", REFERENCE_TIME)
        first = self.clean_statistics()
        self.assertGreater(first["deleted"], 0)
        self.assertEqual(first["deleted"], first["expired"])
        second = self.clean_statistics()
        self.assertEqual(second["deleted"], 0)
        self.assertEqual(second["scanned"], first["kept"])

    def test_boundary_records(self):
        """Age == validity period is expired; age == validity period - 1 s is valid."""
        program("--mode", "gen", "--ttl", 1, "--count", 3, "--now", REFERENCE_TIME)
        listing = program("--mode", "list", "--now", REFERENCE_TIME).stdout
        self.assertIn("id=rec_00001", find_log_line(listing, "id=rec_00001"))
        self.assertIn("state=EXPIRED", find_log_line(listing, "id=rec_00001"))   # age == ttl
        self.assertIn("state=VALID", find_log_line(listing, "id=rec_00002"))     # age == ttl - 1 s
        self.assertIn("state=EXPIRED", find_log_line(listing, "id=rec_00003"))   # age == ttl + 1 s

    def test_later_reference_time_expires_everything(self):
        program("--mode", "gen", "--ttl", 7, "--count", 50, "--now", REFERENCE_TIME)
        result = program("--mode", "clean", "--now", "2026-04-01T00:00:00Z")
        statistics = parse_key_values(find_log_line(result.stdout, "REG_CLEAN"))
        self.assertEqual(statistics["kept"], 0)
        self.assertEqual(statistics["deleted"], 50)

    def test_invalid_arguments_are_rejected(self):
        self.assertEqual(program("--mode", "clean", "--ttl", 0).returncode, 2)
        self.assertEqual(program("--mode", "no_such_mode").returncode, 2)
        self.assertEqual(program("--mode", "clean", "--now", "not-a-date").returncode, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
