# Auto-update (opt-in, com kill-switch)

Princípios: nada atualiza sozinho sem consentimento; nada executa sem hash.

## Configuração (`settings.json`)

```json
{
  "updates": {
    "enabled": true,
    "channel": "stable",
    "checkIntervalHours": 24,
    "autoDownload": false
  }
}
```

- `enabled: false` desliga 100% da rede (nem checagem). É o default em builds portable.
- `channel`: `stable` (tags `v*`) ou `beta` (pre-releases).
- `autoDownload: true` baixa em background, mas **instalar sempre pede confirmação**
  com UAC + exibição do SHA-256.

## Como funciona (`UpdateManager.hpp`)

1. `CheckNow()` consulta `GET https://api.github.com/repos/<org>/nanotask/releases/latest`
   (ou `/releases` filtrando pre-release no canal beta).
2. Compara `tag_name` com a versão embutida (`APP_VERSION_STR`). Se maior, baixa o
   asset `NanoTask-Setup-X.Y.Z.exe` + `.sha256`.
3. Verifica SHA-256 do bytes baixados. Hash divergente = descarta + log + Toast de alerta.
4. Chama o instalador com `/SILENT /CLOSEAPPLICATIONS` somente após clique do usuário.
5. Falha de rede silenciosa: loga e tenta no próximo intervalo (backoff 1h→24h).
   Sem telemetria, sem envio de dados — só GET público no GitHub.

## Riscos cobertos

| Risco | Mitigação |
|---|---|
| MITM / release trocada | HTTPS + SHA-256 obrigatório; sem hash, sem instalar |
| Update quebrado em massa | Canal `stable` só promovido 24h após tag; `enabled:false` = imune |
| Loop de update | Lock-file + mutex; instalador mata processo via `CloseApplications` |
