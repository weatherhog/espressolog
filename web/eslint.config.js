// The lint exists for exactly one rule: react-hooks/rules-of-hooks.
//
// A hooks violation is invisible to `vite build` and to every test in this
// repo — it compiles, it bundles, it deploys, and then React throws at
// runtime the first time the hook count changes between renders. That
// happened here: a `useState` below an early `return` white-screened the
// capture screen on its own advertised workflow, and four rounds of
// human-and-model review had missed it. Static analysis would not have.
//
// Everything else is deliberately quiet. This is a single-user PWA, not a
// codebase that needs a style opinion, and a lint that cries wolf about
// unused variables is a lint that gets `--no-verify`d past.

import js from '@eslint/js'
import globals from 'globals'
import reactHooks from 'eslint-plugin-react-hooks'
import reactRefresh from 'eslint-plugin-react-refresh'
import { defineConfig, globalIgnores } from 'eslint/config'

export default defineConfig([
  // prototype/ is the original single-file sketch the app was cut from. It is
  // kept for reference and is not built.
  globalIgnores(['dist', 'prototype']),
  // The config files at the root run in node, not the browser. Without this
  // block they match no `files` pattern, which means zero rules — and eslint
  // reports a file with zero rules exactly like a file that passed.
  {
    files: ['*.config.js'],
    extends: [js.configs.recommended],
    languageOptions: { globals: globals.node },
  },
  {
    files: ['src/**/*.{js,jsx}'],
    extends: [
      js.configs.recommended,
      // recommended-latest carries the React Compiler rules too, which catch
      // the mutation-during-render mistakes that make a component render
      // differently on a re-run. Worth having; they cost nothing to satisfy.
      reactHooks.configs.flat['recommended-latest'],
      reactRefresh.configs.vite,
    ],
    languageOptions: {
      ecmaVersion: 2022,
      globals: globals.browser,
      parserOptions: {
        ecmaFeatures: { jsx: true },
        sourceType: 'module',
      },
    },
    rules: {
      // Unused *arguments* are how you document a callback signature.
      // Unused imports and locals are still worth hearing about.
      'no-unused-vars': ['error', { args: 'none', varsIgnorePattern: '^_' }],
    },
  },
])
