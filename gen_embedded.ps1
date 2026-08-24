# gen_embedded.ps1 - 将二进制文件转为 C++ 头文件
# 用法: powershell -File gen_embedded.ps1 -OutPath "embedded_dll.h" -VarName "embedded_dll_data" -DataFile "MeowPAPI.dll"
param(
    [Parameter(Mandatory=$true)][string]$OutPath,
    [Parameter(Mandatory=$true)][string]$VarName,
    [Parameter(Mandatory=$true)][string]$DataFile
)

$bytes = [System.IO.File]::ReadAllBytes($DataFile)
$sb = [System.Text.StringBuilder]::new()

[void]$sb.AppendLine("// AUTO-GENERATED - DO NOT EDIT")
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("#include <array>")
[void]$sb.AppendLine("#include <cstdint>")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("constexpr std::array<uint8_t, $($bytes.Length)> $VarName{")

$chunkSize = 4096
for ($i = 0; $i -lt $bytes.Length; $i += $chunkSize) {
    $end = [Math]::Min($i + $chunkSize, $bytes.Length)
    $parts = @()
    for ($j = $i; $j -lt $end; $j++) {
        $parts += "0x{0:X2}" -f $bytes[$j]
    }
    [void]$sb.Append($parts -join ",")
    if ($end -lt $bytes.Length) { [void]$sb.Append(",") }
    [void]$sb.AppendLine("")
}

[void]$sb.AppendLine("};")
[System.IO.File]::WriteAllText($OutPath, $sb.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Host "[gen_embedded] Wrote $OutPath ($($bytes.Length) bytes -> $($sb.Length) chars)"
