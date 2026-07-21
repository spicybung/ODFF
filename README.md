# ODFF

Open-source RenderWare DFF viewer and SA-MP/open.mp collision tool.

## 0.2.0

- Renders DFF materials with their referenced TXD textures instead of always
  displaying prelight vertex colours.
- Decodes D3D8/D3D9 DXT1, DXT3, DXT5, common uncompressed, and paletted TXD
  rasters into OpenGL textures.
- Preserves embedded alpha and applies standard source-alpha blending.
- Uploads complete mip chains and respects RenderWare filtering and addressing
  flags.
- Resolves DFF texture names case-insensitively and without filename
  extensions.
- Scans the DFF directory and adjacent `TXD`, `Textures`, or `Texture`
  directories for a dictionary containing the referenced materials.
- Reports decoded mip counts, alpha, decode failures, and resolved material
  texture counts in the properties panel.
- Resolves shared entries in RenderWare material lists instead of assuming
  every material slot owns a separate material chunk.
- Shows `SAMP Collision: passing` with a green check, or
  `SAMP Collision: failing` with a red X.
- Reads saved 2DFX lights and previews both their coloured glow and their
  light on the model.
- Uses the dark viewer background by default.
- Includes a Windows build file that finds the ODFF folder automatically and
  keeps the window open when a build fails.
