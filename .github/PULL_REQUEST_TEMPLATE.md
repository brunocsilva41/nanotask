## O que muda?

<!-- Descreva o problema ou a motivação. Link a issue: Fixes #123 -->

## Tipo

- [ ] fix (corrige travamento/vazamento)
- [ ] feat (nova funcionalidade, ex: modo taskbar)
- [ ] perf (reduz RAM/CPU)
- [ ] docs / chore / ci

## Checklist do autor

- [ ] `cmake --build` passa em MinSizeRel sem warnings (`/WX`)
- [ ] Sem `new`/`delete` manual — só RAII (`UniqueHandle`, `winrt::com_ptr`)
- [ ] Toda chamada Win32 checada + `LogWin32Error` em falha
- [ ] Smoke-test local: `NanoTask.exe --smoke-test` sai com código 0
- [ ] Sem regressão de RAM (idle ≤ 6 MB Private Working Set)
- [ ] Atualizei `docs/` ou `config/settings.defaults.json` se mudei comportamento

## Evidência

<!-- Print do Gerenciador de Tarefas / log / vídeo curto -->
