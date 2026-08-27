# RFC: Lode Package Resolution, Aliases, and Lockfiles

## Status

Accepted and implemented design for `package.luau`, dependency resolution, and
`lode.lock`. Changes to the versioned lockfile shape require an explicit
format-version update.

## Summary

The package manager resolves package requirements and writes the result into a
lockfile and Luau configuration. The manifest package name identifies the
package, but it does not automatically become a `require` alias. A resolved
package is identified by its exact version and source as well as its name.
`lode add` adds a Git dependency to the manifest and then runs the same
installation path as `lode install`.

The existing `.config.luau` format remains in use because it is consumed by the
Luau compiler and LSP. The package manager may update managed aliases in that
file, but must not replace the Luau configuration format with a new one.

## Dependency declarations

The shorthand form is reserved for an official Lode standard-library module.

```luau
return {
  dependencies = {
    task = "1.0.0"
  }
}
```

For an official standard module, the package manager first checks the stdlib
bundle associated with the installed Lode runtime. If the bundled version does
not satisfy the requirement, unlocked installation downloads the aggregate
GitHub Release bundle from the official LodeRuntime repository. The
release tag is read from the installed catalog's `stdlib/VERSION` marker, and
the current runtime target selects:

```text
lode-stdlib-<platform>-<architecture>-<nightly-release>.zip
```

This fallback currently requires an exact SemVer requirement. A `^` or `~`
requirement is installable when the bundled module satisfies it; otherwise
the installer reports that it cannot select an official artifact for a range.
This is a GitHub Release convention, not a Lode-owned registry.

An explicit source may be used for Git or local development:

```luau
return {
  dependencies = {
    example = {
      git = "<git-repository>",
      version = "^1.0.0"
    }
  }
}
```

For local development, the same alias is overridden by changing its source;
it is not declared a second time:

```luau
return {
  dependencies = {
    example = {
      path = "../example-package",
      version = "1.0.0"
    }
  }
}
```

The dependency key is the project-local alias and must be unique within its
dependency table. There is no separate `alias` field in the initial contract,
so two declarations cannot silently produce the same alias. The package
manager must validate that the resolved package manifest has the expected
`name` and that its version satisfies the requested range.

Published versions use SemVer without a leading `v` in the manifest. Git
release tags use `vMAJOR.MINOR.PATCH`, for example `v1.2.0`. A Git branch may be
used for local development outside the package-manager flow, but package
resolution requires a SemVer tag and records its commit rather than relying on
a moving branch or the word `latest`.

## Adding a Git dependency

`lode install` installs the dependency graph already declared by the project.
It does not interpret a repository argument as a package request. The command
that adds a repository dependency is:

```text
lode add [--dev] owner/repository[@version] [package-root]
```

The repository shorthand is canonicalized to `github:owner/repository` in
`package.luau`. A local Git repository path and a full supported Git URL may also
be used when developing a package.

When `@version` is omitted, Lode lists the repository's `v<SemVer>` tags and
selects the highest stable version. With `@1.2.0`, it selects exactly
`v1.2.0`; the existing exact, `^`, and `~` requirement forms are accepted.
The selected version is written to the manifest when no requirement was
provided. The command then runs unlocked installation, which writes the
lockfile and materializes the alias.

Tag selection never uses a moving `latest` tag or the repository's default
branch. Every selected tag is resolved to a commit and recorded in
`lode.lock`.

## Resolved package identity

Dependency aliases identify edges in the graph, not package records. The
stable logical identity of a resolved package is the tuple:

```text
package name
exact package version
source kind: stdlib, path, or git
source reference when the source kind has one
resolved Git commit when the source kind is git
```

The alias is intentionally absent from this identity. Two packages may use
the same alias for different resolved records in their own package contexts.
Conversely, two aliases that resolve to the same tuple may share one physical
installation.

The standard library has no external repository identity in this tuple. Its
source kind is `stdlib`; the selected standard-module artifact is identified
separately by its Lode release target and checksum as defined by RFC 09.

