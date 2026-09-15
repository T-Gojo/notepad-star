# Notepad Star identity

The mark combines a folded-page **N** with a four-point north-star spark. It is
original artwork for Notepad Star, not a modification of the Notepad++ logo.
The geometric wordmark is drawn as paths; no font installation is required.

| Asset | Use |
|---|---|
| `notepad-star-wordmark.svg` / `.png` | Primary lockup on light backgrounds |
| `notepad-star-wordmark-dark.svg` / `.png` | Reverse lockup on dark backgrounds |
| `notepad-star-symbol.svg` / `.png` | Standalone mark on light backgrounds |
| `notepad-star-symbol-light.svg` / `.png` | Standalone mark on dark backgrounds |
| `notepad-star-monochrome.svg` / `.png` | Single-color reproduction |
| `notepad-star-brand-preview.png` | Overview and small-size samples |
| `..\icons\notepad-star.svg` / `.png` / `.ico` / `.icns` | Application/window/installer icons |

Palette: Midnight `#15243F`, Signal Blue `#4968ED`, North Mint `#74EDDA`,
Paper `#F1F5FF`. Keep the symbol proportions and leave at least half the star's
width as clear space. Use the application tile at small sizes; do not squeeze
the full wordmark into a toolbar icon.

Regenerate the entire kit from the shared design geometry:

```powershell
python tools\generate_icon.py
```

The generator requires Pillow. SVGs remain editable vector assets; permanent
design changes should also be reflected in the generator before regeneration.
Artwork follows the repository license. No trademark availability claim is made.
