# Política de Segurança

## Escopo

NanoTask executa ações privilegiadas por design (encerrar processos, limpar
StandbyList, trocar DNS). Este documento define os limites para não virar malware.

## Regras inegociáveis

1. **Mínimo privilégio**: o processo roda `asInvoker`. Elevação (UAC) só na ação
   específica que exige, nunca no boot. Sem `requireAdministrator` no manifest.
2. **Kill-switch de processos críticos**: `Safety::IsCriticalPid()` bloqueia
   `TerminateProcess`/`Suspend` em PID ≤ 4, `csrss.exe`, `wininit.exe`, `services.exe`,
   `lsass.exe`, `smss.exe` e no próprio NanoTask. Bypass = bug crítico.
3. **Sem execução de strings**: proibido `system()`, `ShellExecute` com input do
   usuário, `CreateProcess` com linha montada por concatenação não sanitizada.
   DNS usa API (`SetDnsSettings`), não `netsh`.
4. **Paths seguros**: `%TEMP%`/`hosts` resolvidos via `SHGetKnownFolderPath` +
   canonicalização; escrita fora de `%LOCALAPPDATA%\NanoTask` exige confirmação na UI.
5. **Rede**: o auto-update só baixa de `github.com/<org>/nanotask/releases` via
   HTTPS com hash SHA-256 verificado antes de executar. Sem HTTP, sem mirror
   configurável sem assinatura.
6. **Memória**: sem `new[]` em hot-path; buffers com tamanho máximo (`MAX_PATH`,
   ring de 300); todo `MapViewOfFile`/`HICON`/`HHOOK` tem owner RAII e caminho de
   liberação mesmo em falha parcial.
7. **Crash containment**: a UI roda em processo separado. Crash da UI nunca derruba
   o monitoramento (ver `docs/TASKBAR_MODES.md`).

## Reportar vulnerabilidade

**Não abra issue pública.** Envie e-mail para o mantenedor com: versão, PoC mínimo,
impacto (RCE / privesc / DoS local). Resposta em até 5 dias úteis; correção
prioritária sai como `fix(security)` + release patch em até 14 dias.
