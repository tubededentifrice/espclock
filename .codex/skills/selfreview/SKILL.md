---
name: selfreview
description: Skeptically review recent ESPClock repository changes against their intent, autofix defects and omissions, and run the applicable project quality gates before handoff. Use after completing an implementation plan or non-trivial firmware, hardware, test, or documentation edit; invoke with `autofix` as the mandatory final step of every implementation plan.
---

# Self-review ESPClock changes

Review task-owned changes adversarially before handoff. Look for concrete
defects, omissions, and regressions rather than opportunities for unrelated
cleanup.

## Select the mode

- **Interactive**: report findings, ask the user which findings to fix, apply
  the selected fixes, and run the applicable gates.
- **Autofix**: skip plan mode and user triage. Fix every BUG, MISSING, and RISKY
  finding in scope, then run the applicable gates. Use this mode when invoked
  as `$selfreview autofix`, at the end of an implementation plan, or for an
  explicit hands-off request.

Do not commit, push, stage files, or recursively invoke selfreview. Do not widen
the user's authorization: stop for destructive work, external changes, or a
material product decision that still needs the user.

## Phase 1: Establish scope and intent

Read `AGENTS.md` and the original task or plan. Inspect:

```sh
git status --short
git diff
git diff --cached
```

If `HEAD` exists, also inspect `git log --oneline -5`. An unborn repository is
valid during initial scaffolding; do not treat its lack of commits as a review
failure. If the branch contains task commits, also review the diff from its
merge base with the target branch. Include task-owned untracked files because
`git diff` does not show them. Exclude unrelated pre-existing changes and never
overwrite another contributor's work.

List the intended behavior, affected hardware or protocols, user-visible
effects, and claimed verification. Review against that intent.

## Phase 2: Investigate

In interactive mode, enter plan mode or the runtime's read-only equivalent.
In autofix mode, remain in the current execution mode and continue without
asking the user.

Read the full changed files and their callers, tests, and affected
documentation. Search for shared constants and duplicated contracts before
judging a change in isolation.

## Phase 3: Review adversarially

### Correctness and timekeeping

- Trace normal, empty, invalid, timeout, disconnect, reboot, and wraparound
  paths.
- Use subtraction-based monotonic millisecond comparisons so `millis()` wrap
  remains safe.
- Keep scheduling time separate from UTC epoch and civil display time.
- Keep the DS3231 in UTC and apply only a validated, confirmed timezone or UTC
  offset for display.
- Check epoch bounds, timezone bounds and syntax, payload sizes, buffer
  termination, integer widths, and conversions.

### State machines and concurrency

- Keep network and peripheral operations bounded and non-blocking where
  practical.
- Verify BLE, portal, and NTP updates are arbitrated atomically in their
  documented priority order. A lower-priority source must not overwrite or
  strand a pending BLE update.
- Verify a failed open-network BSSID cannot be retried in the same boot.
- Check callback/task ownership, shared-state synchronization, and cleanup
  after partial initialization or failure.
- Check `String` and container growth, repeated flash writes, heap lifetime,
  stack use, and behavior across long runtimes.

### Hardware and failure independence

- Reject attached-module use of GPIO2, GPIO8, GPIO9, GPIO18, or GPIO19.
- Preserve TM1637 CLK GPIO4 and DIO GPIO3 and shared I2C SDA GPIO6 and SCL
  GPIO7 unless the user explicitly changes the hardware contract.
- Keep DS3231 and BH1750 at 3.3 V. Never imply that a CR2032 may be charged;
  retain the ZS-042 charging-path warning.
- Ensure missing RTC, BH1750, BLE, Wi-Fi, DNS, HTTP, or other optional
  facilities cannot stop a valid clock display.
- Mark electrical, RF, brightness, enclosure, and long-duration behavior that
  still requires physical bench verification.

### Security and privacy

- Treat phone/browser time, timezone, BLE writes, SSIDs, BSSIDs, network
  payloads, and HTTP requests as untrusted.
- Check authentication or bonding assumptions, bounds, parser failure, timeout
  behavior, and replay or stale-update paths.
- Do not log secrets, authentication material, full device identifiers, or
  precise location.
- Never claim generic BLE pairing universally transfers phone time or Internet
  access. For the explicitly authorized captive-portal automator, verify every
  documented bound, synthetic-data-only rule, and cancellation path; never
  transmit credentials, payment details, user/family data, stable identifiers,
  or retained portal state over open Wi-Fi.

### Completeness and consistency

- Require tests for new logic and negative paths.
- Keep code, `README.md`, `docs/design-review.md`,
  `docs/hardware-research.md`, wiring documentation, and acceptance tests
  consistent when their contract changes.
- Check BOM, pins, dependencies, status indications, first-use flow,
  limitations, safety notes, and sourcing dates when affected.
- Pin added PlatformIO libraries and explain them in the README.
- Check naming, includes, dead code, warnings, memory use, and surrounding
  project conventions.

## Phase 4: Record findings

Use these severities:

- **BUGS**: causes errors, unsafe behavior, or wrong results.
- **MISSING**: leaves the requested implementation incomplete.
- **RISKY**: fails under a specific credible condition.
- **NITPICKS**: optional polish with no material effect.

Give every finding a `file:line`, manifestation, and concrete fix. Do not pad
the report with generic advice or invent findings. An empty report is valid.

## Phase 5: Triage

In interactive mode, ask whether to fix all material findings, bugs only, show
details, or accept the changes. In autofix mode, do not ask: fix all BUGS,
MISSING, and RISKY findings within the original task's authority. Apply a
NITPICK only when it is clearly safe and tightly scoped.

After each fix, re-check the affected path and update the findings record.

## Phase 6: Run quality gates

Discover any narrower checks from the changed modules, then run gates
proportionate to the change.

For firmware, tests, dependencies, or PlatformIO configuration, require:

```sh
UV_CACHE_DIR=/private/tmp/espclock-uv-cache uv run --locked python tools/pio.py test -e native
UV_CACHE_DIR=/private/tmp/espclock-uv-cache uv run --locked python tools/pio.py run
```

Review compiler warnings, native-test results, flash/RAM usage, and pin
assignments. Do not hide a failure by weakening or skipping a test.

For documentation- or agent-instruction-only changes, validate the changed
artifacts, links, commands, and cross-file consistency. Run the firmware gates
too when the documentation asserts changed firmware behavior or build
configuration.

Report:

- findings fixed and any intentionally skipped NITPICKS;
- exact checks run and their results;
- remaining physical hardware verification;
- any blocker that could not be fixed within the user's authority.

Return after the gates. Leave staging, commits, pushes, and release actions to
the caller.
