# SMS-Plugins: agent instructions

SMS means SudoMetalStudio, the creator's YouTube channel. This AI-developed
project builds native Linux audio plugins. SMS-Midichopper, a live chopping
sampler, is the first plugin and is under development. Linux LV2 is primary;
DPF also provides optional VST3 and CLAP builds.

## Start small

- Read this file, then choose only the relevant entries in the
  [task index](Docs/AI/INDEX.md). Do not preload all documentation or source.
- Check `git status --short`; preserve unrelated work. Commands below run from
  the repository root. There is no root CMake project.
- Search specific paths/symbols with `rg`. Exclude build output and
  `third_party/DPF` unless the task concerns the framework. Read bounded excerpts.
- Prefer the cheapest capable model/sub-agent for bounded searches, edits,
  tests, and reviews when delegation reduces total cost. Give it a precise
  task, owned files, relevant instructions, and acceptance checks, not the whole
  conversation. Request a short result with file references and test evidence.
- Avoid overlapping agents, repeated exploration, and delegating trivial work.
  Use larger models for ambiguity, architecture, real-time/concurrency reasoning,
  or failed smaller-model attempts. If model selection/delegation is unavailable,
  work locally with the same narrow scope; do not claim a cheaper model was used.

## Development rules

- Read the plugin's [vision](SMS-Midichopper/Docs/VISION.md) before feature/design
  work. Distinguish intended capabilities from implemented behavior.
- Keep engine, format adapter, and UI separate. Keep `Common-Src` free of DPF
  and plugin-specific dependencies; DPF UI reuse belongs in `Common-UI`.
- Audio callbacks must not allocate, block, lock, perform I/O, or throw.
  Preserve host parameter identities, saved-state compatibility, and sample
  timing; inspect the affected contracts before changing them.
- Make the smallest complete change. Run relevant existing tests; add regression
  coverage for changed behavior. Use jalv for applicable LV2 audio/MIDI/UI/state
  integration checks; see [testing](Docs/AI/TESTING.md). Report skipped checks
  and their actual reason. A successful launch alone is not an audio/UI test.

## Documentation is part of the change

- Keep affected docs accurate and up to date in the same change as code,
  commands, architecture, or workflow changes. Fix stale claims when encountered.
- AI docs contain concise contracts, file maps, commands, and pitfalls. Human
  docs contain short setup, usage, product direction, and contribution guidance.
  Use clear sentences; do not compress away information a small model needs.
- Maintain one authoritative home per fact and link to it. Index every new or
  moved Markdown page in the task index; repair links and remove obsolete text.
  No session transcripts, duplicated plans, or speculative features as facts.
- Limits: this file 600 words; index 350; Copilot adapter 100; human pages 600;
  AI reference pages 900. These are ceilings, not targets. Split by task only
  when useful; do not create many pages merely to bypass limits.
- Run `python3 scripts/check_docs.py` and `git diff --check` before handoff.
  The checker verifies structure, not factual accuracy: compare affected claims
  with source/tests and report documentation changes, or why none were needed.

## GitHub and handoff

- GitHub is the remote, issue tracker, CI, and release system. Use issues for
  backlog and discussion rather than duplicating them in repository docs.
- Local commits are allowed. **Agents must never push to any remote**, including
  branches or tags, directly or through APIs, scripts, or delegated agents.
  Do not use a hosted agent workflow that automatically pushes changes.
- Human maintainers push and publish. The
  [release procedure](Docs/RELEASING.md) is for them; agents may prepare local
  changes. Do not publish releases or trigger publishing workflows.
- Handoff briefly: what changed, validation results, remaining limitations.
