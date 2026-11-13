# Task: <short name>

Date: <YYYY-MM-DD>   Branch: `feat/<task>`   Base: `main`

## Requirement
<One paragraph: what must be true after this change, and why. Say what is measured
or observed, not just "improve" or "optimise".>

## Evidence for the need
<Audit finding ID, deferred item ID (docs/DEFERRED_*.md), bench result, or log. If there is
no measurement, say so: an optimisation with no baseline cannot be accepted, only reviewed.>

## Files in scope
- <path>
- <path>

## Protected files this task may touch (default: none)
<Name each one and exactly what may change. If this is empty and the work seems to need one, the
agent must stop and report.>

## Out of scope
- Anything not listed above
- Pyro, arming, interlock, watchdog configuration, telemetry packet format, pin assignments

## Acceptance
- [ ] Host tests: `<count>` pass, 0 fail (`powershell -File test\host\run_host_tests.ps1`)
- [ ] New tests cover: <behaviors>
- [ ] Compile check exits 0, zero warnings in `src/`
- [ ] `check_protected_paths.sh` prints OK
- [ ] <Measurable criterion, e.g. flash size not above X, loop time not above Y, stack high-water not below Z>

## Needs hardware (cannot be accepted here)
- <what the bench must confirm later>

## Notes and constraints
<Links to the audit or deferred entries, datasheet pages, known pitfalls.>
