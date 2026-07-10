# Paint Accuracy / Quality v2

This document describes the accuracy-focused paint pipeline enhancements added on
top of the mesh-first packed replication route. Every change is **guarded**: if a
paint payload does not carry the new tuning keys, the native bridge reproduces
the exact legacy behavior. This keeps older configs and replays bit-compatible.

> This is a modified/unofficial build feature (see `BRANDING.md`). It does not
> imply endorsement by the upstream project.

## Goal

Make the projected camouflage/auto-paint result match the captured reference more
faithfully — sharper corak/pattern edges, smoother gradients, fewer specular
artifacts, and more even coverage of back/side/front regions.

## Quality presets

A single user-facing `PaintQualityPreset` (`Fast`, `Balanced`, `High`, `Ultra`)
drives five underlying accuracy parameters through
`PaintSettings.ApplyQualityPreset(preset)`. The gradient is monotonic
(verified by unit test):

| Preset   | Bilinear | Bicubic | Dither | MinRoughness | FalloffHardness% | CoverageSupersample |
|----------|----------|---------|--------|--------------|------------------|---------------------|
| Fast     | off      | off     | 0.00   | 0.00         | 100              | 1                   |
| Balanced | on       | off     | 0.35   | 0.05         | 90               | 1                   |
| High     | on       | off     | 0.50   | 0.08         | 85               | 2                   |
| Ultra    | on       | on      | 0.60   | 0.10         | 78               | 2                   |

**Bicubic** is the highest accuracy tier and is enabled only at **Ultra**. It
adds no extra strokes or replication load — it only changes how each color
sample is reconstructed from the reference capture (see algorithm 1b). This is
intentional: raising `CoverageSupersample` further would inflate the packed RPC
batch, so accuracy is pushed via a better reconstruction filter instead.

Fresh `PaintSettings` default to **High**. `CoverageSupersample` is capped at 2
for replication-load safety (per `runtime-paint-replication-research.md`).

Ultra additionally enables a **linear-light edge-adaptive unsharp filter** at
strength `0.35`. Local four-neighbour luminance contrast gates the filter, so
flat gradients are not sharpened while real pattern boundaries recover
micro-contrast after bicubic reconstruction. High uses a conservative `0.18`.
Preset pacing is also monotonic: Fast/Balanced/High/Ultra request
`75/50/20/1 ms`; runtime queue pressure remains the safety back-pressure.

## Algorithms (native, `bridge.cpp`)

1. **Bilinear color capture** (`mesh_first_capture_project_color`): when enabled,
   the projected sample position is filtered from its four neighboring capture
   texels with fractional weights (pixel-center `-0.5` convention), instead of
   snapping to the nearest texel. Preserves fine pattern/line detail and reduces
   aliasing. Falls back to nearest sampling for 1-pixel captures.
   - **Gamma-correct (linear-space) blending**: the four sRGB texels are decoded
     to linear light (`sdk_srgb_to_linear_unit`), blended, then re-encoded to
     sRGB (`sdk_linear_to_srgb_unit`). Naive sRGB-space interpolation darkens
     midtones on pattern edges/gradients; linear-space blending keeps the color
     of corak/garis accurate. Downstream color handling is unchanged.
   - **(1b) Bicubic Catmull-Rom reconstruction** (Ultra only,
     `bicubic_color_sampling`): instead of the 2x2 bilinear neighborhood, the
     sample is reconstructed from the surrounding **4x4** texels using separable
     Catmull-Rom cubic weights
     (`w0=-0.5t^3+t^2-0.5t`, `w1=1.5t^3-2.5t^2+1`, `w2=-1.5t^3+2t^2+0.5t`,
     `w3=0.5t^3-0.5t^2`). This is a higher-order interpolation that preserves
     sharp corak/pattern edges and fine garis far more faithfully than bilinear,
     which tends to soften them. It runs in the same gamma-correct linear space
     (decode 16 taps -> convolve -> re-encode) and clamps each channel with
     `max(0, .)` before sRGB encoding to absorb Catmull-Rom's slight negative
     overshoot. Requires captures >= 4x4; otherwise it falls back to bilinear,
     then nearest.