A selected compiled implementation artifact extends the logical identity with its target:

```text
logical package identity
platform
architecture
runtime configuration when applicable
Lode ABI identifier
artifact release, asset name, and SHA-256 when a published artifact is selected
```

This prevents an artifact for one target or ABI from replacing another artifact
belonging to the same package version.

## Alias generation

For a dependency key named `<alias>`, the package manager generates a relative
alias in `.config.luau`:

```lua
aliases = {
    ["<alias>"] = "lode_modules/<alias>",
}
```

The generated paths must be project-relative. Absolute paths into a global
cache would make the committed project configuration machine-specific.

For a package's own local test context, the package may use its own directory
as an explicit alias:

```luau
aliases = {
    native_example = ".",
}
```

The alias value is resolved relative to the `.config.luau` file. This is a
local development/test convenience and does not replace the consumer-facing
`lode_modules/<package>` alias generated by the package manager.

## Global cache and project installation

Downloads may be cached globally and reused by multiple projects. The project
must still receive a local, portable view under `lode_modules/`, using a copy,
junction, or supported link mechanism.

The intended layout is:

```text
project/
├── package.luau
├── lode.lock
├── .config.luau
└── lode_modules/
    └── signal/
```

The decided global cache root is `%USERPROFILE%\\.lode\\global\\lode_modules`.
Downloaded archives and staging data remain outside the project and must not
appear in `.config.luau` or determine the project's import paths.

The complete cache layout is:

```text
%USERPROFILE%\\.lode\\
├── cache\\
│   ├── archives\\sha256\\<lowercase-sha256>.zip
│   └── staging\\<operation-id>\\
└── global\\lode_modules\\
    └── <name>\\<version>\\<identity-sha256>\\
```

Archive files are addressed only by their verified lowercase SHA-256. Staging
directories are operation-scoped and are never used as installed packages.
The global module directory is addressed by a lowercase SHA-256 of the
canonical resolved identity tuple defined above, encoded as deterministic JSON;
the dependency alias is never included. When a compiled implementation artifact is selected,
its platform, architecture, configuration when applicable, ABI, and artifact
checksum are included in the identity material so incompatible artifacts
cannot share an installation directory.

The extracted package root is stored directly inside the identity directory.
The project-local `lode_modules/<alias>` view may be a supported link or a
copy of that immutable global installation, but `.config.luau` never points
into `%USERPROFILE%`.

The global installation is deduplicated by the resolved package identity, not
by the dependency alias. A package with the same logical identity is extracted
once and may be linked into multiple project or package-local contexts. Two
incompatible versions, or the same version from different sources, remain
separate installations. Package-local configuration determines which physical
installation a dependency alias reaches.

The runtime-owned stdlib is separate from project dependencies. A normal Lode
distribution contains a complete stdlib bundle and its root `.config.luau`.
The package manager does not put that bundle in `lode.lock`. When a project or
transitive dependency requests an incompatible stdlib version, only the
required module artifact is installed and the affected package receives a
package-local `.config.luau` context. Each unresolved stdlib edge is handled
independently, so a module and its stdlib dependencies are downloaded once
per resolved package identity rather than as a second copy of the complete
stdlib bundle.

The installed catalog's `stdlib/VERSION` is the exact nightly release used for
unlocked fallback downloads. The installer verifies the release checksum
before extraction and records the selected release, asset, ABI, and SHA-256
in the stdlib package record. The current release must contain the requested
exact-version module asset; Lode does not query a registry or synthesize a
version from a moving branch.

When Lode validates a package outside the runtime checkout, the validator uses
the stdlib catalog installed alongside the `lode` executable. The CLI passes
that directory explicitly to validation and CI generation; it is not inferred
from the package's source tree. When running inside this repository, the local
`modules/` catalog remains the fallback. The catalog source is therefore
deterministic for both installed CI environments and repository fixtures.

## Local materialization and managed aliases

