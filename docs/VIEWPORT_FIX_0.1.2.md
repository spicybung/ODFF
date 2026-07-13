# ODFF 0.1.2 viewport fix

## Fixed

- The delayed OpenGL callback no longer calls `ImGui::GetItemRectMin()` or `ImGui::GetItemRectMax()`.
- The exact viewport rectangle is captured during layout and stored in framebuffer pixels.
- OpenGL rendering and scissor clearing are restricted to the actual model viewport.
- The right-side model/collision/TXD panel and bottom status area are no longer overwritten by the viewport clear.
- The model background is now very light sky purple: `#DDE1FA`.
- Empty-viewport helper text now uses a dark color for readability.

## Rebuild

```bat
rmdir /s /q build
cmake -S . -B build -A x64
cmake --build build --config Release
```
