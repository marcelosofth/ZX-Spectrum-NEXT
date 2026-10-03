<#
.SYNOPSIS
  Limpa a pasta zesarux-src do projeto ZEsarUX -> RetroArch (core Next).

.DESCRIPTION
  - Por padrao faz so SIMULACAO (dry-run): lista o que seria removido.
  - Com -Apply, MOVE os itens para uma pasta de quarentena
    (..\_removed_zesarux-src) preservando a estrutura. Nada e apagado.
  - Com -Apply -Delete, apaga de verdade (pede confirmacao digitada).
  - NUNCA toca em arquivos .c / .h / .m / Makefile / configure / LICENSE.
    A poda de codigo-fonte de outras maquinas deve ser feita no Makefile.base.

  Niveis:
    (padrao) Nivel 1: lixo seguro (.o, .bak, binario Linux, docs, docker, midia, TODOs, scripts .sh)
    -Roms    Nivel 2: ROMs/discos/fitas/flash de outras maquinas (mantem uma lista minima)
    -Large   Nivel 3: imagens MMC grandes (tbblue.mmc etc). Garanta que existe copia na pasta system!

.EXAMPLE
  .\limpar_zesarux_src.ps1                     # so mostra
  .\limpar_zesarux_src.ps1 -Apply              # move nivel 1 para quarentena
  .\limpar_zesarux_src.ps1 -Apply -Roms        # niveis 1 e 2
  .\limpar_zesarux_src.ps1 -Apply -Roms -Large # niveis 1, 2 e 3
#>
[CmdletBinding()]
param(
    [string]$Src = '',
    [switch]$Apply,
    [switch]$Roms,
    [switch]$Large,
    [switch]$Delete
)

$ErrorActionPreference = 'Stop'

# Pasta do script (compativel com PowerShell 5.1 / -File)
$ScriptDir = $PSScriptRoot
if (-not $ScriptDir) { $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $ScriptDir) { $ScriptDir = (Get-Location).Path }
if (-not $Src) { $Src = Join-Path $ScriptDir 'zesarux-src' }