The current local materializer copies an already verified global installation
into project/lode_modules/<alias> through a temporary sibling directory and
renames it into place only after the copy succeeds. It creates
lode_modules/ when needed, refuses to replace an existing destination, rejects
symbolic links in the source installation, and removes a failed temporary copy.
Link-based views remain a future optimization; they are not required for the
initial implementation.

After the local package directory is materialized, the package manager updates
the existing root .config.luau luau.aliases table with:

    ["<alias>"] = "lode_modules/<alias>",

Only that aliases table is changed. Existing aliases with the same target are
left untouched; an existing alias with a different target is an error. The
configuration file is replaced atomically, and a malformed or missing
luau.aliases table is an error rather than an opportunity to overwrite the
user's configuration. Alias and path values must remain single, relative,
non-traversing components/paths.

## Lockfile

`lode.lock` is separate from `package.luau`. It records the resolved graph rather
than the requested ranges. A lock entry must include at least:

```text
package identity
resolved version
source kind
Git source or official stdlib source
exact Git commit when applicable
package/artifact checksum when applicable
resolved aliases
transitive dependencies
```

Version 1 does not store a separate package-manifest content hash. The
resolved graph, including the manifest-derived fields and artifact metadata,
is serialized deterministically and compared with the lockfile during locked
operations. Package and artifact checksums remain recorded when they are
available.

Each dependency edge in the lockfile records the alias used by the dependent
package and points to one resolved package identity. The edge must not point
only to a name or alias, because that would be ambiguous when multiple
versions or sources coexist.

The lockfile format is versioned JSON. Version 1 uses the following concrete
shape:

```json
{
  "lockfileVersion": 1,
  "root": 0,
  "packages": [
    {
      "name": "signal",
      "version": "1.0.0",
      "source": "root",
      "dependencies": [
        {
          "alias": "task",
          "requirement": "1.0.0",
          "target": 1
        }
      ]
    },
    {
      "name": "task",
      "version": "1.0.0",
      "source": "stdlib",
      "dependencies": []
    }
  ]
}
```

The `root` value is a zero-based index into `packages`. Every dependency edge
contains the local alias, the original requirement for lock consistency, and a
`target` index pointing to the exact resolved package record. The target record
contains the complete logical identity, so an edge never resolves by name or
alias alone.

The root record may also contain a `devDependencies` array with the same edge
shape. The lockfile resolves and records the root development graph, but
development packages are installed only when `--dev` is supplied. A dependency
package's own `devDependencies` are not transitive runtime dependencies and
are not included in the consumer's graph.

The package array is deterministic: the root record is first, and all other
records are ordered by source kind, name, exact version, source reference, and
Git commit. The index is only a reference within this lockfile; the package
identity is the complete record data.

For `path` and `git` records, `reference` is required. A path reference is
normalized and remains relative to the project/package context; it is never an
absolute cache path. A Git record also requires its resolved `commit`. A
`stdlib` record has no external repository reference.

Packages with compiled implementations may include an `artifacts` array. Each artifact record
contains `platform`, `architecture`, optional `configuration`, `abi`, the exact
GitHub Release `release` tag, the selected `asset` name, and its lowercase
SHA-256 `sha256`. Artifact selection is part of installation, not runtime
module resolution.

The package manager writes `lode.lock` atomically as part of:

```text
lode install
```

`lode install` resolves `package.luau`, reuses compatible locked records when
possible, updates the lockfile when the requested graph changes, installs the
resolved runtime artifacts, and materializes project/package-local aliases. It
also resolves the root `devDependencies` into the lockfile but does not install
those packages unless `--dev` is supplied. It does not compile published
dependencies locally.

```text
lode install --locked
```

`--locked` requires an existing, current lockfile and never changes it or
performs a new version/revision resolution. It may still download the exact
locked artifact when it is absent from the global cache. A missing or stale
lockfile is an error. A project with no dependencies may complete without a
lockfile.

The runtime does not read `lode.lock`. `lode install --locked` and CI use it to
reproduce the same package graph.

