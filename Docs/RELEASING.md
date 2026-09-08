# Releasing plugins

Human maintainer procedure. Agents may prepare local changes and commits but
must never push branches/tags, publish releases, or trigger publishing workflows.

GitHub Actions builds and tests SMS-Midichopper on every push to `main` and on
every pull request. A separate documentation job checks links, indexing, and
size limits. Normal CI runs do not retain build artifacts. Workflow definitions
live in [.github/workflows](../.github/workflows/).

To publish a release:

1. Update the version in `SMS-Midichopper/CMakeLists.txt` and commit it.
2. Push the reviewed commit to `main`; wait for Ubuntu CI and Documentation
   checks to pass on that commit.
3. From that commit, create and push a matching annotated tag. Replace `X.Y.Z`
   with the version from CMake:

   ```bash
   git tag -a SMS-Midichopper-vX.Y.Z -m "SMS-Midichopper vX.Y.Z"
   git push origin SMS-Midichopper-vX.Y.Z
   ```

The release workflow checks that the tag matches the CMake version, performs a
fresh build and test run, and publishes separate Linux x86-64 LV2 and VST3
archives with SHA-256 checksums. No manual upload is needed.
