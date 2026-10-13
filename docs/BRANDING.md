# Identidade NanoTask

Conceito: **fantasma que mede**. Fundo dark `#0B0F19`, acento verde `#4ADE80`
(saudável), amarelo `#FACC15` (alerta), vermelho `#F87171` (crítico).

## Arquivos

| Arquivo | Uso | Tamanhos |
|---|---|---|
| `assets/app.ico` | EXE, Explorer, taskbar (modo fixo), `version.rc` | 16–256 (7 frames) |
| `assets/tray/tray-{green,yellow,red}.ico` | Base do ícone dinâmico por faixa de temperatura | 16/24/32 |
| `assets/installer/setup.ico` | Janela do instalador | 16–256 |
| `assets/installer/banner.bmp` | Wizard lateral Inno (164×314) | — |
| `assets/installer/header.bmp` | Topo das páginas Inno (55×58) | — |
| `assets/store/logo-512.png` | README, GitHub About, loja | 512 |

## Regras

1. Nunca esticar PNG no código — o `HICON` dinâmico é **desenhado via GDI**
   (`TrayHost::MakeTempIcon`) com `TempToColor()`; os `.ico` são fallback/boot.
2. Regenerar: edite a função `brand()` no script de geração e rode o bloco Python
   documentado no histórico (Pillow). Valide com o parse do header ICO
   (`count` = 7 no app.ico).
3. Contraste: texto do tray sempre branco sobre fundo dark; nunca usar o acento
   como fundo de texto pequeno.
