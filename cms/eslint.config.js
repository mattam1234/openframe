/* ESLint (flat config) for the OpenFrame CMS (TypeScript, Node). Uses the
 * non-type-checked typescript-eslint recommended set — catches real correctness
 * issues (unused vars, unsafe patterns) without requiring the full type-aware
 * program or a codebase-wide restyle. tsc (`npm run typecheck`) already covers
 * type errors. */
const js = require('@eslint/js');
const tseslint = require('typescript-eslint');

module.exports = tseslint.config(
  { ignores: ['dist/', 'node_modules/', 'public/', 'web/'] },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    languageOptions: { ecmaVersion: 'latest', sourceType: 'module' },
    rules: {
      // Allow intentionally-unused args/vars prefixed with _.
      '@typescript-eslint/no-unused-vars': ['warn', { argsIgnorePattern: '^_', varsIgnorePattern: '^_' }],
      'no-unused-vars': 'off',
      // `any` is used deliberately for dynamic JSON/test fixtures; tsc (strict) still
      // type-checks the rest. Empty catch blocks are an intentional ignore pattern.
      '@typescript-eslint/no-explicit-any': 'off',
      'no-empty': ['error', { allowEmptyCatch: true }],
    },
  },
);
