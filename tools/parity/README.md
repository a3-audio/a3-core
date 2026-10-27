# Python parity guard

`tests/core/parity/cases.tsv` lists every test case of a3-core's Python suite. Columns, read by
header name: `id` (`module.Class.test`), `verdict`, `group` (`stable` or `G`), `from` (a3-core SHA
it was ported from), `task`.

Rows whose id starts with `beat-analyzer.` are beat-analyzer's own tests (`beat-analyzer.<file>.<test>`,
`from` = its commit) for what the engine took over from it: the /vu meters (Task 24).

Verdicts: `port:<Suite>.<Name>` (must exist as `TEST`/`TEST_F` in `tests/core/*.cpp` or
`tests/engine/*.cpp`; only `TEST` and `TEST_F` count, `TEST_P` does not; commented-out, `#if 0` and
`DISABLED_` tests do not count either), `retired:<reason>`, `stays:<reason>` (a reason is required).

Run from the repo root with a Python that has `unittest` (the dev venv):

    # the real check: fails listing every port: case without a C++ test
    /home/aaa/.venv/bin/python3 -m unittest tests.parity.test_cases_are_ported.RealParityCheck

    # the guard's own self-tests (temporary tsv + C++ file), must always pass
    /home/aaa/.venv/bin/python3 -m unittest tests.parity.test_cases_are_ported.GuardSelfTests

Narrowing the real check:

    A3_PARITY_ONLY=test_core_web  ...   # one Python module (first dotted part of the id)
    A3_PARITY_GROUP=stable        ...   # one group: stable | G
