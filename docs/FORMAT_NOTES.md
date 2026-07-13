# Format notes

ODFF keeps format parsing and UI code separate.

The DFF reader follows the RenderWare chunk hierarchy:

```text
Clump
  Struct
  Frame List
    Struct
    Extension per frame
  Geometry List
    Struct
    Geometry
      Struct
      Material List
      Extension
  Atomic
```

The generic geometry struct reader handles:

- geometry flags
- prelighting
- one or more UV sets
- triangle material indices
- morph-target vertices
- morph-target normals

Native geometry is rejected with an explicit message rather than guessed.

The TXD reader currently indexes native texture metadata:

- platform id
- raster format
- D3D format
- name and mask name
- width and height
- depth
- mipmap count

Raster payload decoding belongs in a later `TextureDecoder` unit so D3D, PS2, Xbox, and mobile layouts do not become mixed into dictionary parsing.
