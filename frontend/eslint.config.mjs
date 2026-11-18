// ESLint flat config for the OpenFrame Vue frontend (ESLint 10 + eslint-plugin-vue).
// vue3 "essential" catches real correctness issues without imposing a full style
// restyle on the existing codebase. (.mjs because this package is not "type": "module".)
import js from '@eslint/js'
import globals from 'globals'
import pluginVue from 'eslint-plugin-vue'
import pluginVueA11y from 'eslint-plugin-vuejs-accessibility'

export default [
  // Built output and static service-worker assets aren't linted as source.
  { ignores: ['dist/**', 'public/**', 'playwright-report/**', 'test-results/**'] },
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
      // Vuetify uses dotted dynamic slot names (e.g. #item.foo) on data tables/steppers.
      'vue/valid-v-slot': ['error', { allowModifiers: true }],
      // caughtErrors: 'none' keeps the ESLint 8 default (ESLint 9+ defaults to 'all').
      'no-unused-vars': ['warn', { argsIgnorePattern: '^_', ignoreRestSiblings: true, caughtErrors: 'none' }],
      // `autofocus` is only ever used to move focus INTO a dialog / the command
      // palette when it opens — that's an accessibility benefit, not the page-load
      // autofocus this rule guards against.
      'vuejs-accessibility/no-autofocus': 'off',
    },
  },
]
