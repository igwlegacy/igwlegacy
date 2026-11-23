# Vendored: LuaJIT

This directory is an unmodified copy of the upstream LuaJIT repository.

- Upstream: https://github.com/LuaJIT/LuaJIT (branch `v2.1`)
- Commit: 96f7b297326e9fd454b7186c6858b2773f94a0dc
- License: MIT (see `COPYRIGHT`)

The `.relver` file contains the commit timestamp substituted by
`git archive` (upstream marks it `export-subst`). The LuaJIT build uses it
to derive the release version, so keep it when updating.

To update, replace the contents of this directory (except this file) with
`git archive` output of the desired upstream commit and update the commit
reference above.
