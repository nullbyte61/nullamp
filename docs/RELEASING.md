# Releasing

Releases are cut by pushing a version tag. A GitHub Actions workflow
([`.github/workflows/release.yml`](../.github/workflows/release.yml)) does the rest: it builds,
runs the test gates, packages the plugin bundles, and publishes a GitHub Release with the tarball
attached.

## Steps

1. Pick the new version, `X.Y.Z` (semantic versioning).

2. Bump the version in two places so they match the tag:
   - `CMakeLists.txt`: `project(Nullamp VERSION X.Y.Z ...)`
   - `packaging/PKGBUILD`: `pkgver=X.Y.Z`

   The release workflow fails if the tag and the CMakeLists version don't match, so this is enforced.

3. Update `CHANGELOG.md`: rename the `Unreleased` section to `## [X.Y.Z] - YYYY-MM-DD` and start a
   fresh `Unreleased` section above it. The text under that heading becomes the release notes.

4. Commit the version bump and changelog.

5. Tag and push:

   ```sh
   git tag -a vX.Y.Z -m "Nullamp X.Y.Z"
   git push origin main
   git push origin vX.Y.Z
   ```

That's it. The workflow runs on the tag, and the release shows up under the repo's Releases page with:

- `nullamp-X.Y.Z-linux-x86_64.tar.gz` (the LV2/VST3/CLAP bundles, the standalone, install scripts, docs)
- a matching `.sha256` checksum
- release notes pulled from the changelog

## The tarball

Users download and run:

```sh
tar xzf nullamp-X.Y.Z-linux-x86_64.tar.gz
cd nullamp-X.Y.Z-linux-x86_64
./install.sh        # copies the bundles into ~/.lv2, ~/.vst3, ~/.clap
```

`./uninstall.sh` removes them again.

## Notes

- Releases build for x86-64. ARM is untested (see the README); add a build matrix here if that
  changes.
- To re-run a release (for example after fixing the workflow), delete the tag and the release on
  GitHub, then push the tag again.
