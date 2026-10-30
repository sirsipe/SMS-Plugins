# Releasing plugins

Human maintainer procedure. Agents may prepare local changes and commits but
must never push branches/tags, publish releases, or trigger publishing workflows.

GitHub Actions builds and tests SMS-Anville Sampler on every push to `main` and on
every pull request. A separate documentation job checks links, indexing, and
size limits. Push and pull-request builds do not retain artifacts. Workflows
live in [.github/workflows](../.github/workflows/).

Releases include Linux x64 VST3/LV2 and Windows x64 VST3.
CI and packaging exclude CLAP following the sample editor failure in
[issue #80](https://github.com/sirsipe/SMS-Plugins/issues/80).
Opt-in local CLAP builds remain available for development.

## Testing a branch build

Open the repository's **Actions** page, select **Ubuntu CI**, choose **Run
workflow**, and select the branch to build. The workflow builds and tests the
selected commit without creating a tag or release. When it succeeds, download
the `SMS-AnvilleSampler-<commit>-linux-x86_64` artifact from the workflow run's
summary page. It contains VST3 and LV2 ZIPs, a matching
`*-source.tar.gz` archive, and `SHA256SUMS.txt`; it expires after 14 days.


For an unsigned Windows x64 development build, select **Windows CI** instead.
Its `SMS-AnvilleSampler-<commit>-windows-x86_64` artifact contains a VST3 ZIP,
matching source archive, and `SHA256SUMS.txt`. Extract the whole
`SMS-AnvilleSampler.vst3` folder, including `Contents`, `licenses`, `skins` and
`skin-template`. Windows CI runs CTest and bundle checks;
it does not establish native DAW compatibility. The release workflow reuses this
job to publish Windows assets after both platform builds pass.
Prerequisites and configuration live in [platform builds](AI/PLATFORM-BUILDS.md).

The manual control becomes available after the workflow containing it is on the
default branch. The same build can be started from the GitHub CLI with
`gh workflow run ubuntu-ci.yml --ref <branch>` (or `windows-ci.yml` for Windows).

To publish a release:

1. Update numeric `VERSION` and `ANVILLESAMPLER_PRERELEASE` in
   `SMS-AnvilleSampler/CMakeLists.txt` and commit them. Use an empty prerelease
   string for a stable release. Hosts receive the numeric version; the UI, tags
   and packages include the suffix, for example `0.1.0-beta.1`.
2. Push the reviewed commit to `main`; wait for Ubuntu CI, Windows CI and
   Documentation checks to pass on that commit.
3. From that commit, create and push the exact full version tag:

   ```bash
   release_version="$(python3 scripts/release_version.py)"
   git tag -a "SMS-AnvilleSampler-v${release_version}" -m "SMS-Anville Sampler v${release_version}"
   git push origin "SMS-AnvilleSampler-v${release_version}"
   ```

The release workflow checks the tag against CMake, then reuses Ubuntu and Windows
CI for fresh builds and tests. Publishing waits for both jobs and produces:

```text
SMS-AnvilleSampler-vX.Y.Z-Linux-x64-vst3.zip
SMS-AnvilleSampler-vX.Y.Z-Linux-x64-lv2.zip
SMS-AnvilleSampler-vX.Y.Z-Windows-x64-vst3.zip
```

Suffixes such as `-beta.1` are preserved in all names. Suffixed versions publish
as GitHub prereleases without replacing Latest; stable versions become Latest.
Windows builds remain unsigned. Older releases retain their original asset names.

Each platform also supplies its matching `*-source.tar.gz`; one `SHA256SUMS.txt`
covers all five archives. Windows source includes its additional Khronos headers.
Release builds embed the tag as Online Help's reference; ordinary builds use
`main`. No manual upload is needed. Installation instructions are in the
[plugin guide](../SMS-AnvilleSampler/README.md#how-to-install).

[ZIP packaging](../scripts/package_plugins.py) puts exactly one folder at the
archive root. VST3/LV2 preserve their complete bundles. Local preview:

```bash
python3 scripts/package_plugins.py --build-dir build \
  --output-directory build/release-packaging --platform Linux --version vX.Y.Z
```

## Licenses and corresponding source

Every build packages the root [license](../LICENSE), the
[DAW host-linking exception](../HOST-LINKING-EXCEPTION.txt), full third-party
notices, and source instructions. LV2 and VST3 keep them in `licenses/` inside
the bundle. Opt-in local CLAP builds use `SMS-AnvilleSampler.licenses` beside the binary, or
`licenses/` inside the macOS CLAP bundle. CMake installation preserves these
files, skins, and templates for each enabled format. Keep them when repackaging.

[Notice generation](../scripts/package_licenses.py) reads the pinned upstream
copyright and permission notices, including the embedded font's separate terms.
Extraction accepts LF and CRLF checkouts and preserves upstream text. Tests cover
both styles with and without Windows' optional Khronos headers.
Review its inventory whenever dependencies or bundled assets change. The host
exception permits independent proprietary hosts to load the plugin through
standard VST3/LV2/CLAP interfaces; it does not waive copyleft or network-source
requirements for the plugin, its modifications, or ports.

[Source packaging](../scripts/package_source.py) exports the committed Git tree,
recursively includes the exact DPF and Pugl revisions, and records identities in
`SOURCE-MANIFEST.json`. Windows archives also include the pinned downloaded
Khronos headers and their hashes. It rejects modified tracked files, mismatched
submodules, and uninitialized submodules. Commit intended changes before running:

```bash
python3 scripts/package_source.py \
  --output dist/SMS-AnvilleSampler-source.tar.gz --prefix SMS-Plugins
ctest --test-dir build --output-on-failure -R '^(package-licenses|packaged-licenses|source-archive|plugin-zips|release-version)$'
```

Extract the source archive and follow `SMS-AnvilleSampler/README.md` to build,
skipping `git submodule update`; no Git checkout or download is needed. GitHub's automatic source ZIP
and tarball omit submodule contents and are not substitutes for this archive.

Release notes identify the matching source asset next to the binary downloads.
Keep it available at no extra charge for as long as the binaries are offered.
Manual CI artifacts include source in the same download. A downstream packager
must provide corresponding source for its actual binaries, including changes
and required build scripts, and preserve notices for any extra bundled libraries.
Build and packaging tests verify file coverage; they do not establish ownership
of contributed material or replace review of a new dependency's license.

## Public version compatibility

Versions before 1.0.0 carry no backwards compatibility promise.
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
