# Troubleshooting

## App starts but fonts/UI look wrong

- Ensure `editor_assets/fonts/` is present next to the executable root.
- If custom fonts are missing, ShaderLab falls back to default ImGui font.

## Build from VS Code fails due to toolchain/environment

- Re-run: `tools/dev_env.ps1`
- Retry build task after environment initialization.

## Installer warns about missing VC++ runtime bundle

- ShaderLab attempts to bundle `vc_redist.x64.exe` automatically.
- If not found, installer still builds but runtime installation step is skipped.

## Preview or shader compile issues

- Check diagnostics panel for compile errors.
- Confirm shader source files referenced by project are present.

## Clean solution export confusion

- Exported projects store shader links as `@file:<relative-path>` and source files under `assets/shaders/hlsl/`.
- This is expected and required for source-linked recompilation.
