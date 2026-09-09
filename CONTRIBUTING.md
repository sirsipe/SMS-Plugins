# Contributing

SMS-Plugins is developed with AI and human testing. Report bugs and propose work
in [GitHub Issues](https://github.com/sirsipe/SMS-Plugins/issues). For bugs,
include the plugin version, Linux/host versions, reproduction steps, and expected
versus observed behavior. Feature proposals must fit the plugin's
[vision](SMS-Midichopper/Docs/VISION.md).

Build using the [plugin guide](SMS-Midichopper/README.md). Validate code changes
with the existing tests and appropriate host/UI checks described in the
[testing reference](Docs/AI/TESTING.md).

Keep affected documentation accurate in the same change. Human guides should
stay short; implementation contracts and task maps belong in AI references.
Update existing pages before adding new ones, and link every Markdown page from
the [task index](Docs/AI/INDEX.md). With Python 3.9+ installed, run from the
repository root:

```bash
python3 scripts/check_docs.py
git diff --check
```

CI checks links, indexing, size limits, and the Claude alias. Reviewers still
need to compare documentation with actual behavior.

Agents follow [AGENTS.md](AGENTS.md). They may commit locally but must never
push to any remote or publish releases. Human maintainers handle pushes and
the [release procedure](Docs/RELEASING.md).
