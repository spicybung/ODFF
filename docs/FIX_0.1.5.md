# ODFF 0.1.5

## Cause of the remaining red rendering artifacts

Some DFFs contain degenerate triangles. These may be strip connectors, duplicate points, or nearly collinear faces. Filled rendering gives them no visible area, but drawing their edges as collision lines creates long red lines across the screen.

The previous collision builder checked only whether the three vertex indices were different. That was insufficient.

## Fix

Collision creation and collision preview now reject a triangle when:

- a vertex contains a non-finite coordinate
- every edge is effectively zero length
- the cross-product area is effectively zero
- its area is negligible relative to its longest edge

The collision overlay no longer moves geometry toward the camera in world space. It uses a depth-range bias, so the red wireframe remains visible without changing its shape.

The projection near and far planes now scale with the loaded model and camera distance rather than always using `0.01` to `1000000.0`.
