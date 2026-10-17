param(
    [string]$ToolchainDir = (Split-Path -Parent $PSScriptRoot),
    [switch]$ShaderOnly
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path (Join-Path $ToolchainDir 'clang.exe'))) {
    $ToolchainDir = Join-Path $ToolchainDir 'bin'
}
$Clang = Join-Path $ToolchainDir 'clang.exe'
$Plugin = Join-Path $ToolchainDir 'ClangEnzyme-21.dll'
$Bin = Join-Path $PSScriptRoot 'bin'
$Include = Join-Path $PSScriptRoot 'include'
$Examples = Join-Path $PSScriptRoot 'examples'
$Out = Join-Path $PSScriptRoot 'out'
$env:PATH = "$Bin;$ToolchainDir;$env:PATH"
New-Item -ItemType Directory -Force $Out | Out-Null
function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}
function Clean-Spirv {
    param([string]$Source, [string]$Destination)
    $Optimized = "$Destination.optimized"
    Invoke-Checked -Exe (Join-Path $Bin 'spirv-opt.exe') -Arguments @(
        '--eliminate-dead-code-aggressive', '--eliminate-dead-const',
        '--trim-capabilities', $Source, '-o', $Optimized)
    # LLVM 21 can leave an unused Int8 capability after lowering buffer names.
    # Remove it only when no 8-bit integer type remains; validate afterwards.
    $Bytes = [IO.File]::ReadAllBytes($Optimized)
    if (($Bytes.Length % 4) -ne 0 -or [BitConverter]::ToUInt32($Bytes, 0) -ne 0x07230203) {
        throw 'Unexpected SPIR-V binary format.'
    }
    $HasInt8Type = $false
    for ($Offset = 20; $Offset -lt $Bytes.Length;) {
        $Word = [BitConverter]::ToUInt32($Bytes, $Offset)
        $Count = [int]($Word -shr 16)
        $Opcode = $Word -band 65535
        if ($Count -lt 1 -or $Offset + 4 * $Count -gt $Bytes.Length) { throw 'Invalid SPIR-V instruction.' }
        if ($Opcode -eq 21 -and $Count -eq 4 -and [BitConverter]::ToUInt32($Bytes, $Offset + 8) -eq 8) {
            $HasInt8Type = $true
        }
        $Offset += 4 * $Count
    }
    $Stream = New-Object IO.MemoryStream
    try {
        $Stream.Write($Bytes, 0, 20)
        for ($Offset = 20; $Offset -lt $Bytes.Length;) {
            $Word = [BitConverter]::ToUInt32($Bytes, $Offset)
            $Count = [int]($Word -shr 16)
            $Opcode = $Word -band 65535
            $Remove = (-not $HasInt8Type) -and $Opcode -eq 17 -and $Count -eq 2 -and
                [BitConverter]::ToUInt32($Bytes, $Offset + 4) -eq 39
            if (-not $Remove) { $Stream.Write($Bytes, $Offset, 4 * $Count) }
            $Offset += 4 * $Count
        }
        [IO.File]::WriteAllBytes($Destination, $Stream.ToArray())
    } finally { $Stream.Dispose() }
    Remove-Item $Optimized
    Invoke-Checked -Exe (Join-Path $Bin 'spirv-val.exe') -Arguments @('--target-env', 'vulkan1.3', $Destination)
    Invoke-Checked -Exe (Join-Path $Bin 'spirv-dis.exe') -Arguments @($Destination, '-o', "$Destination.txt")
}
if (-not (Test-Path $Clang)) { throw 'Pass -ToolchainDir pointing to the existing ClangEnzyme-21 compiler directory.' }
Invoke-Checked -Exe $Clang -Arguments @('--version')
$Warnings = @('-Wno-invalid-constexpr', '-Wno-header-guard', '-Wno-c++14-extensions')
$SquareRaw = Join-Path $Out 'square.raw.spv'
Invoke-Checked -Exe $Clang -Arguments (@(
    '--driver-mode=dxc', '-T', 'cs_6_0', '-E', 'main', '-spirv',
    '-fspv-target-env=vulkan1.3', '-fspv-extension=SPV_KHR_no_integer_wrap_decoration',
    '-I', $Include, (Join-Path $Examples 'square.hlsl'), '-Fo', $SquareRaw) + $Warnings)
Clean-Spirv -Source $SquareRaw -Destination (Join-Path $Out 'square.spv')
Write-Host 'PASS: buffer shader compiled and validated for Vulkan 1.3.'
if ($ShaderOnly) { return }
if (-not (Test-Path $Plugin)) { throw 'ClangEnzyme-21.dll is required for the gradient examples.' }
foreach ($Example in @('scalar', 'tasks')) {
    $Gradient = Join-Path $Out "$Example.gradient.ll"
    $Wrapper = Join-Path $Out "$Example.wrapper.ll"
    $Linked = Join-Path $Out "$Example.linked.bc"
    $Ready = Join-Path $Out "$Example.ready.bc"
    $Raw = Join-Path $Out "$Example.raw.spv"
    $Final = Join-Path $Out "$Example.spv"
    # Load ClangEnzyme in its matching clang host. Loading it in opt.exe can
    # register LLVM command-line options twice in this Windows distribution.
    Invoke-Checked -Exe $Clang -Arguments @(
        '-cc1', '-triple', 'spirv-unknown-vulkan1.3-compute', '-x', 'c++', '-O1',
        "-fpass-plugin=$Plugin", '-emit-llvm',
        (Join-Path $Examples "$Example.cpp"), '-o', $Gradient)
    if ((Get-Content -Raw $Gradient) -match '\bcall\b[^\r\n]*__enzyme_autodiff') {
        throw "Enzyme call remains in $Gradient"
    }
    Invoke-Checked -Exe $Clang -Arguments (@(
        '-x', 'hlsl', '--target=spirv-unknown-vulkan1.3-compute',
        '-Xclang', '-hlsl-entry', '-Xclang', 'main', '-O1',
        '-I', $Include, '-S', '-emit-llvm',
        (Join-Path $Examples "$Example.hlsl"), '-o', $Wrapper) + $Warnings)
    Invoke-Checked -Exe (Join-Path $Bin 'llvm-link.exe') -Arguments @($Wrapper, $Gradient, '-o', $Linked)
    Invoke-Checked -Exe (Join-Path $Bin 'opt.exe') -Arguments @(
        '-internalize-public-api-list=main', '-passes=internalize,default<O2>', $Linked, '-o', $Ready)
    Invoke-Checked -Exe (Join-Path $Bin 'llc.exe') -Arguments @(
        '-mtriple=spirv-unknown-vulkan1.3-compute', '-O0', '-filetype=obj',
        '-spirv-ext=+SPV_KHR_no_integer_wrap_decoration', $Ready, '-o', $Raw)
    Clean-Spirv -Source $Raw -Destination $Final
    Write-Host "PASS: $Example C++ gradient compiled and validated for Vulkan 1.3."
}
Write-Host "Outputs: $Out"
