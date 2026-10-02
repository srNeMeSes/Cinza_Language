# Benchmark B2: melhor de 5 execuções de cada programa de bench/ (em ms).
# Uso (na raiz do projeto): powershell -File bench/medir.ps1
$programas = Get-ChildItem bench\*.cinza | Sort-Object Name
foreach ($p in $programas) {
    $tempos = @()
    $saida = ""
    for ($i = 0; $i -lt 5; $i++) {
        $t = Measure-Command { $script:saida = & .\cinza.exe $p.FullName }
        $tempos += $t.TotalMilliseconds
    }
    $melhor = ($tempos | Measure-Object -Minimum).Minimum
    "{0,-16} {1,8:N0} ms   (saida: {2})" -f $p.Name, $melhor, ($saida -join " ")
}
