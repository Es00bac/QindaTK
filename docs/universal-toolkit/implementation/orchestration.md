<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Sol execution and delegation rules

**These instructions are for the future Sol execution session.** This document
does not launch agents. [Plan](README.md) · [Worker templates](task-templates.md)

## Authority and model assignments

| Role | Model | Owns | Boundary |
| --- | --- | --- | --- |
| Orchestrator, architect, integrator | GPT 6 Sol (`gpt-6-sol`) | Plan, graph, contracts, backend work, review, integration, evidence and final completion | One root owner for the whole program |
| Bounded implementation worker | GPT 6 Luna (`gpt-6-luna`) | Specified controls, adapters with fixed contracts, fixtures, documentation and focused fixes | Does not redefine architecture or coordinate other workers |
| Hard-problem consultant | GPT 6 Astra (`gpt-6-astra`) | One difficult algorithm, lifetime/concurrency issue or design decision | Returns findings or a bounded patch to Sol; never becomes orchestrator |

These are the user's requested assignments. No pricing, relative benchmark or
account-availability claims are needed to use this plan. Sol verifies the actual
model used for each worker. If the environment cannot provide a requested model,
report that specific limitation and continue independent work; do not silently
substitute models or pretend that a prompt changed the root model.

Sol is also the implementation lead for difficult packages. “Sol lead” does not
mean another coordinating agent must be spawned. Sol does the hard core work and
delegates independent presentation/fixture tasks once their contracts are stable.

## Route tasks deliberately

| Task shape | Route |
| --- | --- |
| New ownership, transaction, document, precision, thread or backend contract | Sol designs and implements the initial proof |
| Template control using an accepted service API and existing visual pattern | Luna, typically one control or a small tightly related set |
| Focused source defect with a reproduction and bounded files | Luna; Sol reviews API/compatibility impact |
| Deterministic model fixture or failure harness from a fixed contract | Luna |
| Catalog/parser/documentation work with clear schema and examples | Luna, with Sol reviewing public API claims |
| Native integration, renderer, realtime audio, codecs, workbook/layout algorithms | Sol; split simple adapters/UI to Luna after a working proof |
| A specifically identified hard blocker with a minimized reproducer | Astra consultation under the escalation rule below |
| Final integration and acceptance | Sol |

Before assignment, reduce a package into tasks with one observable outcome, an
explicit file boundary and one acceptance statement. A package covering six
controls is usually several Luna tasks. An engine package needs a prototype,
interface, implementation and qualification sequence; do not assign its entire
unspecified backend to a worker as “make this universal.”

## Efficient concurrency

1. Inspect actual agent slots and active builds. With four slots available, a
   useful default is Sol plus two Luna workers, keeping one slot available for
   an on-demand Astra consultation. Do not create idle consultants.
2. Use an extra Luna only when independent work exists and Sol can review the
   results without creating a backlog. Parallelize by accepted contract and
   non-overlapping responsibility, not by giving multiple workers the same file.
3. Keep the active queue small: one principal Sol backend problem and a few
   bounded Luna tasks. Integrate before starting many consumers of an unstable API.
4. Avoid duplicate full builds and repeated broad tests. Use one coordinated
   integration build and focused worker checks as needed, each with its own build
   directory and the exact required job/load policy.
5. Schedule expensive builds sequentially if contention is the issue. Never
   solve contention by reducing configured build limits. Do not terminate an
   existing build that the user asked to let finish.
6. While a build/worker runs, Sol can review docs, prepare the next contract or
   inspect a separate failure. Keep user updates concise and frequent.

Agent count is not compiler job count. Build parallelism is governed by the
explicit user policy below, regardless of the number of active agents.

## Shared contracts and file ownership

Sol owns changes to shared build files, package exports, Theme role tables,
public contract headers, common model schemas, docs/controls.md, docs/layout.md,
docs/decisions.md, the catalog and the execution ledger. Workers may propose
patches for these, but Sol integrates them serially.

Workers own explicitly assigned implementation files and focused test/example
files. Use distinct worktrees and build directories. Before creating a worker
worktree, capture a faithful integration snapshot including necessary uncommitted
work. Starting from the old HEAD without the user's existing edits is incorrect.
Exclude build caches and unrelated artifacts from snapshots deliberately.

Record each worker's base commit/snapshot, allowed paths and dependencies. A
worker that discovers necessary changes outside its boundary returns the finding
to Sol and continues independent authorized work. Sol revises boundaries or
implements the shared change; the worker does not edit another agent's files.

Write complete files. Reconfigure after adding QML because the file list is
globbed. Keep hand-written files at or below 500 non-blank lines and include the
SPDX header. Never revert user edits to make a patch apply.

## One delegation cycle

1. **Select:** choose a ready package whose dependencies are accepted in the
   integration result. Confirm the smallest valuable subtask and owner model.
2. **Specify:** write the task packet from [the template](task-templates.md),
   including source contracts, allowed files, observed baseline and acceptance.
3. **Isolate:** create the snapshot/worktree/build directory and give the worker
   exact locations. Use focused context rather than the full historical thread
   when supported; include the actual instructions it needs.
