# ShaderLab Editor Fonts

This directory contains custom fonts used exclusively in the ShaderLab editor interface. These fonts are **not** included in runtime builds to keep demoscene executable sizes minimal.

## Fonts Included

### Hacked
- **File:** `Hacked-KerX.ttf`
- **Usage:** Logo text ("SHADERLAB" in menu bar) and large headings
- **Sizes:** 48px (logo), 20px (headings)
- **Style:** Futuristic, technical, monospaced display font

### Orbitron
- **Files:** `OrbitronMedium-Bz9B.ttf`, `OrbitronLight-R7GV.ttf`, `OrbitronBold-10M0.ttf`, `OrbitronBlack-n6dV.ttf`
- **Usage:** Default UI text (buttons, labels, menus)
- **Size:** 15px
- **Style:** Futuristic, geometric sans-serif
- **Used:** Medium weight as default

### Erbos Draco NBP
- **File:** `ErbosDraco1StNbpRegular-99V5.ttf`
- **Usage:** Numerical input fields and displays
- **Size:** 14px
- **Style:** Monospaced technical font optimized for numbers

## Design Theme

The editor UI follows a futuristic cyberpunk/demoscene aesthetic with:
- **Dark blue-black backgrounds** (RGB: 2-4, 3-6, 4-8)
- **Bright cyan/teal accents** (RGB: 0, 255, 255)
- **Sharp, angular geometry** (no rounded corners)
- **High contrast** for readability in dark environments

## Font Loading

Fonts are loaded in `UISystem::Initialize()` and stored as ImFont pointers:
- `m_fontHackedLogo` - Large logo font
- `m_fontHackedHeading` - Heading font
- `m_fontOrbitronText` - Default UI font (set as `io.FontDefault`)
- `m_fontErbosDracoNumbers` - Numerical fields font

## License Information

Please refer to the license files that came with each font package for usage terms.

## Editor-Only Notice

⚠️ **IMPORTANT:** These fonts are only used in the editor application. They are NOT included in runtime/player builds or exported demoscene executables to minimize file size.
