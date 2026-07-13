# ODFF 0.1.6

The remaining preview problem was treated as a coordinate-space issue.

For mesh-face collision, the old preview rendered the flattened collision export buffer in clump space. The visible model was rendered geometry-by-geometry with its atomic frame transform. Even though those paths were intended to be equivalent, they did not reliably produce the same result for every DFF.

The new preview uses the source geometry and applies the exact same frame matrix as the model renderer. Therefore:

```text
visible geometry local vertices
    -> atomic world matrix
    -> camera view
    -> projection

collision preview local vertices
    -> the same atomic world matrix
    -> the same camera view
    -> the same projection
```

The flattened collision buffer remains in use for DFF/COL3 export. Only the OpenGL preview path changed.