4. **Implement:** worker reads the local instructions and relevant docs, edits
   only its scope and runs meaningful focused verification under the build policy.
5. **Return:** worker supplies complete patches/commits, commands/results,
   evidence paths, known limits and outstanding dependencies. It does not merge.
6. **Review:** Sol checks source, contracts, tests, resource lifetime, accessibility
   and documentation. A worker's “done” message is not acceptance evidence.
7. **Integrate:** Sol resolves conflicts in the owned integration worktree,
   reconfigures as needed and runs the required integration gates.
8. **Accept:** update ledger/manifest/evidence together. Reuse the established
   pattern for the next bounded task; revise it if the integration exposed a flaw.

## Astra escalation rule

Use Astra when Sol can state an important unresolved problem precisely and at
least one of these conditions holds:

- A correctness-critical algorithm lacks a convincing design after a concrete
  prototype: exact time, pagination, calculation, geometry, synchronization.
- A minimized resource-lifetime, private Qt, realtime or GPU issue remains after
  targeted diagnosis; the problem crosses interfaces in a way that makes local
  trial-and-error expensive.
- Competing designs have materially different compatibility, precision or
  performance outcomes and a focused expert evaluation can settle the choice.

Send the exact question, small source/reproducer, measured evidence, constraints,
attempts and required deliverable. Ask for a recommendation with reasoning and
verification cases, or a patch restricted to named files. Sol tests and accepts
the result. Escalation does not transfer orchestration or authorize an unrelated
redesign. Do not use Astra as a routine second reviewer for every component.

After two unsuccessful repair cycles on the same bounded Luna task, return it to
Sol with the reproducer. Sol fixes the specification, handles the issue locally
or makes a justified Astra consultation. Avoid repeatedly reissuing an unchanged
prompt. Difficult work remains required; changing models is a diagnosis tool.

## Build policy — copy into every delegated build/packaging task

The user's global policy takes precedence over less strict wording in local
examples:

> Never override the user's configured system MAKEOPTS. For Portage, inherit
> its configuration unchanged; do not supply command-local MAKEOPTS, alter
> /etc/portage, or introduce an environment wrapper to change job/load limits.
> Read the actual value with `portageq envvar MAKEOPTS` when needed; do not use a
> remembered, guessed or hard-coded value. For direct builds outside Portage,
> use the equivalent configured job **and load** limits. Do not reduce them with
> flags, environment variables, presets, Ninja options or job pools. If a
> demonstrated failure requires different limits, explain the evidence and ask
> the user first; only an explicit user instruction permits an exception. Do not
> interrupt or restart a build merely to change its limits when the user asked
> to let that build finish. Include this policy in delegated build/packaging
> tasks and explicitly notify any already running managers of it.

At execution time, read the configuration, then translate its job/load settings
to the selected direct build backend without altering the configuration. For
Ninja, pass the corresponding job and load values obtained from that read. Do
not copy a numeric value from these documents; none is provided as an override.
If configuration cannot be read, investigate the environment before guessing.

Use the documented CMake configuration, ctest and qtk-preview workflow. Isolated
build directories follow `build/<task>`; direct backend arguments must preserve
the observed system limits. Headless tests and actual GPU/device tests serve
different purposes and must be labeled accordingly.

## Runtime and repository invariants

- Every QML file imports `QindaTK as Tk`; toolkit references stay qualified.
- Controls use QtQuick.Templates and Theme roles. New roles update derivation,
  properties, role tables, tests and docs. Domain content values remain distinct.
- Layout uses Flex/Grid/Stack/Box/Scroll; preserve their implicit-size ownership.
- DockModel remains the C++ arrangement authority; views do not duplicate state.
- Controls provide tooltip/name paths, appropriate accessibility roles/actions
  and stable documented objectNames. Icon-only controls have tooltips.
- QML/GUI, worker, render and realtime ownership obey the accepted interfaces.
- Each actual change updates applicable operational docs/catalog/examples.
- Visual/layout claims require geometry dumps and screenshots that are opened
  and inspected. Instantiation or a screenshot file existing is insufficient.
- Keep qinda jobs silent; do not send spoken updates to qinda-top. Do not contact
  real email/chat recipients or publish externally as an implementation test.

## Decisions, blockers and persistence

Sol resolves routine implementation choices and records durable ADRs. Internal
contract acceptance and milestones are Sol's responsibility, not recurring user
permission gates. Ask for genuinely missing requirements or a necessary external
action only after preparing the concrete work and checking existing authority.
Continue independent ready work while awaiting input or an environment.

Each pause/context handoff records: current integration commit/snapshot, active
workers and files, running builds, accepted packages, immediate next task and
specific unresolved blockers. On resumption, reconcile these records with the
actual tree/processes before launching duplicate work.

The ledger represents reality. `blocked` means the package has a named missing
prerequisite; it does not count as accepted. Sol continues until all mandatory
scope is complete or clearly reports what external condition prevents progress.
Do not convert a capability stub, unavailable runtime check or disabled menu item
into a completion claim.
