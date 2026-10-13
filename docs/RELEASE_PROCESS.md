# Processo de Release

Fonte única de versão: arquivo `VERSION` (semver `X.Y.Z`).

## Fluxo

1. Atualize `VERSION` + `docs/CHANGELOG.md` (seção Unreleased → versão).
2. `git commit -m "chore(release): vX.Y.Z" && git tag vX.Y.Z && git push origin main vX.Y.Z`
3. O workflow `release.yml` faz: build MinSizeRel com carimbo de versão
   (`version.rc`), smoke-test, instalador Inno (`dist/NanoTask-Setup-X.Y.Z.exe`),
   ZIP portable e GitHub Release com changelog dos commits.
4. Marque a release como `Latest` e anexe o SHA-256 dos artefatos (gerado no job).

## Versionamento

- `MAJOR`: quebra compatibilidade de `settings.json` ou protocolo do FileMapping.
- `MINOR`: feat nova (modo taskbar, provedor de temperatura).
- `PATCH`: fix de crash/vazamento, ajuste de instalador.

## Rollback

Toda release mantém o instalador anterior anexado. O auto-update (ver
`docs/AUTO_UPDATE.md`) só promove a nova versão após 24h sem spike de crash
reports; em regressão, publique `PATCH` revertendo e marque a tag ruim como
`Pre-release` com aviso.
