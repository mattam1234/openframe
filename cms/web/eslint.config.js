// ESLint flat config for the CMS Vue UI — mirrors the device frontend's config so
// both apps lint to the same bar (vue3 essential + accessibility).
import js from '@eslint/js'
import globals from 'globals'
import pluginVue from 'eslint-plugin-vue'
import pluginVueA11y from 'eslint-plugin-vuejs-accessibility'

export default [
  { ignores: ['dist/**', 'node_modules/**', 'playwright-report/**', 'test-results/**'] },
  js.configs.recommended,
  ...pluginVue.configs['flat/essential'],
  ...pluginVueA11y.configs['flat/recommended'],
  {
    languageOptions: {
      ecmaVersion: 'latest',
      sourceType: 'module',
      globals: { ...globals.browser, ...globals.es2022, ...globals.node },
    },
    rules: {
      'vue/multi-word-component-names': 'off',
      'vue/valid-v-slot': ['error', { allowModifiers: true }],
      // caughtErrors: 'none' keeps the ESLint 8 default (ESLint 9+ defaults to 'all').
      'no-unused-vars': ['warn', { argsIgnorePattern: '^_', ignoreRestSiblings: true, caughtErrors: 'none' }],
      // autofocus is only used to move focus into the login dialog on open — an
      // accessibility benefit, not the page-load autofocus this rule guards against.
      'vuejs-accessibility/no-autofocus': 'off',
    },
  },
]