## Current installer boundary

The current installer commands are:

    lode install [--dev] [package-root]
    lode install --locked [--dev] [package-root]

The unlocked resolver validates the current source graph, supports stdlib and
local path packages already present on disk, and resolves every Git dependency
by selecting the highest matching `v<SemVer>` tag and checking out its commit.
Luau-only Git packages are copied from that checkout. A package with a compiled
implementation must use the GitHub Release artifact contract described by RFC 09; the selected
`v<version>` release is downloaded, checked, and copied into the global cache.
For stdlib dependencies, a compatible module is reused from the installed
catalog. An incompatible exact requirement downloads the one matching
`lode-stdlib-<platform>-<architecture>-<release>.zip` asset from the release named by
`stdlib/VERSION`; its transitive stdlib edges are resolved by the same rule.
Resolved packages are materialized under the project's `lode_modules`
directory and reflected in `.config.luau` and the deterministic `lode.lock`.

`--locked` performs the same materialization only after validating the existing
lockfile and never changes it. For Git packages it checks out the exact commit
and verifies the exact locked compiled artifact, when present. Without --dev, root
devDependencies and the runtime dependencies reachable only through those
development roots are not materialized. With --dev, those root development
packages and their runtime dependency subgraphs are materialized; a dependency
package's own devDependencies remain excluded.

When a locked stdlib edge contains an artifact record, installation downloads
that exact release asset when it is absent from the cache and rejects any
release, asset, ABI, or checksum mismatch. If the installed bundled module
already satisfies the locked requirement, no separate artifact is required.

CI artifact validation uses the same locked graph after the package's compiled
artifacts are built. `lode ci validate --artifact --locked` validates the
package's complete declared artifact matrix while accepting an exact locked
stdlib or Git artifact that is not part of the Lode distribution's bundled catalog. It may
populate the global cache, but it does not materialize dependencies or modify
the project configuration.

The runtime contains the SHA-256 verifier, safe-ZIP staging, and GitHub Release
download path for the current published target. A Git package with an
`implementation` declaration must use the generated release contract
(`v<version>`, `lode-<name>-<version>-<platform>-<architecture>.zip`, and its
`.sha256` asset). The downloaded
package is validated as an artifact before entering the global cache. GitHub is
the source; there is no Lode registry or `gh` runtime dependency.

`--locked` clones the exact Git commit recorded by the lockfile and downloads
the same release asset only when its release, asset name, ABI, and checksum
match the locked artifact record. A Git package that is not a supported
GitHub Release package, or a package targeting another runtime platform, is
rejected explicitly. The Git declaration has no branch/ref field in v1;
unlocked resolution follows SemVer tags and pins the selected commit in
`lode.lock`.

## Version conflicts

The package manager must not silently replace one incompatible version with
another. Compatible requirements may share one resolved package identity and
physical installation. Incompatible requirements must either be isolated in
package-local dependency contexts or reported as a conflict until contextual
resolution is implemented.

Package-local `.config.luau` files may provide the aliases for a dependency's
own context. This preserves the existing upward Luau configuration lookup and
allows two packages to require different versions without encoding versions in
the `require` string.

## Current compatibility boundary

The package manager reads statically representable `dependencies` and
`devDependencies` tables from `package.luau`; it never executes the manifest.
The runtime-owned stdlib fallback is independent of dependency resolution.
Until the package manager generates project and package-local `.config.luau`
files for every resolved context, existing relative requires and manually
configured aliases remain supported behavior.

> **Limitation (2026-08-21):** `lode install` currently flattens the graph
> into a single `lode_modules/` + root `.config.luau` (`src/PackageInstaller.cpp:973`).
> Per-package `.config.luau` isolation for transitive version conflicts
> (`06:443:447`) is not yet implemented — incompatible transitive versions
> are reported as an alias collision (`PackageInstaller.cpp:1016`) instead of
> being isolated in package-local contexts. This is tracked as future work and
> does not affect single-version graphs.
