# Git Workflow — NanoTask

## Regra de ouro
`main` sempre instalável. Nada commita direto nela.

Proteção (Settings > Branches > `main`): exigir PR + `ci / build-windows` verde + dispensar approvals stale.

## Branches
- `feat/<slug>` · `fix/<slug>` · `perf/<slug>` · `chore/<slug>`
- PR curto para `main`. Squash merge.

## Hotfix a partir de tag
```powershell
git fetch --tags
git checkout -b fix/travamento-tray v1.0.0
# ... corrige, testa smoke ...
git commit -m "fix(tray): evita double-free no NIM_MODIFY"
# volta p/ main via PR; depois release patch:
# atualiza VERSION -> 1.0.1, commit chore(release), tag v1.0.1
```

## Reverter release ruim
1. Na página da Release ruim: Edit > marque **Pre-release** + aviso "NAO USAR — regressao X".
2. `fix/` revertendo o commit causador, PR para `main`.
3. Nova tag patch (`v1.0.1`) — o `release.yml` publica o instalador bom.

## VERSION vs tag
- `VERSION` = fonte da verdade no `main` (CI lê de lá).
- Tag `vX.Y.Z` = gatilho da release (deve bater com `VERSION`).
- Ordem: bump `VERSION` + commit `chore(release): vX.Y.Z` → `git tag vX.Y.Z` → `git push origin main vX.Y.Z`.
