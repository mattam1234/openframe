/* ESLint config for the OpenFrame Vue frontend (ESLint 8 + eslint-plugin-vue).
 * vue3-essential catches real correctness issues without imposing a full style
 * restyle on the existing codebase. */
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
  ],
  parserOptions: {
    ecmaVersion: 'latest',
    sourceType: 'module',
  },
  rules: {
    'vue/multi-word-component-names': 'off',
    // Vuetify uses dotted dynamic slot names (e.g. #item.foo) on data tables/steppers.
    'vue/valid-v-slot': ['error', { allowModifiers: true }],
    'no-unused-vars': ['warn', { argsIgnorePattern: '^_', ignoreRestSiblings: true }],
  },
  // Built output and static service-worker assets aren't linted as source.
  ignorePatterns: ['dist', 'public'],
}