if (-not (Test-Path -LiteralPath $Src)) { throw "Pasta nao encontrada: $Src" }
$Src  = (Resolve-Path -LiteralPath $Src).Path.TrimEnd('\')
$Quar = Join-Path (Split-Path $Src -Parent) '_removed_zesarux-src'

# ---------------------------------------------------------------- listas
# Nivel 1 - arquivos na raiz de zesarux-src (curingas permitidos)
$L1_RootFiles = @(
    'zesarux',                     # executavel Linux de 6.5 MB
    'zesarux.mp3', 'zesarux.odt', 'zesarux.pdf', 'zesarux.xcf',
    'included_utilities.odt', 'z88_shortcuts.bmp',
    'benchmark_*.txt',
    'Dockerfile*', 'docker-zesarux.sh',
    'Cambios', 'FEATURES_es',
    'TODO*',
    'CHECKLIST', 'current_checklist.txt',
    '*.sh',
    'template_nested_*.tpl',
    'prism_change_boot.txt'
)
# Nivel 1 - extensoes removidas recursivamente (qualquer subpasta)
$L1_RecurseFiles = @('*.o', '*.bak')
# Nivel 1 - pastas inteiras (nao contem codigo usado pelo core Next)
$L1_Dirs = @('macos', 'docs', 'speech_filters', 'steering_wheel_presets', 'text_image_filters')

# Nivel 2 - ROMs e midias de outras maquinas (raiz). Mantem a lista abaixo.
$L2_Patterns = @('*.rom', '*.dsk', '*.tzx', '*.tzx.config', '*.flash')
$L2_Keep     = @('tbblue_loader.rom', '48.rom', '48es.rom', '128.rom', '128s.rom',
                 'esxmmc085.rom', 'esxide085.rom')

# Nivel 3 - imagens grandes
$L3_Files = @('tbblue.mmc', 'tbblue.mmc.vazio.bak')

# ---------------------------------------------------------------- coleta
$items = New-Object System.Collections.Generic.List[object]
function Add-Item($fsItem, $level) {
    if (-not ($items | Where-Object { $_.Path -eq $fsItem.FullName })) {
        $items.Add([pscustomobject]@{ Path = $fsItem.FullName; Level = $level; IsDir = $fsItem.PSIsContainer })
    }
}

# Pastas
foreach ($d in $L1_Dirs) {
    $p = Join-Path $Src $d
    if (Test-Path -LiteralPath $p) { Add-Item (Get-Item -LiteralPath $p) 1 }
}
# Arquivos da raiz
foreach ($pat in $L1_RootFiles) {
    Get-ChildItem -LiteralPath $Src -Filter $pat -File -Force -ErrorAction SilentlyContinue |
        ForEach-Object { Add-Item $_ 1 }
}
# Recursivos
foreach ($pat in $L1_RecurseFiles) {
    Get-ChildItem -LiteralPath $Src -Filter $pat -File -Recurse -Force -ErrorAction SilentlyContinue |
        ForEach-Object { Add-Item $_ 1 }
}
# Nivel 2
if ($Roms) {
    foreach ($pat in $L2_Patterns) {
        Get-ChildItem -LiteralPath $Src -Filter $pat -File -Force -ErrorAction SilentlyContinue |
            Where-Object { $L2_Keep -notcontains $_.Name } |
            ForEach-Object { Add-Item $_ 2 }
    }
}
# Nivel 3
if ($Large) {
    foreach ($f in $L3_Files) {
        $p = Join-Path $Src $f
        if (Test-Path -LiteralPath $p) { Add-Item (Get-Item -LiteralPath $p) 3 }
    }
}

# Salvaguarda: nunca remover codigo-fonte / build / licenca
$protectedExt  = '.c', '.h', '.m', '.cpp', '.asm', '.in'
$protectedName = 'Makefile', 'Makefile.base', 'Makefile.win', 'configure', 'LICENSE', 'LICENSES_info', 'README', 'ACKNOWLEDGEMENTS'
$items = $items | Where-Object {
    $n = [IO.Path]::GetFileName($_.Path)
    $e = [IO.Path]::GetExtension($_.Path).ToLower()
    $_.IsDir -or (($protectedExt -notcontains $e) -and ($protectedName -notcontains $n))
}

# Remove arquivos que ja estao dentro de uma pasta selecionada
$dirs  = @($items | Where-Object IsDir | ForEach-Object { $_.Path.TrimEnd('\') + '\' })
$items = $items | Where-Object {
    $p = $_.Path
    $_.IsDir -or -not ($dirs | Where-Object { $p.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) })
}

# Tamanhos
foreach ($it in $items) {
    if ($it.IsDir) {
        $s = (Get-ChildItem -LiteralPath $it.Path -Recurse -File -Force -ErrorAction SilentlyContinue |
              Measure-Object Length -Sum).Sum
    } else {
        $s = (Get-Item -LiteralPath $it.Path -Force).Length
    }
    $it | Add-Member -NotePropertyName Size -NotePropertyValue ([int64]$s)
}

# ---------------------------------------------------------------- relatorio
function Fmt($b) {
    if ($b -ge 1GB) { '{0:N2} GB' -f ($b / 1GB) }
    elseif ($b -ge 1MB) { '{0:N1} MB' -f ($b / 1MB) }
    else { '{0:N0} KB' -f ($b / 1KB) }
}

Write-Host ""
Write-Host "Origem : $Src"
Write-Host ("Modo   : " + $(if ($Apply) { if ($Delete) { 'APAGAR DEFINITIVAMENTE' } else { "MOVER para quarentena ($Quar)" } } else { 'SIMULACAO (nada sera alterado)' }))
Write-Host ("Niveis : 1" + $(if ($Roms) { ', 2' }) + $(if ($Large) { ', 3' }))
Write-Host ""

$items | Sort-Object Level, Path | ForEach-Object {
    $rel  = $_.Path.Substring($Src.Length + 1)
    $tipo = if ($_.IsDir) { '[PASTA]' } else { '       ' }
    '{0}  N{1}  {2,10}  {3}' -f $tipo, $_.Level, (Fmt $_.Size), $rel
}

$total = ($items | Measure-Object Size -Sum).Sum
Write-Host ""
Write-Host ("Itens: {0}   Total: {1}" -f @($items).Count, (Fmt $total))

if (-not $Apply) {
    Write-Host ""
    Write-Host "Simulacao concluida. Rode com -Apply para executar." -ForegroundColor Yellow
    return
}

# ---------------------------------------------------------------- confirmacoes
if ($Large) {
    Write-Host ""
    Write-Host "ATENCAO: nivel 3 inclui tbblue.mmc (imagem do cartao SD)." -ForegroundColor Red
    Write-Host "Confirme que existe uma copia em system_test\zesarux ou na pasta system do RetroArch."
    if ((Read-Host "Digite SIM para continuar") -ne 'SIM') { Write-Host 'Cancelado.'; return }
}
if ($Delete) {
    Write-Host ""
    Write-Host "Isto APAGA os arquivos sem lixeira e sem quarentena." -ForegroundColor Red
    if ((Read-Host "Digite APAGAR para continuar") -ne 'APAGAR') { Write-Host 'Cancelado.'; return }
}

# ---------------------------------------------------------------- execucao
$ok = 0; $falhas = 0
foreach ($it in $items) {
    try {
        if ($Delete) {
            Remove-Item -LiteralPath $it.Path -Recurse -Force
        } else {
            $rel  = $it.Path.Substring($Src.Length + 1)
            $dest = Join-Path $Quar $rel
            $dd   = Split-Path $dest -Parent
            if (-not (Test-Path -LiteralPath $dd)) { New-Item -ItemType Directory -Path $dd -Force | Out-Null }
            if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
            Move-Item -LiteralPath $it.Path -Destination $dest -Force
        }
        $ok++
    } catch {
        $falhas++
        Write-Warning ("Falhou: {0} -> {1}" -f $it.Path, $_.Exception.Message)
    }
}

Write-Host ""
Write-Host ("Concluido: {0} itens processados, {1} falhas." -f $ok, $falhas) -ForegroundColor Green
if (-not $Delete) {
    Write-Host "Itens movidos para: $Quar"
    Write-Host "Se o build quebrar, copie de volta o que faltar. Se tudo compilar, pode apagar a quarentena."
}
