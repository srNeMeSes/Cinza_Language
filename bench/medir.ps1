# Benchmark: melhor de 5 execuções de cada programa de bench/ (em ms), no
# interpretador e na CVM. Programa que a CVM ainda não compila aparece como "-".
# Uso (na raiz do projeto): powershell -File bench/medir.ps1
function Melhor($programa, $opcoes) {
    $tempos = @()
    $saida = ""
    for ($i = 0; $i -lt 5; $i++) {
        $t = Measure-Command { $script:saida = & .\cinza.exe @opcoes $programa 2>$null }
        if ($LASTEXITCODE -ne 0) { return @("-", "") }
        $tempos += $t.TotalMilliseconds
    }
    return @(("{0:N0} ms" -f ($tempos | Measure-Object -Minimum).Minimum), ($script:saida -join " "))
}

"{0,-16} {1,12} {2,12}   {3}" -f "programa", "interpretador", "cvm", "saida"
foreach ($p in Get-ChildItem bench\*.cinza | Sort-Object Name) {
    $interp = Melhor $p.FullName @()
    $cvm    = Melhor $p.FullName @("--cvm")
    "{0,-16} {1,12} {2,12}   {3}" -f $p.Name, $interp[0], $cvm[0], $interp[1]
}
