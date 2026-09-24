# Reproducible Windows CPU inference runtime for the built-in TranslateGemma backend.
$ErrorActionPreference = "Stop"
$win = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$build = Join-Path $win "build"
$archive = Join-Path $build "llama-b11149-bin-win-cpu-x64.zip"
$unpacked = Join-Path $build "llama-unpacked"
$runtime = Join-Path $build "llama"
$url = "https://github.com/ggml-org/llama.cpp/releases/download/b11149/llama-b11149-bin-win-cpu-x64.zip"
$expected = "d1cb5f9ef7bbb7068954b4c9767d5b5309e20bcefeb61d4aafc47f9581f38752"

New-Item -ItemType Directory -Force $build, $runtime | Out-Null
if (-not (Test-Path $archive) -or (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    Invoke-WebRequest $url -OutFile $archive
}
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw "llama.cpp runtime archive failed SHA-256 verification"
}
Expand-Archive $archive -DestinationPath $unpacked -Force
Copy-Item (Join-Path $unpacked "llama-server.exe") $runtime -Force
Copy-Item (Join-Path $unpacked "*.dll") $runtime -Force
Copy-Item (Join-Path $unpacked "LICENSE-LLVM-OpenMP") $runtime -Force
$license = Join-Path $runtime "LICENSE-llama.cpp"
if (-not (Test-Path $license)) {
    Invoke-WebRequest "https://raw.githubusercontent.com/ggml-org/llama.cpp/b11149/LICENSE" -OutFile $license
}
Write-Output "Prepared bundled llama.cpp b11149 CPU runtime at $runtime"
