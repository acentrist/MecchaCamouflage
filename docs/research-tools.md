# Research Tools

This project pins OSS game-research tools as git submodules under
`third_party/`. They are used only when a game update requires asset, mapping,
or SDK investigation.

The normal runtime build requires the WebView2 controller host, the native bridge and
injector, and the reviewed mesh profile artifacts under `resources/mesh-profiles/`.
The tools below are for update recovery and profile regeneration, not normal
app builds.

Runtime bridge probes for multiplayer paint replication are documented in
`docs/runtime-paint-replication-research.md`. Put repeatable bridge/runtime
investigation scripts under `scripts/research/`; keep generated output under
`artifacts/research/` or a local temp directory.

v1.5.0 uses the mesh-first paint route. The required game-derived mesh profiles
are tracked under `resources/mesh-profiles/`; `make build` copies the required
profile assets into the local build output for package/debug runs. After game
updates, initialize the research-tool
submodules and run `make mesh MAPPINGS=<path-to-usmap>` to regenerate the
reviewed shipping profile.

Initialize the research tools with:

```bash
git submodule update --init --recursive
```

## CUE4Parse

- Upstream: https://github.com/FabianFG/CUE4Parse
- Purpose: inspect cooked Unreal assets and regenerate mesh/profile data after
  a MECCHA CHAMELEON update.
- Pinned path used by `make mesh`: `third_party/CUE4Parse`

## UnrealMappingsDumper

- Upstream: https://github.com/TheNaeem/UnrealMappingsDumper
- Reference commit used during current investigation:
  `4da8c66c23ce66ef86d75962d66b12cf39185092`
- License: MIT
- Purpose: generate or inspect UE mappings when reflected property offsets,
  `FProperty` layouts, or runtime SDK assumptions break after a game update.
- Pinned path: `third_party/UnrealMappingsDumper`

Build from source. Do not rely on upstream prebuilt DLLs for this project; local
crash guards and logging are often needed when offsets are wrong.

## Mesh Profile Regeneration

`make mesh` regenerates the shipping Paintman mesh profile from local game
archives, a local `.usmap` mappings file, and the pinned CUE4Parse submodule:

```bash
make mesh MAPPINGS=/path/to/current-game.usmap
```

A `.usmap` mappings file is the Unreal type and property layout map for
unversioned cooked assets. CUE4Parse needs it to deserialize cooked packages
such as `paintman.uasset`; without a current mapping, profile generation stops
with a mappings error. Generate it locally from the current game build with
UnrealMappingsDumper, then pass its path to `make mesh`. Do not commit generated
`.usmap` files.

Optional overrides:

```bash
make mesh \
  PAKS="/path/to/MECCHA CHAMELEON/Chameleon/Content/Paks" \
  MAPPINGS="/path/to/current-game.usmap" \
  CUE4PARSE="third_party/CUE4Parse" \
  GAME_VERSION=GAME_UE5_6
```

The command writes `resources/mesh-profiles/paintman.mesh-profile-v2.json` after
validating the expected Paintman LOD0 shape. It fails closed if CUE4Parse,
game archives, mappings, or the expected mesh shape are unavailable.

## Update Workflow

When a game update breaks painting, use this order:

1. Run the app once and keep `%LOCALAPPDATA%\MecchaCamouflage\runtime\` logs.
2. Check runtime reflection metadata first: function availability, reflected
   offsets, `RuntimePaintable` state, and mesh profile identity.
3. Use UnrealMappingsDumper only when runtime reflection cannot resolve a
   trustworthy layout or the engine-side mapping changed.
4. Generate a current `.usmap` locally if the previous mapping no longer works.
5. Run `make mesh MAPPINGS=<path-to-current.usmap>` to regenerate the profile.
6. Review and commit regenerated shipping profiles in `resources/mesh-profiles/`.
   `scripts/build.ps1` copies profiles into `.build/bin/mesh-profiles/` for
   package/debug runs.

Commit only reviewed project artifacts:

- source changes under `src/`, `scripts/`, or docs
- small regenerated profile JSON files under `resources/mesh-profiles/` that are
  intended to ship
- submodule pointer updates when a pinned research tool version changes
- pinned patch files or instructions if a research tool needs local changes

Do not commit:

- game content (`.pak`, `.ucas`, `.utoc`, `.sig`)
- generated mappings (`*.usmap`)
- research logs, dumps, injected DLLs, or build output
