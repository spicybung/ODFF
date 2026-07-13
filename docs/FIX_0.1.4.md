# ODFF 0.1.4

## Collision preview

The old preview asked OpenGL to draw every collision triangle in polygon-line mode. On dense meshes this produced heavy duplicate edges, coplanar flicker, and large clipping artifacts.

The new preview:

- extracts each unique collision edge once
- validates all face indices
- rejects impossible long-edge corruption
- preserves the model's polygon rendering state
- offsets preview lines slightly toward the camera
- keeps depth testing enabled so hidden lines remain hidden

The collision data used for DFF export is unchanged.

## Camera

The old camera clamped pitch between -89 and +89 degrees. The new target-orbit camera wraps both pitch and yaw through 360 degrees and uses a calculated camera basis for viewing and panning.

## Interface

- `Open DFF Group` removed
- `Attach To Group` renamed to `Attach Collision to All`
- Hover tooltip: `Attach collision to DFF group`
