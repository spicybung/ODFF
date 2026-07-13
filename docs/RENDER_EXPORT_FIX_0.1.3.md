# ODFF 0.1.3

## Viewport

The full screenshot showed that the properties panel was present. The actual failure was that the OpenGL callback cleared the viewport but inherited Dear ImGui's OpenGL texture and fixed-function state, so the grid and model geometry were not reliably visible.

ODFF now:

- saves and restores the complete OpenGL attribute state
- saves and restores projection and model-view matrices
- disables Dear ImGui's bound font texture during model drawing
- disables lighting, fog, alpha testing, blending, and stencil state
- keeps depth testing enabled for the DFF
- keeps the very light sky-purple viewport background

## Export

The `.odffcol` project-file path has been removed.

`Export DFF` writes the selected model.

`Export DFF Group` writes all uniquely named models to a selected folder.

The exporter preserves the original DFF byte stream and replaces or adds the clump collision extension. Attached collision is encoded as SA-MP COL3 and placed in RenderWare plugin `0x0253F2FF`.

Empty collision is treated as an attached empty COL3 rather than as no collision.
