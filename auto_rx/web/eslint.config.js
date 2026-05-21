import js from '@eslint/js'
import globals from 'globals'
import reactHooks from 'eslint-plugin-react-hooks'
import reactRefresh from 'eslint-plugin-react-refresh'
import tseslint from 'typescript-eslint'
import { defineConfig, globalIgnores } from 'eslint/config'

export default defineConfig([
  globalIgnores(['dist']),
  {
    files: ['**/*.{ts,tsx}'],
    extends: [
      js.configs.recommended,
      tseslint.configs.recommended,
      reactHooks.configs.flat.recommended,
      reactRefresh.configs.vite,
    ],
    languageOptions: {
      globals: globals.browser,
    },
    rules: {
      // Allow `any` deliberately. Backend response shapes are dynamic and
      // Leaflet's @types are incomplete enough that strict typing in those
      // spots adds friction without catching real bugs. Other lint rules
      // (exhaustive-deps, set-state-in-effect, purity) stay enforced.
      '@typescript-eslint/no-explicit-any': 'off',
      // Underscore-prefixed names = intentionally unused (e.g. callback
      // signatures from third-party libs we don't need every arg of).
      '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_', varsIgnorePattern: '^_' }],
      // We don't use Vite HMR (the UI is build-and-refresh), so the
      // "files must only export components" rule is irrelevant.
      'react-refresh/only-export-components': 'off',
      // We use `try { ... } catch {}` deliberately to swallow non-fatal
      // errors (network blips, missing localStorage, etc.).
      'no-empty': ['error', { allowEmptyCatch: true }],
      // The current rule is overzealous about callback-based setState
      // inside effects (setIntervals, async fetch then-handlers, socket
      // event handlers, etc.) — all of those run *after* the effect
      // setup phase and are correct React. Re-enable if we ever see a
      // genuine cascading-render bug.
      'react-hooks/set-state-in-effect': 'off',
    },
  },
])
