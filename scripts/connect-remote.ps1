#Requires -Version 5.1
<#
.SYNOPSIS Conecta o repo local NanoTask ao GitHub.
.EXAMPLE .\scripts\connect-remote.ps1 -Visibilidade public
#>
[CmdletBinding()]
param(
  [ValidateSet('public','private')][string]$Visibilidade = 'public',
  [string]$Nome = 'nanotask'
)
$ErrorActionPreference = 'Stop'
function Fail([string]$m) { Write-Host "ERRO: $m" -ForegroundColor Red; exit 1 }
function Ok([string]$m)   { Write-Host "OK: $m" -ForegroundColor Green }

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
  Fail "'gh' nao encontrado. Rode: choco install gh -y ; gh auth login"
}
gh auth status 2>$null
if ($LASTEXITCODE -ne 0) { Fail "gh sem login. Rode: gh auth login" }
Ok "gh autenticado."

if (git remote get-url origin 2>$null) { Fail "remote 'origin' ja existe. Rode: git remote -v" }
gh repo create $Nome --$Visibilidade --source=. --push
if ($LASTEXITCODE -ne 0) { Fail "gh repo create falhou." }
Ok "repo $Nome criado."

git branch -M main
if ($LASTEXITCODE -ne 0) { Fail "git branch -M main falhou." }
Ok "branch local = main (workflows ja usam main)."

git push -u origin main --tags
if ($LASTEXITCODE -ne 0) { Fail "git push falhou." }
Ok "push main + tags (v1.0.0) concluido."

$repo = (gh repo view --json nameWithOwner -q .nameWithOwner)
if ($LASTEXITCODE -ne 0) { Fail "nao foi possivel ler repo atual." }
gh workflow list --repo $repo
if ($LASTEXITCODE -ne 0) { Fail "Actions indisponiveis. Cheque Settings > Actions." }
Ok "Actions ativas em $repo. Confira em https://github.com/$repo/actions"
Write-Host "Pronto. Proteja main: Settings > Branches > Add rule (PR + ci verde)." -ForegroundColor Cyan
