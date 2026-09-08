# Releasing plugins

GitHub Actions builds and tests SMS-Midichopper on every push to `main` and on
every pull request. Normal CI runs do not retain build artifacts.

To publish a release:

1. Update the version in `SMS-Midichopper/CMakeLists.txt` and commit it.
2. Wait for the Ubuntu CI workflow to pass on `main`.
3. Create and push a matching annotated tag:

   ```bash
   git tag -a SMS-Midichopper-v0.0.3 -m "SMS-Midichopper v0.0.3"
   git push origin SMS-Midichopper-v0.0.3
   ```

The release workflow checks that the tag matches the CMake version, performs a
fresh build and test run, and publishes separate Linux x86-64 LV2 and VST3
archives with SHA-256 checksums. No manual upload is needed.
