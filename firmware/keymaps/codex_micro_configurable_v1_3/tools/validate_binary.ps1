param(
    [Parameter(Mandatory = $false)]
    [string]$Binary = "doio_kb16_rev2_codex_micro_compat_v1_2.bin"
)

$resolved = Resolve-Path -LiteralPath $Binary -ErrorAction Stop
$bytes = [IO.File]::ReadAllBytes($resolved)

function Find-BytePattern {
    param([byte[]]$Pattern)

    $hits = @()
    for ($offset = 0; $offset -le $bytes.Length - $Pattern.Length; $offset++) {
        $match = $true
        for ($index = 0; $index -lt $Pattern.Length; $index++) {
            if ($bytes[$offset + $index] -ne $Pattern[$index]) {
                $match = $false
                break
            }
        }
        if ($match) {
            $hits += $offset
        }
    }
    return $hits
}

$patterns = [ordered]@{
    HidReport = [byte[]](
        0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x06,
        0x09, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x95, 0x3F,
        0x75, 0x08, 0x81, 0x02, 0x09, 0x02, 0x15, 0x00, 0x26,
        0xFF, 0x00, 0x95, 0x3F, 0x75, 0x08, 0x91, 0x02, 0xC0
    )
    UsbIdentity = [byte[]](0x3A, 0x30, 0x60, 0x83, 0x00, 0x01)
    RawInterface = [byte[]](0x09, 0x04, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00)
    RawIn64 = [byte[]](0x07, 0x05, 0x82, 0x03, 0x40, 0x00)
    RawOut64 = [byte[]](0x07, 0x05, 0x03, 0x03, 0x40, 0x00)
    Manufacturer = [Text.Encoding]::Unicode.GetBytes("Work Louder")
    Product = [Text.Encoding]::Unicode.GetBytes("Codex Micro")
}

if ($bytes.Length -gt 131072) {
    throw "Firmware exceeds 128 KiB: $($bytes.Length) bytes"
}

foreach ($entry in $patterns.GetEnumerator()) {
    $hits = @(Find-BytePattern -Pattern $entry.Value)
    if ($hits.Count -ne 1) {
        throw "$($entry.Key): expected exactly one match, found $($hits.Count)"
    }
    Write-Output "$($entry.Key): offset $($hits[0])"
}

$hash = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash
Write-Output "Size: $($bytes.Length) bytes"
Write-Output "SHA-256: $hash"
Write-Output "Codex Micro binary descriptor validation passed"
