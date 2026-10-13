# Changelog

Formato: `Mantido` o essencial por versão. Detalhe completo em cada GitHub Release.

## [Unreleased]

## [1.1.0] — 2026-10-13
- **UI WinUI 3 Bento completa**: Mica, 7 cards, ícones Fluent, empty states,
  energia em destaque, PANIC vermelho, mixer por app, toast de throttling.
- Host lança `NanoTask.UI.exe` no clique (lazy, processo separado); fallback
  dashboard Win32 quando a UI não está instalada.
- Anel compartilhado (`Local\NanoTaskBlackbox`): host publica 5 min de telemetria,
  UI desenha gráfico retroativo.
- Widget flutuante com Segoe UI, barras de CPU/RAM e fonte do sensor.
- Painel Win32 funcional como fallback (processos/portas/sistema/ações).

## [1.0.0] — 2026-10-13
- Processo fantasma (`HWND_MESSAGE`) + ícone dinâmico por temperatura (GDI).
- Wrappers RAII: tree-kill, kill-by-port, sniper, StandbyList, presets.
- Blackbox 5 min (PDH) + cadeia de sensores sem driver (NVML → WMI).
- Instalador Inno, CI + release automatizada, auto-update opt-in, 3 modos taskbar.
- Docs: contributing, segurança, signing, branding, git workflow.
