# PoseAnchor branding

The eight SVGs are the original, unmodified files supplied for PoseAnchor.
The horizontal and stacked wordmarks contain outlined paths, not font files.

- `poseanchor-icon.svg`: primary dark-background application icon.
- `poseanchor-icon-cyan.svg`: alternate cyan-background icon.
- `*-ink.svg`: wordmarks/symbol for light backgrounds.
- `*-white.svg`: wordmarks/symbol for dark backgrounds.
- `poseanchor-icon.png`: generated preview; the Windows ICO is committed at
  `src/status/PoseAnchorStatus.ico` in the repository.

The app, Setup, Windows Apps entry and Start-menu shortcuts use the same primary
icon. README uses theme-aware horizontal wordmarks with descriptive alt text.
Warnings retain the standard Windows warning symbol.

## Regeneration (maintainers only)

Use Python with Pillow 12.3.0 and Node.js with sharp 0.35.5 available through
Node's module search path (or `NODE_PATH`):

```powershell
python scripts/generate-icon.py
python scripts/generate-icon.py --check
```

`--node <path>` selects a specific Node executable. Regeneration converts the
supplied SVG, without redesigning it. ICO frames: 16, 20, 24, 32, 40, 48, 64,
128 and 256 px, with transparency. These tools are not shipped or loaded at
runtime; normal CMake builds use the committed ICO directly.
