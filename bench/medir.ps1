# Benchmark: melhor de 5 execuções de cada programa de bench/ (em ms) na CVM e no código
# nativo (cinzac -O2, se llvm/bin/cinzac.exe existir: make cinzac). A saída dos dois tem de
# ser a mesma; programa que falha aparece como "-".
# Uso (na raiz do projeto): powershell -File bench/medir.ps1
$cinzac = ".\llvm\bin\cinzac.exe"
$pasta  = Join-Path $env:TEMP "cinza_bench"
New-Item -ItemType Directory -Force $pasta | Out-Null

function Melhor($comando, $argumento) {
    $tempos = @()
    $saida = ""
    for ($i = 0; $i -lt 5; $i++) {
        $t = Measure-Command { $script:saida = & $comando $argumento 2>$null }
        if ($LASTEXITCODE -ne 0) { return @("-", "", 0) }
        $tempos += $t.TotalMilliseconds
    }
    $m = ($tempos | Measure-Object -Minimum).Minimum
    return @(("{0:N0} ms" -f $m), ($script:saida -join " "), $m)
}

"{0,-16} {1,10} {2,10} {3,8}   {4}" -f "programa", "cvm", "nativo", "ganho", "saida"
foreach ($p in Get-ChildItem bench\*.cinza | Sort-Object Name) {
    $cvm = Melhor ".\cinza.exe" $p.FullName
    $nat = @("-", "", 0)
    if (Test-Path $cinzac) {
        $exe = Join-Path $pasta ($p.BaseName + ".exe")
        & $cinzac -O2 $p.FullName -o $exe 2>$null
        if ($LASTEXITCODE -eq 0) { $nat = Melhor $exe "" }
    }
    $razao = if ($nat[2] -gt 0) { "{0:N1}x" -f ($cvm[2] / $nat[2]) } else { "-" }
    $saida = $cvm[1]
    if ($nat[2] -gt 0 -and $nat[1] -ne $cvm[1]) { $saida = "DIFERE: cvm [" + $cvm[1] + "] nativo [" + $nat[1] + "]" }
    "{0,-16} {1,10} {2,10} {3,8}   {4}" -f $p.Name, $cvm[0], $nat[0], $razao, $saida
}
