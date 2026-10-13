<p align="center">
  <img src="assets/store/logo-512.png" width="128" alt="NanoTask"/>
</p>

<h1 align="center">NanoTask</h1>

<p align="center">
  <strong>O fantasma da bandeja.</strong> Monitor de hardware, task manager killer e otimizador — em 3–6 MB de RAM.
</p>

<p align="center">
  <a href="https://github.com/brunocsilva41/nanotask/actions/workflows/ci.yml"><img src="https://github.com/brunocsilva41/nanotask/actions/workflows/ci.yml/badge.svg" alt="CI"/></a>
  <a href="https://github.com/brunocsilva41/nanotask/releases"><img src="https://img.shields.io/github/v/release/brunocsilva41/nanotask" alt="Release"/></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/brunocsilva41/nanotask" alt="MIT"/></a>
</p>

## Por que existe

Gerenciadores de tarefa e monitores de hardware consomem mais recursos do que monitoram. O NanoTask inverte isso: um processo **fantasma** (`HWND_MESSAGE`, sem janela) que vive na bandeja consumindo menos que uma aba de terminal — e só materializa a interface (WinUI 3, GPU) no clique.

| Estado | RAM |
|---|---|
| Fantasma (bandeja) | **3–6 MB** |
| Dashboard aberto (transitório) | ~70–95 MB |
| Após fechar (trim) | 3–6 MB de volta |

## Instalação

**Recomendado — instalador:** baixe `NanoTask-Setup-X.Y.Z.exe` em [**Releases**](https://github.com/brunocsilva41/nanotask/releases). Sem assinatura de código por enquanto (ver `docs/SIGNING.md`) — o SmartScreen vai pedir confirmação, é esperado.

**Portable:** baixe `NanoTask-portable-X.Y.Z.zip`, descompacte e rode. Para iniciar com o Windows, marque "autostart" no menu da bandeja.

**Do código:**
```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config MinSizeRel
.\build\MinSizeRel\NanoTask.exe --smoke-test
```

## O que faz

- **Task manager killer** — kill em árvore (sem órfãos), sniper `Ctrl+Shift+Click`, liberador de portas dev (3000/5432/8080).
- **Telemetria sem driver** — ícone da bandeja muda de cor pela temperatura (NVML → WMI → fallback); blackbox de 5 min para caçar picos; alerta de thermal throttling.
- **Zelador** — limpa StandbyList (admin, opt-in), `%TEMP%`, flush DNS.
- **Automação** — presets de prioridade/afinidade por processo, congela Docker/WSL (`NtSuspend`), EcoQoS em jogos.
- **QoL** — clipboard dev (20 itens), mixer por app, always-on-top, keep-awake condicional, DNS switcher, panic button.
- **3 modos de taskbar** — fantasma, fixo, minimizado (`docs/TASKBAR_MODES.md`).

## Documentação

| Doc | Assunto |
|---|---|
| `CONTRIBUTING.md` | Como contribuir (Conventional Commits, checklist de PR) |
| `docs/GIT_WORKFLOW.md` | Branches, hotfix de tag, reversão de release |
| `docs/RELEASE_PROCESS.md` | Como publicar versão |
| `docs/AUTO_UPDATE.md` | Auto-update opt-in + kill-switch |
| `docs/SIGNING.md` | Assinatura gratuita (SignPath/Certum) |
| `docs/BRANDING.md` | Identidade visual e assets |
| `SECURITY.md` | Limites de segurança e reporte |

## Reportar bug

Abra uma issue pelo formulário **Report de bug** (anexe versão, modo da taskbar e logs) — ou no app: menu da bandeja → *Reportar problema*, que já monta o pacote redigido.

## Licença

MIT — ver `LICENSE`.
