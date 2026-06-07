# ADR-0001: Shared Scalar Colormap Library (`marine_colormap`)

## Status

Accepted (2026-06-07). Tracked by
[rolker/unh_marine_autonomy#137](https://github.com/rolker/unh_marine_autonomy/issues/137).

This is the first ADR in this repository; it establishes `docs/decisions/` here as
the home for **cross-cutting marine-software architecture decisions** — those that
span multiple project repos (rqt plugins, rviz displays, CAMP, shared libraries),
as distinct from the workspace's own `docs/decisions/`, which records
agent-framework decisions.

## Context

Scalar-field colormapping — mapping a value to a display color through a named
palette and a transfer function (range normalize → gain → contrast/gamma →
alpha) — is reimplemented in at least three places, all slightly differently:

- **`rviz_sonar_image::ColorMap`** (Ogre/rviz, `jazzy` branch): maps on the
  **CPU** (`lookup(value)` per sample) to fill an RGBA Ogre texture, re-baked when
  the range changes. 13 thermal stops (one duplicated), interpolates in **float
  linear RGB**, returns **white for `value ≤ min`** as a no-data/background
  sentinel, supports an alpha range (`setAlphaRange`), default range −70..0 dB.
- **`rqt_sonar_waterfall::ColorMap`** (Qt, `rqt_operator_tools`): a second copy;
  its Thermal palette is "adapted from rviz_sonar_image". 12 thermal stops (the
  duplicate dropped), interpolates in **8-bit**, clamps to the first stop below
  range, splits `scale_intensity(value,min,max,gain,contrast)` from `lookup(t)`,
  has its own auto-range, persists palette selection **by index**.
- **`camp::map::ColorMap`** (proposed,
  [camp#63](https://github.com/rolker/camp/issues/63)): a third, still-unbuilt
  copy wanting `grayscale` + `viridis`/`turbo` with per-layer range.

Two forces motivate consolidation:

1. **Duplication and drift.** The copies have already diverged — the two
   "thermal" palettes do not produce the same colors (different stop counts,
   different interpolation space, different below-range behavior). Viewers of the
   same data show different colors.
2. **Bit depth.** We want a path not bound to 8-bit input. A GPU path can keep
   full-precision input.

The hard constraint: the three consumers sit on **three different rendering
substrates**, so a single shared *runtime renderer* is not possible.

| Consumer | Substrate | GPU path |
|---|---|---|
| rqt plugins | Qt widgets | `QOpenGLWidget` + GLSL (raw GL via Qt) |
| rviz displays | Ogre3D (rviz2 jazzy = Ogre 1.x) | Ogre material/program + RTSS |
| CAMP | `QGraphicsScene` (CPU/QPainter) | needs a GL viewport, or stays CPU |

Live GL/texture objects cannot be shared across Qt-GL, Ogre, and QGraphicsScene.

## Decision

Extract colormapping into a layered library, and share only the layers *below*
the renderer.

### Home: a standalone repository

`marine_colormap` lives in its **own repository**
([rolker/marine_colormap](https://github.com/rolker/marine_colormap), public,
`jazzy`), **not** inside a consumer or tools repo. Rationale (an earlier draft
proposed hosting it inside `marine_perception_tools`; adversarial review rejected
that):

- `rviz_sonar_image` and `camp` would otherwise hard-depend on an unrelated repo,
  dragging its CI, license, and branch policy into their build graphs.
- The shared **appearance contract** needs to be **versioned independently**: a
  palette/transfer change re-colors three downstreams, so it must be a tagged,
  semver-gated dependency — not an incidental side effect of a tools-repo commit.

### Tier 1 — `marine_colormap` core (no Qt / Ogre / GL)

A ROS 2 (ament) package whose **public CMake interface pulls in neither Qt nor
Ogre**, so an Ogre consumer (rviz) and Qt consumers can all depend on it cleanly.
Acceptance criterion: `ament_export_*` exposes headers/targets with zero Qt/Ogre
in the public dependency set. It provides:

- **Palettes:** `grayscale`, `bronze`, `thermal` plus `viridis`, `turbo`. The
  perceptual ramps must come from a **cited canonical source** (matplotlib's
  viridis table, Google's turbo polynomial, or a vetted header-only lib such as
  tinycolormap) — not hand-rolled, to avoid "our turbo ≠ everyone's turbo". The
  palette **registry is append-only / name-keyed**: consumers persist selection
  **by name**, so adding a palette never renumbers and silently changes a saved
  UI setting.
- **Transfer function** as an explicit params struct, not a bag of positional
  args. It enumerates: range normalize(min,max) → gain → contrast/gamma (a fixed,
  documented order) → **alpha ramp** (value-dependent alpha, baked into the LUT);
  plus **no-data / NaN** handling and a distinct **below-floor / above-ceiling
  color** (rviz's "white below min" is exactly the sentinel a clamped LUT cannot
  otherwise express). The **normalize step is its own pure function** so the CPU
  path and the future GPU shader call the *identical* formula and cannot diverge.
- **Color representation contract:** the plain color type and the LUT have a
  defined color space (**linear vs sRGB**), alpha convention (**straight vs
  premultiplied**), and storage (the LUT is 8-bit RGBA for display; CPU `lookup`
  returns float and quantizes only at the final output). Consumers convert to
  `QColor` / `Ogre::ColourValue` at their boundary.
- `bake_lut(colormap, transfer, N) -> Lut` (the 1-D LUT the GPU path uploads) and
  a CPU `lookup(...)`.

Pure C++, unit-testable with **no GL context** (runs in headless CI). It is the
single source of truth for appearance.

### Tier 2 — shared GLSL *math* + LUT-as-texture (later phase)

A shared GLSL **function body** (normalize → transfer → 1-D LUT sample), plus the
baked LUT bytes. This is **not** a drop-in fragment program for every consumer:
the binding is per-substrate, and on rviz2/Ogre 1.x it means authoring an Ogre
`.material`/`.program` with Ogre's auto-param binding and RTSS — a separate
shader-integration effort, not raw GLSL injection. The realistic shared artifact
is the math snippet, concatenated into each consumer's shader.

Portability requirements (desktop GL via Qt, GLES targets, Ogre): use **`R32F`**
for the scalar texture and **`highp`** in the fragment shader (dB-range data
bands at `mediump`); do **not** assume linear filtering of the scalar texture
(R32F linear filtering is not universally guaranteed on GLES — filter the LUT, or
nearest-sample the scalar); and treat the `#version` difference (`330` vs
`300 es`) as a real precision/sampler/`texture()` matrix, not a one-line shim.
Tier-2 testing needs an offscreen GL context (EGL / software GL) — Tier-1's
headless C++ tests do not cover it.

Full-precision input is achieved by keeping raw samples in the R32F scalar
texture and normalizing in the shader; input is never quantized to 8-bit before
colormapping. (The CPU path likewise stays float through `lookup` and quantizes
only the final color — there is **no** inherent 8-bit input quantization on
either path.)

### Migration fidelity: pick a canonical reference

"Preserve current colors" cannot hold for *both* existing consumers — their
thermal palettes already differ (13 vs 12 stops, float vs 8-bit interpolation,
white-below-min vs clamp). The core therefore **pins one canonical definition**
per palette (stop list, count, interpolation space, below/no-data behavior), and
the migration accepts a documented, bounded pixel delta on whichever consumer was
not the reference. A **golden-LUT / golden-image conformance test** locks the
canonical reference so it cannot drift. (The thermal reference and the
below-range sentinel are to be chosen during #137's core-lib work — rviz's
float-space + white-below-min is the stronger candidate as the more capable
behavior.)

### Seam

Each consumer keeps its own small renderer integration but depends on the same
Tier 1 core (and, in the GPU phase, the same Tier 2 math + LUT). We do **not**
build a shared runtime renderer.

### Out of scope (noted, not decided here)

Consolidating the *code* does not stop two viewers of the same topic being
configured to different palettes/ranges. Making the colormap/range a **ROS config
contract** that travels with the data (a param or small message field) would
close that gap; it is a separate, later decision.

### Sequencing

1. **First cut:** Tier 1 core in [rolker/marine_colormap](https://github.com/rolker/marine_colormap)
   ([#1](https://github.com/rolker/marine_colormap/issues/1)) + migrate the CPU
   consumers ([rqt_operator_tools#44](https://github.com/rolker/rqt_operator_tools/issues/44),
   [rviz_sonar_image#4](https://github.com/rolker/rviz_sonar_image/issues/4) on
   `jazzy`); satisfy [camp#63](https://github.com/rolker/camp/issues/63) by adopting
   the core (CPU) rather than a camp-local `ColorMap`. No GPU yet.
2. **Follow-up phase:** Tier 2 GLSL renderer for the rqt family (`QOpenGLWidget`) —
   the >8-bit waterfall; then an Ogre GPU material for `rviz_sonar_image`; CAMP GPU
   only if it moves to a GL viewport.

## Consequences

- **Positive:** one palette/transfer definition; identical colors across viewers
  (locked by a golden test); headless-testable core; an independently versioned
  appearance contract; a clean base for the >8-bit GPU path.
- **Cost:** a new repo to maintain, and a real inter-repo version discipline
  (downstreams pin a `marine_colormap` version; appearance changes are
  deliberate, tagged releases). The clean `ament_export` (no Qt/Ogre leakage) is
  explicit acceptance work, not a given.
- **Migration is not pixel-transparent:** one existing consumer's colors shift to
  the canonical reference; the delta is documented, not silent.
- **Tier-2 remains per-consumer effort:** the GPU phase shares math + LUT, not the
  Ogre/Qt/QGraphicsScene plumbing; the Ogre material path is the main cost driver
  for rviz, and GLES portability needs the format/precision care noted above.

## References

- Umbrella: [rolker/unh_marine_autonomy#137](https://github.com/rolker/unh_marine_autonomy/issues/137)
- Core lib: [rolker/marine_colormap#1](https://github.com/rolker/marine_colormap/issues/1)
- rqt migration: [rolker/rqt_operator_tools#44](https://github.com/rolker/rqt_operator_tools/issues/44)
- rviz migration (jazzy): [rolker/rviz_sonar_image#4](https://github.com/rolker/rviz_sonar_image/issues/4)
- CAMP adoption: [rolker/camp#63](https://github.com/rolker/camp/issues/63)
