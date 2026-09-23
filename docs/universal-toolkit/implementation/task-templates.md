<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Task, consultation and evidence templates

[Plan](README.md) · [Orchestration](orchestration.md) · [Acceptance](acceptance.md)

Sol fills these templates with concrete values. Angle-bracket fields are
placeholders, not shell commands. Keep task packets focused on the files and
contracts needed for the outcome. Include the full build policy in every task
that can build or package; a link alone does not satisfy the user's requirement.

## Luna implementation task

```text
Role/model: bounded implementation worker, GPT 6 Luna / gpt-6-luna.
Orchestrator and integration owner: GPT 6 Sol. Do not delegate further.
Task ID: <WP-ID/subtask-ID>.
Outcome: <one observable completed behavior>.
Proposal/support IDs: <exact IDs>.

Worktree: <absolute path>.
Base commit/snapshot: <verified integration base including necessary dirty work>.
Build directory: <absolute isolated build directory>.
Accepted dependencies: <IDs and contract versions/commits>.
Allowed implementation files: <explicit files/directories>.
Allowed test/example files: <explicit files/directories>.
Shared files owned by Sol: <headers/theme/build/docs/catalog/etc.>.

Read before editing:
- Local AGENTS.md and applicable user instructions.
- <owning package, proposal anchors and existing module documentation>.
- <nearest accepted implementation and interface definitions>.

Observed baseline: <reproduction, actual result and evidence path>.
Required behavior: <state transitions, data identity, signals, errors>.
Compatibility constraints: <defaults/public API/objectNames to preserve>.
Boundary: <model/provider owns what; GUI receives what>.
Acceptance: <observable result, negative/cancel case and relevant scale>.
Required verification: <focused test, qtk-preview scene, dump/grab, backend proof>.
Documentation/example changes: <specific updates to implement/propose>.

Follow Tk imports, Templates, Theme roles, layout and accessibility rules.
Use stable documented objectNames. Include SPDX. Write complete files and keep
hand-written files <= 500 non-blank lines. Reconfigure after adding QML.
Preserve all baseline/user changes and stay within the assigned file boundary.
If the boundary or contract must change, send the specific issue to Sol; continue
independent authorized work. Do not replace a real provider with an unlabeled stub.

Mandatory build policy:
Never override configured system MAKEOPTS. Portage inherits configuration
unchanged; no command-local MAKEOPTS, /etc/portage changes or wrappers to alter
job/load limits. Read the actual value with portageq envvar MAKEOPTS when needed.
For direct builds, use equivalent configured job AND load limits. Do not guess,
hard-code remembered values, or reduce limits with flags, environment variables,
presets, Ninja options or job pools. A demonstrated failure requiring different
limits must be reported with evidence to Sol and the user; only the user can
authorize an exception. Do not interrupt/restart an existing build merely to
change its limits when the user asked to let it finish. Include this policy in
any delegated build/packaging work and notify already running managers.

Audio: no spoken updates to qinda-top; scheduled/unattended work remains silent.
Do not contact real recipients or publish externally as a verification step.

Return the result in the format below. Do not merge, push or accept your own work.
After two unsuccessful repair cycles on the same issue, return the minimized
reproducer and findings to Sol rather than repeating the same approach.
```

## Worker return format

```text
Task ID and actual model:
Status: ready for review / partial / blocked.
Base and result commit(s), or complete patch location:
Files changed:
Behavior delivered and proposal IDs covered:
Contract/API changes, if any:
Commands executed, exit codes and environment:
Build MAKEOPTS read and actual equivalent job/load invocation, if built:
Tests/results, including failures and pre-existing failures:
Geometry dump lines proving layout claims:
Screenshot paths and what was inspected:
Real backend/format/device evidence, if applicable:
Docs/catalog/example updates or patches for Sol-owned files:
Known limitations and remaining work:
Dependencies or decisions needed from Sol:
```

Use precise status. “Ready for review” means the worker believes its bounded task
meets acceptance; only Sol can accept the integrated work package. A passing
unit test does not substitute for a promised real device or render check.

## Astra consultation

```text
Role/model: hard-problem consultant, GPT 6 Astra / gpt-6-astra.
Sol remains orchestrator, implementation owner and integrator.
Do not spawn agents, broaden the task or take over the execution queue.

Question: <one specific unresolved question>.
Affected package/contracts: <IDs and links>.
Constraints: <Qt versions, precision, performance, API, threading, compatibility>.
Minimal source/reproducer: <paths and exact reproduction command>.
Expected versus observed result: <data/evidence>.
Attempts and why they failed: <concise technical record>.
Options already considered: <tradeoffs and measured costs>.

Deliverable: <recommendation/algorithm/proof outline, verification cases,
or a patch limited to these named files>.
Success criterion: <what Sol can test to accept the answer>.
Return assumptions, remaining uncertainties and any compatibility impact.
```

For a patch/build consultation, append the same complete build/audio/file-policy
block from the Luna task. For a read-only design consultation, give sufficient
source and evidence to avoid an unnecessary build. Sol validates the result and
records why the escalation was worthwhile before closing the issue.

<a id="evidence-record"></a>
## Package evidence record

Create one Markdown record under a proposed `implementation/evidence/` directory
when execution begins, with an SPDX header and a stable package ID. No empty
pretend test records are pre-created by this plan.

```markdown
<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-xxx — <title>

## Scope and baseline

- Proposal/support IDs:
- Integration snapshot and starting commit:
- Existing user changes/failures relevant to this package:
- Accepted dependency contracts:
- Actual model(s), owned files and task IDs:

## Implementation and decisions

- Behavior delivered:
- Source/commits:
- ADRs and public API changes:
- Real provider/backend and version:
- Supported operations and explicit limits:

## Verification

| Check | Environment/input | Command or procedure | Result | Evidence |
| --- | --- | --- | --- | --- |
| <meaningful check> | <actual values> | <actual invocation> | <pass/fail/not run> | <link> |

Record the actual MAKEOPTS read and direct-build equivalent invocation when used.
Include geometry lines, inspected screenshots, numerical/output references,
failure/cancellation cases and relevant performance data. Label mock/real paths.

## Integration and acceptance

- Integrated commit/artifact:
- Documentation/catalog/example updates:
- Original proposal completion checks satisfied:
- Shared acceptance gate satisfied:
- Sol review result:
- Remaining work/blockers and exact unblock action:
- Accepted date, or current state:
```

## Checkpoint for context handoff

Record the integration worktree/base, current commit and dirty diff, active worker
IDs/models/worktrees/owned files, running build sessions and configured limits,
accepted packages, pending review results, blockers, last verified evidence and
the next ready task. Preserve a link to the full ledger and contracts.

On resumption Sol reads this record and checks actual state. Do not restart
already running work or count an old test result as a pass for changed source.
