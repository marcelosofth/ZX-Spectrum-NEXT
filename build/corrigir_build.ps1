$ErrorActionPreference = 'Stop'
$dir  = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$alvo = Join-Path $dir 'build_win.ps1'
$bak  = Join-Path $dir 'build_win.ps1.bak'
$orig = Join-Path (Split-Path $dir -Parent) '_debug\next_arquivos variados\build_win.ps1'

if ((-not (Test-Path $alvo)) -or ((Get-Item $alvo).Length -lt 500)) {
    if (Test-Path $bak)       { Copy-Item $bak  $alvo -Force }
    elseif (Test-Path $orig)  { Copy-Item $orig $alvo -Force }
    else { throw "Sem copia para restaurar." }
}
if (-not (Test-Path $bak)) { Copy-Item $alvo $bak }

$t = Get-Content $alvo -Raw -Encoding UTF8
if (-not $t) { throw "Falha ao ler build_win.ps1" }

$t = $t.Replace('$Proj = ''G:\Internet_Temp\Batocera_add\Projetos\NEXT''', '$Proj = Split-Path -Parent $PSScriptRoot')
$t = $t.Replace('$Dll  = Join-Path $Proj ''zesarux_libretro.dll''', '$Dll  = Join-Path $Proj ''dist\zesarux_libretro.dll''')
$t = $t.Replace('Join-Path $Proj ''Makefile.base''', 'Join-Path $PSScriptRoot ''Makefile.base''')
$t = $t.Replace('''..\zesarux_glue.c''', '''..\scr\zesarux_glue.c''')
$t = $t.Replace('''..\libretro.c''', '''..\scr\libretro.c''')
$t = $t.Replace('(Join-Path $Proj $n[1])', '(Join-Path $ObjDir $n[1])')
foreach ($o in 'scr_win.o','snd_win.o','glue_win.o','libretro_win.o') {
    $t = $t.Replace("(Join-Path `$Proj '$o')", "(Join-Path `$ObjDir '$o')")
}
if ($t -notmatch [regex]::Escape("'-I..\scr'")) {
    $t = $t.Replace("'-I.', '-I..',", "'-I.', '-I..', '-I..\scr',")
}
[IO.File]::WriteAllText($alvo, $t, (New-Object Text.UTF8Encoding($true)))

$raiz = Split-Path $dir -Parent
New-Item -ItemType Directory -Force (Join-Path $raiz 'obj-win'), (Join-Path $raiz 'dist') | Out-Null

Select-String $alvo -Pattern '\$Proj\s*=|\$Dll\s*=|\$ObjDir\s*=|Makefile\.base|\.\.\\scr|-I\.\.' |
    ForEach-Object { '{0}: {1}' -f $_.LineNumber, $_.Line.Trim() }