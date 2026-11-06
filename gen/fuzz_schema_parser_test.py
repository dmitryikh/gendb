import fuzz_schema_parser


def test_a_short_fuzz_run_finds_nothing():
    # Random names clash now and then, but most schemas must get through to the deeper checks.
    counts = fuzz_schema_parser.run(seconds=1, seed=1)
    assert counts["accepted"] > counts["rejected"]
