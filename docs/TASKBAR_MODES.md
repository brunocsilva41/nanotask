# Modos da barra de tarefas

O NanoTask é fantasma por padrão. Três modos, trocáveis com 1 clique no menu do tray
e persistidos em `HKCU\SOFTWARE\NanoTask\TaskbarMode` + `settings.json`:

| Modo | Valor | Comportamento |
|---|---|---|
| **Fantasma** | `0` | Só ícone na bandeja (`HWND_MESSAGE`, sem botão na taskbar). `ShowWindow` nunca chamado. RAM 3–6 MB. |
| **Fixo** | `1` | Janela mini-dashboard ancorada (botão visível na taskbar, `WS_EX_APPWINDOW`). Clique alterna mostrar/ocultar. |
| **Minimizado** | `2` | Abre minimizado na taskbar; ao minimizar volta sozinho (`WM_SIZE/SIZE_MINIMIZE` → `ShowWindow(SW_HIDE)` + reaparece no tray). |

## Personalização

- **Fixar/desafixar**: menu do tray → "Fixar na barra" alterna Fantasma ↔ Fixo sem reiniciar.
- **Ícone dinâmico**: temperatura desenhada no `HICON` (verde <65°C, amarelo ≤80°C, vermelho >80°C) + tooltip com CPU/RAM. Ícones base em `assets/tray/`.
- **Notificações**: Toast só para thermal throttling e update pronto (respeita Focus Assist).
- **Boot**: tarefa `Run` com `--tray` (modo fantasma). Sem UAC no boot (manifest `asInvoker`).

## Regras de UX

1. Um clique esquerdo = ação primária do modo (fantasma abre dashboard, fixo alterna visibilidade).
2. Clique direito = menu completo (modos, update, report de bug, sair).
3. Fechar a janela (`X`) nunca encerra o app — volta para a bandeja. Sair só pelo menu "Encerrar".
