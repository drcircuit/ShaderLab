# Contributing to ShaderLab

Thanks for helping improve ShaderLab.

## First Steps

- Read `docs/README.md` for canonical documentation.
- Use `docs/archive/` only for historical context.
- Keep changes focused and minimal unless a broader refactor is explicitly discussed.

## Reporting Bugs

Open an issue with:

- clear repro steps,
- expected vs actual behavior,
- OS + GPU details,
- relevant logs/screenshots.

## Proposing Features

Open an issue with the enhancement context first.

Include:

- user value,
- scope,
- impact on size/performance constraints,
- alternatives considered.

## Pull Request Workflow

1. Create a branch from latest main.
2. Implement focused changes with clear commit messages.
3. Run local checks.
4. Update docs when behavior changes.
5. Open PR with rationale and validation notes.

## Build and Validation

Preferred local flow is workspace tasks:

- Build ShaderLab (Debug)
- Build ShaderLab (Release)
- Reconfigure CMake

Before validating from a plain shell, load environment with:

- .\tools\dev_env.ps1

Run checks with:

- .\tools\check.ps1

## Documentation Maintenance (Required)

If you change behavior, build flow, structure, or policies:

- update canonical docs in `docs/`,
- avoid adding new root-level status reports,
- keep historical notes in `docs/archive/`.

Run docs checks before opening PR:

- .\tools\check_docs.ps1

`tools/check_docs.ps1` validates:

- required canonical docs are present,
- archived pointer docs stay in archived format,
- local markdown links resolve.

## Runtime/Build Policy Notes

When changing build/export behavior, preserve and document:

- tiny presets: MicroPlayer + x86,
- open/free preset: full runtime + x64,
- runtime/compact debug flags default OFF for size-sensitive outputs.

## Boundary Rules (Required)

When contributing to editor/UI code (`src/app/ShaderLabMain/*`, `src/ui/*`, `include/ShaderLab/UI/*`):

- Use stable API surfaces only:
	- `ShaderLab/DevKit/*`
	- `ShaderLab/Core/*`
	- `ShaderLab/Graphics/*`, `ShaderLab/Audio/*`, `ShaderLab/Shader/*`
- Do not include runtime-internal app/runtime headers:
	- `ShaderLab/App/PlayerApp.h`
	- `ShaderLab/App/DemoPlayer.h`
	- `ShaderLab/App/Runtime*`
	- `ShaderLab/Runtime/*`
	- direct `src/app/runtime/*` includes

Enforcement:
- Local: `tools/check.ps1` includes an **Editor Include Boundary** gate.
- Release automation: `.github/workflows/release.yml` runs `tools/check.ps1` (non-blocking warning gate).

## Code Guidelines

- Use C++20 and existing project style.
- Prefer RAII and explicit ownership.
- Keep naming descriptive.
- Avoid unrelated refactors in the same PR.

## Licensing

By contributing code, you agree to license contributions under `LICENSE-COMMUNITY.md`.

By contributing creative assets, you agree to `creative/LICENSE.md`.

## Community

- Be respectful and constructive.
- Keep discussions technical and actionable.
- Help improve clarity for future contributors.
