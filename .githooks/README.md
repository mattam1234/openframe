# Git hooks

Project-managed git hooks (kept in-repo so every clone shares the same checks).

## Enable

```sh
git config core.hooksPath .githooks
```

Run that once per clone. Hooks are skipped for a single commit with `git commit --no-verify`.

## `pre-commit`

Runs only for the areas you actually touched:

- **frontend/** (`.vue`/`.js`/`.ts` staged) → `npm run lint`
- **cms/** (`.ts` staged) → `npm run typecheck`
- **firmware/** (`.cpp`/`.h`/…) → `clang-format --dry-run --Werror` *(only if a `.clang-format` and the `clang-format` binary are both present)*

If a tool's `node_modules` is missing, that check is skipped with a note rather than failing.
