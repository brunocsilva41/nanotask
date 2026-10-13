# Contribuindo com o NanoTask

Padrão comercial: direto, revisável, sem ruído. Leia antes do primeiro PR.

## 1. Fluxo Git

- Branch principal: `main` (sempre instalável).
- Branches: `feat/<slug>`, `fix/<slug>`, `perf/<slug>`, `chore/<slug>`.
- Commits em **Conventional Commits**: `feat:`, `fix:`, `perf:`, `docs:`, `ci:`, `chore:`.
  - Ex: `fix(tray): evita double-free do HICON no NIM_MODIFY`
- Releases por tag: `git tag v1.2.0 && git push origin v1.2.0` → o workflow `release.yml` compila, gera o instalador e publica.
- Nunca commite binários (`build/`, `dist/`, `*.exe`) — o CI gera.

## 2. Padrão de código (obrigatório no review)

1. **RAII sempre**: proibido `new`/`delete`, `CloseHandle` manual, `DestroyIcon` fora de deleter.
2. **Zero silent failures**: toda Win32 checada; falha → `nt::LogWin32Error("Ctx")`.
3. **Sem bloqueio da Message Pump**: I/O e PDH em `std::jthread`; UI só no processo `NanoTask.UI`.
4. **Orçamento de recursos**: idle ≤ 6 MB Private Working Set, worker < 0,5% CPU. PR que estourar precisa justificar com medição.
5. **Segurança**: sem `system()`, sem paths concatenados com input do usuário, sem `TerminateProcess` sem confirmar PID > 4 e fora da allowlist de críticos (ver `Safety.hpp`).

## 3. Antes de abrir o PR

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
.\build\NanoTask.exe --smoke-test   # deve sair com código 0 em <15s
```

Preencha o template do PR com evidência (print do Working Set ou log).

## 4. Report de bug

Use o formulário **Issues → Report de bug** (anexe versão, modo da taskbar, `settings.json`, logs de `%LOCALAPPDATA%\NanoTask\logs`). Sem esses dados a issue volta para triagem.
