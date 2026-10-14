# Releasing plugins

Human maintainer procedure. Agents may prepare local changes and commits but
must never push branches/tags, publish releases, or trigger publishing workflows.

GitHub Actions builds and tests SMS-Anville Sampler on every push to `main` and on
every pull request. A separate documentation job checks links, indexing, and
size limits. Push and pull-request builds do not retain artifacts. Manually
dispatched Ubuntu and Windows CI builds retain test artifacts for 14 days. Workflow
definitions live in [.github/workflows](../.github/workflows/).

## Testing a branch build

Open the repository's **Actions** page, select **Ubuntu CI**, choose **Run
workflow**, and select the branch to build. The workflow builds and tests the
selected commit without creating a tag or release. When it succeeds, download
the `SMS-AnvilleSampler-<commit>-linux-x86_64` artifact from the workflow run's
summary page. It contains separate LV2 and VST3 `.tar.gz` archives plus
`SHA256SUMS.txt` and expires after 14 days.


For an unsigned Windows x64 development build, select **Windows CI** instead.
Its `SMS-AnvilleSampler-<commit>-windows-x86_64` artifact contains a VST3 ZIP and
`SHA256SUMS.txt`. Extract the whole `SMS-AnvilleSampler.vst3` folder, including
`Contents`, `skin` and `skin-template`. Windows CI runs CTest and bundle checks;
it does not establish native DAW compatibility or publish a Windows release.
Prerequisites and configuration live in [platform builds](AI/PLATFORM-BUILDS.md).

The manual control becomes available after the workflow containing it is on the
default branch. The same build can be started from the GitHub CLI with
`gh workflow run ubuntu-ci.yml --ref <branch>` (or `windows-ci.yml` for Windows).

To publish a release:

1. Update the version in `SMS-AnvilleSampler/CMakeLists.txt` and commit it.
2. Push the reviewed commit to `main`; wait for Ubuntu CI and Documentation
   checks to pass on that commit.
3. From that commit, create and push a matching annotated tag. Replace `X.Y.Z`
   with the version from CMake:

   ```bash
   git tag -a SMS-AnvilleSampler-vX.Y.Z -m "SMS-Anville Sampler vX.Y.Z"
   git push origin SMS-AnvilleSampler-vX.Y.Z
   ```

The release workflow checks that the tag matches the CMake version, performs a
fresh build and test run, and publishes separate Linux x86-64 LV2 and VST3
archives with SHA-256 checksums. It embeds the release tag as the Online Help
reference, so the menu opens the README from that exact release. Ordinary builds
default to the current `main` README. No manual upload is needed.

## Public version compatibility

Versions 0.0.x are alpha/beta and carry no backwards compatibility promise.
The rename to SMS-Anville Sampler is a pre-release identity change. LV2 and
CLAP use new plugin IDs, and skin preferences use the new product directory.
Older sessions and skin selections are not automatically migrated. Parameter
IDs and saved-state codecs are unchanged by the rename.
v1.0.0 establishes the public baseline: future releases must load v1.0.0 and
later supported saved projects and presets with their existing behavior.

Preserve plugin IDs, parameter identities and saved-value meanings. Add
parameters without reordering released entries; retain decoders for released
state versions. A new default must not reinterpret an explicitly saved value.
Keep literal old codec fixtures; do not regenerate them with the current encoder.

At v1.0.0, retain representative Ardour and REAPER projects/presets containing
sample audio, region/ADSR, mixer values and bank mappings. Before later releases,
load those fixtures and representative subsequent versions, compare settings
and playback, and repeat cross-host preset exchange. Product-owner manual
validation counts; record versions and outcomes in the release issue or PR.
Codec tests complement these host checks and cannot verify host plugin discovery.