2. **Edge-adaptive sharpening**: after reconstruction, a four-neighbour
   linear-light unsharp mask is multiplied by a local luminance edge gate. This
   avoids sharpening noise or flat gradients and clamps every channel to gamut.
3. **Coverage supersampling**: the planner coverage step is divided by the
   supersample factor (`tuning_effective_coverage_step`), producing denser,
   more even sample coverage across regions.
4. **Tunable brush falloff hardness**: per-sample paint strokes use
   `FalloffHardnessPct` for the brush hardness; solid *fill* regions always keep
   hardness `1.0` so flat fills stay solid.
5. **Ordered (Bayer 4x4) dithering**: a deterministic, texel-coordinate-keyed
   sub-quantization offset applied to paint-branch colors to break up 8-bit
   gradient banding on the packed route.
6. **Roughness floor** (`MinRoughness`): clamps the lower bound of stroke
   roughness to avoid mirror-like specular artifacts from near-zero-roughness
   sources.

All five are reported back in the paint metadata (`quality_*` keys) for
diagnostics.

## Data flow

```
UI (quality-preset select)
  -> HostSession paint.qualityPreset  (ApplyQualityPreset)
  -> SettingsStore (persist snake_case: quality_preset, bilinear_color_sampling,
     dither_strength, min_roughness, falloff_hardness_pct, coverage_supersample)
  -> BridgePayloadBuilder tuning { quality_preset, enable_bilinear,
     bicubic_color_sampling, dither_strength, min_roughness,
     falloff_hardness_pct, coverage_supersample }
  -> native bridge.cpp (guarded parse + algorithms)
```

Legacy key `enable_bilinear` is accepted on load as a fallback for
`bilinear_color_sampling`.

## Tests

Deterministic unit tests in `MecchaCamouflage.Tests` cover: preset monotonicity,
payload key emission, clamp bounds, settings round-trip (incl. legacy fallback),
HostSession apply + section reset, and UI snapshot exposure. The native
algorithms are statically written to spec; validate them in-game via a paint
smoke test.

## Detection visualization (manual detail)

The "Detection" feature makes the planner's understanding of the mesh visible so
the user can confirm that detected corak/elemen/garis map exactly onto the
model. It is an opt-in debug artifact system, independent from the quality
presets, controlled by two settings:

- **`EnableDetectionArtifacts`** (bool, default off) — when on, every paint plan
  writes debug BMPs into the runtime log directory: a UV color map, a UV region
  map, and a screen-space projection of the plan samples. Previously this was
  only reachable through the `MECCHA_RESEARCH_ARTIFACTS=1` environment variable;
  it is now a first-class, UI-settable toggle (the env var still forces it on).
- **`DetectionDetail`** (int 1..5, default 2) — manual detail level. It scales
  the UV map resolution (`texture_size * detail`, capped at 4096) and tightens
  the per-sample mark radius so higher levels resolve fine corak/garis more
  exactly:
  - 1 = coverage (fat 5x5 marks, base resolution)
  - 2 = standard (3x3 marks, 2x resolution)
  - 3 = fine (3x3 marks, 3x resolution)
  - 4 = exact (single-pixel marks, 4x resolution)
  - 5 = forensic (sub-pixel 0.75-radius splats plus Sobel edge diagnostics)

The detector now also emits `mesh-first-uv-edges-*.bmp`, generated by a real
3x3 Sobel gradient over the normalized UV color map. Metadata reports the edge
detector, output status, and count of strong edge pixels, making fine-line and
pattern-boundary regressions measurable instead of purely visual.

