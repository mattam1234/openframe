/* ESLint for the CMS Vue UI — mirrors the device frontend's config so both apps
 * lint to the same bar (vue3-essential + accessibility). */
module.exports = {
  root: true,
  env: {
    browser: true,
    es2022: true,
    node: true,
  },
  extends: [
    'eslint:recommended',
    'plugin:vue/vue3-essential',
    'plugin:vuejs-accessibility/recommended',
  ],
  parserOptions: {
    ecmaVersion: 'latest',
    sourceType: 'module',
  },
  rules: {
    'vue/multi-word-component-names': 'off',
    'vue/valid-v-slot': ['error', { allowModifiers: true }],
    'no-unused-vars': ['warn', { argsIgnorePattern: '^_', ignoreRestSiblings: true }],
    // autofocus is only used to move focus into the login dialog on open — an
    // accessibility benefit, not the page-load autofocus this rule guards against.
    'vuejs-accessibility/no-autofocus': 'off',
  },
  ignorePatterns: ['dist', 'node_modules', 'playwright-report', 'test-results'],
}
