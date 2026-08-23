# Change management policy

See [EnvyOS component release policy](https://github.com/MeshEnvy/envyos/blob/main/docs/component-release-policy.md).

User-visible firmware changes → `CHANGELOG.md` under `## [Unreleased]` in the same change set as the code.

Release cut on `envyos/main`: promote Unreleased, `./scripts/changelog.sh check vX.Y.Z`, tag, push. Distro publish bundles `envycore/build/motas/` into the EnvyOS GitHub Release.