**Sub-pixel anti-aliased UV color map.** The UV color map is no longer drawn by
snapping each sample to one hard pixel. Every sample is *splatted* with a
separable tent footprint (`splat_radius = mark_radius + 1`) into float
accumulation buffers (`accum_rgb`, `accum_w`), then normalized by coverage
weight before quantizing to 8-bit. This lands each detected corak/garis at its
exact fractional UV position with smooth edges instead of jagged single-pixel
dots, so the map is visibly sharper and more faithful — especially at detail 3-4
where marks are already tiny. The result is reported in metadata as
`mesh_debug_uv_antialiased` and `mesh_debug_uv_splat_radius`. The region map
(front/side/back classification) keeps the original hard marks, since it encodes
discrete categories rather than continuous color.

Both detail values are echoed back in paint metadata as
`detection_artifacts_enabled` and `detection_detail`. When the toggle is off and the env var is unset, no
artifacts are written and the plan path is byte-for-byte identical to before, so
the feature never regresses normal painting.

Data flow mirrors the quality chain: UI (`detection-artifacts` checkbox +
`detection-detail` select) -> HostSession (`paint.enableDetectionArtifacts`,
`paint.detectionDetail`; reset section `paint.detection`) -> SettingsStore
(`enable_detection_artifacts`, `detection_detail`) -> BridgePayloadBuilder tuning
(`detection_artifacts`, `detection_detail`) -> native bridge.cpp
(`mesh_first_write_uv_debug_artifacts` gated by `research_artifacts ||
tuning_detection_artifacts`, detail-scaled resolution + mark radius).

## FAST preset: footprint-integrated color capture (V5)

The FAST preset intentionally keeps its *spatial* cost low (large 8-texel
strokes, coarse coverage, high packed pacing) and disables sub-pixel bilinear /
bicubic sampling. Previously its color path fell back to point (nearest-texel)
sampling. A single point sample under-represents a stroke that physically covers
several source texels, which aliases high-frequency corak/pattern into moire and
wrong average colors -- the main reason FAST looked inaccurate.

V5 replaces point sampling with a **gamma-correct area (box) integration** over a
stroke-scaled footprint. For each FAST sample the bridge decodes the sRGB
neighborhood to linear light, averages it, and re-encodes the mean. This is the
correct representative color for the stroke footprint (the same principle as
texture minification/mipmapping) and removes aliasing without any per-sample
cost that scales with coverage.

- Footprint radius (`mc::quality::fast_footprint_radius`) grows with stroke size
  and is clamped to `[1,3]` capture texels, so cost stays bounded (<= 7x7 taps)
  and monotonic. FAST's 8-texel stroke resolves to radius 2 (5x5).
- The integral (`mc::quality::area_mean_linear`) is energy-preserving: a flat
  footprint reproduces the source exactly, and the result never overshoots the
  min/max of its taps (unlike higher-order kernels).
- Higher presets are untouched: bilinear/bicubic keep `fast_area_radius = 0`, so
  their color paths are byte-for-byte identical and the monotonic quality
  gradient (FAST < Balanced < High < Ultra) still holds -- FAST now integrates
  coarse footprints while the higher tiers add sub-pixel precision.
- Echoed in paint metadata as `quality_fast_area_radius` for verification.

Data flow: SettingsStore/preset (`stroke_size_texels`, bilinear/bicubic off) ->
BridgePayloadBuilder tuning -> native bridge.cpp
(`tuning_fast_area_radius = fast_footprint_radius(stroke)` when both filters are
off) -> `mesh_first_capture_project_color` area-integration branch. Covered by
`src/native/tests/quality_algorithms_test.cpp` (radius bounds/monotonicity and
area-mean correctness incl. a checkerboard anti-aliasing case).

## Packed batch limit is Ultra-only (V5 build fix)

The packed server route self-resolves its batch limit from replication pressure
(`HostSession.DefaultPackedBatchLimit`), so the legacy `server_batch_limit`
tuning key is no longer broadcast for the normal presets. Only the Ultra preset
opts into an explicit hard cap at the validated native maximum (50), because
Ultra also drives 1 ms pacing and must not let the self-resolver exceed the
validated throughput. This resolves the contradictory payload tests (default/High
must omit the key; Ultra must send `50`).
