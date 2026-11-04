param(
    [double]$Percent = [double]::NaN,
    [string]$Port = "COM12"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$sketch = Join-Path $root "GestureFlightBridge"
$config = Join-Path $sketch "flight_config.h"
$cli = "D:\mixly\arduino-cli\arduino-cli.exe"
$cliConfig = "D:\mixly\arduino-cli\arduino-cli.yaml"
$fqbn = "esp32:esp32:esp32c3:CDCOnBoot=cdc"

if ([double]::IsNaN($Percent)) {
    $text = Read-Host "Throttle percent for TAKEOFF and HOLD (example: 29.5)"
    if (-not [double]::TryParse($text, [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$Percent)) {
        throw "Invalid number: $text"
    }
}
if ($Percent -lt 0 -or $Percent -gt 60) {
    throw "Throttle must be between 0 and 60 percent."
}
if (-not (Test-Path -LiteralPath $cli)) { throw "arduino-cli not found: $cli" }
if (-not (Test-Path -LiteralPath $cliConfig)) { throw "arduino-cli config not found: $cliConfig" }
if (-not (Test-Path -LiteralPath $config)) { throw "flight_config.h not found: $config" }

$value = ($Percent / 100.0).ToString("0.0000", [Globalization.CultureInfo]::InvariantCulture) + "f"
$source = Get-Content -LiteralPath $config -Raw
$takeoffPattern = '(?m)^#define\s+FLIGHT_TAKEOFF_THROTTLE\s+\S+.*$'
$hoverPattern = '(?m)^#define\s+FLIGHT_HOVER_THROTTLE\s+\S+.*$'
if ([regex]::Matches($source, $takeoffPattern).Count -ne 1 -or
    [regex]::Matches($source, $hoverPattern).Count -ne 1) {
    throw "Throttle definitions were not found exactly once."
}
$source = [regex]::Replace($source, $takeoffPattern, "#define FLIGHT_TAKEOFF_THROTTLE $value")
$source = [regex]::Replace($source, $hoverPattern, "#define FLIGHT_HOVER_THROTTLE   $value")
[IO.File]::WriteAllText($config, $source, [Text.UTF8Encoding]::new($false))

Write-Host "TAKEOFF/HOLD throttle set to $Percent% ($value). LAND remains unchanged."
Write-Host "Compiling and uploading to $Port..."
& $cli --config-file $cliConfig compile --upload --port $Port --fqbn $fqbn $sketch
if ($LASTEXITCODE -ne 0) {
    throw "Compile/upload failed. If $Port is busy, close its serial monitor and retry."
}
Write-Host "DONE: firmware uploaded to $Port."

