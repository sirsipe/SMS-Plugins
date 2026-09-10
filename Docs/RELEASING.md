# Releasing plugins

Human maintainer procedure. Agents may prepare local changes and commits but
must never push branches/tags, publish releases, or trigger publishing workflows.

GitHub Actions builds and tests SMS-Midichopper on every push to `main` and on
every pull request. A separate documentation job checks links, indexing, and
size limits. Push and pull-request builds do not retain artifacts. Manually
dispatched Ubuntu CI builds retain test artifacts for 14 days. Workflow
definitions live in [.github/workflows](../.github/workflows/).

## Testing a branch build

Open the repository's **Actions** page, select **Ubuntu CI**, choose **Run
workflow**, and select the branch to build. The workflow builds and tests the
selected commit without creating a tag or release. When it succeeds, download
the `SMS-Midichopper-<commit>-linux-x86_64` artifact from the workflow run's
summary page. It contains separate LV2 and VST3 `.tar.gz` archives plus
`SHA256SUMS.txt` and expires after 14 days.

The manual control becomes available after the workflow containing it is on the
default branch. The same build can be started from the GitHub CLI with
`gh workflow run ubuntu-ci.yml --ref <branch>`.

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
