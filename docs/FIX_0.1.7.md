# ODFF 0.1.7

## Confirmed diagnosis

`bridge_1.dff` has ordinary finite geometry bounds of roughly `[-3.71, -1.47, -1.60]` to `[3.72, 1.48, 1.60]`. The generated mesh collision has the same world-space bounds. The red lines at the viewport edges were therefore not oversized collision coordinates.

The remaining corruption came from fixed-function OpenGL line rendering inside the Dear ImGui callback. The model triangles rendered normally, while line clipping/state produced segments at the viewport boundaries.

## Replacement preview path

ODFF now renders collision as a screen-space ImGui overlay:

1. Read generated collision vertices in world space.
2. Transform them into the camera basis.
3. Clip each edge against the camera near plane.
4. Apply the same 55-degree perspective projection as the OpenGL viewport.
5. Clip each projected segment to the actual viewport rectangle.
6. Draw the resulting red segments after ImGui resets the OpenGL state.

Mesh collision uses deduplicated triangle edges. Box collision uses only the twelve actual box edges, without face diagonals. DFF and grid rendering remain OpenGL-based.
