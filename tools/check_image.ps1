param(
    [Parameter(Mandatory = $true)]
    [string]$StageDirectory,

    [Parameter(Mandatory = $true)]
    [string]$ImagePath
)

$ErrorActionPreference = 'Stop'
$stage = (Resolve-Path -LiteralPath $StageDirectory).Path
$image = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $ImagePath).Path)
$boot = [IO.File]::ReadAllBytes((Join-Path $stage 'boot.bin'))
$stage2 = [IO.File]::ReadAllBytes((Join-Path $stage 'stage2.bin'))
$kernel = [IO.File]::ReadAllBytes((Join-Path $stage 'kernel.bin'))

function Read-U16([byte[]]$Bytes, [int]$Offset) {
    return [int]$Bytes[$Offset] -bor ([int]$Bytes[$Offset + 1] -shl 8)
}

function Read-U32([byte[]]$Bytes, [int]$Offset) {
    return [long]$Bytes[$Offset] +
        ([long]$Bytes[$Offset + 1] -shl 8) +
        ([long]$Bytes[$Offset + 2] -shl 16) +
        ([long]$Bytes[$Offset + 3] -shl 24)
}

function Get-Fat12Entry([byte[]]$Fat, [int]$Cluster) {
    $offset = $Cluster + [int][Math]::Floor($Cluster / 2)
    $value = [int]$Fat[$offset] -bor ([int]$Fat[$offset + 1] -shl 8)
    if (($Cluster -band 1) -ne 0) {
        $value = $value -shr 4
    }
    return $value -band 0x0FFF
}

function Assert-FileInRoot([byte[]]$Data, [string]$ShortName) {
    $encodedName = $ShortName.ToUpperInvariant()
    $parts = $encodedName.Split('.')
    $name = $parts[0].PadRight(8) +
        $(if ($parts.Count -eq 2) { $parts[1].PadRight(3) } else { '   ' })
    $encoded = [Text.Encoding]::ASCII.GetBytes($name)

    for ($entry = 0; $entry -lt $rootEntries; ++$entry) {
        $offset = $rootOffset + $entry * 32
        if ($image[$offset] -eq 0) {
            break
        }
        $matches = $true
        for ($character = 0; $character -lt 11; ++$character) {
            if ($image[$offset + $character] -ne $encoded[$character]) {
                $matches = $false
                break
            }
        }
        if ($matches) {
            if ((Read-U32 $image ($offset + 28)) -ne $Data.Length) {
                throw "Unexpected on-disk size for $ShortName."
            }
            $cluster = Read-U16 $image ($offset + 26)
            $actual = New-Object byte[] $Data.Length
            $copied = 0
            $steps = 0
            while ($copied -lt $Data.Length) {
                if ($cluster -lt 2 -or $cluster -ge $clusterCount + 2 -or
                    ++$steps -gt $clusterCount) {
                    throw "Invalid FAT12 chain for $ShortName."
                }
                $sector = $dataStart + $cluster - 2
                $source = $sector * $sectorSize
                $amount = [Math]::Min($sectorSize, $Data.Length - $copied)
                [Array]::Copy($image, $source, $actual, $copied, $amount)
                $copied += $amount
                if ($copied -lt $Data.Length) {
                    $cluster = Get-Fat12Entry $fat $cluster
                }
            }
            if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$Data, [byte[]]$actual)) {
                throw "On-disk contents do not match $ShortName."
            }
            return
        }
    }
    throw "Required file missing from FAT12 root directory: $ShortName."
}

$sectorSize = Read-U16 $image 11
$sectorsPerCluster = $image[13]
$reservedSectors = Read-U16 $image 14
$fatCount = $image[16]
$rootEntries = Read-U16 $image 17
$totalSectors = Read-U16 $image 19
$sectorsPerFat = Read-U16 $image 22
$rootSectors = [int][Math]::Ceiling($rootEntries * 32.0 / $sectorSize)
$rootOffset = ($reservedSectors + $fatCount * $sectorsPerFat) * $sectorSize
$dataStart = $reservedSectors + $fatCount * $sectorsPerFat + $rootSectors
$clusterCount = $totalSectors - $dataStart

if ($image.Length -ne 1474560 -or $boot.Length -ne 512 -or
    $stage2.Length -ne 4096 -or $boot[510] -ne 0x55 -or
    $boot[511] -ne 0xAA) {
    throw 'Invalid floppy image size, boot sector, or second-stage loader.'
}
if ($sectorSize -ne 512 -or $sectorsPerCluster -ne 1 -or
    $reservedSectors -ne 9 -or $fatCount -ne 2 -or
    $rootEntries -ne 224 -or $totalSectors -ne 2880 -or
    $sectorsPerFat -ne 9 -or $clusterCount -ge 4085) {
    throw 'Disk BPB does not describe the expected standard FAT12 floppy.'
}
if ($image[21] -ne 0xF0) {
    throw 'FAT12 media descriptor is invalid.'
}
for ($index = 0; $index -lt 512; ++$index) {
    if ($image[$index] -ne $boot[$index]) {
        throw 'Boot sector does not match the assembled BIOS loader.'
    }
}
for ($index = 0; $index -lt $stage2.Length; ++$index) {
    if ($image[512 + $index] -ne $stage2[$index]) {
        throw 'Reserved FAT sectors do not contain the assembled second-stage loader.'
    }
}

$fat = New-Object byte[] ($sectorsPerFat * $sectorSize)
[Array]::Copy($image, $reservedSectors * $sectorSize, $fat, 0, $fat.Length)
if ($fat[0] -ne 0xF0 -or $fat[1] -ne 0xFF -or $fat[2] -ne 0xFF) {
    throw 'FAT12 reserved entries do not match the media descriptor.'
}
$secondFatOffset = ($reservedSectors + $sectorsPerFat) * $sectorSize
for ($index = 0; $index -lt $fat.Length; ++$index) {
    if ($image[$secondFatOffset + $index] -ne $fat[$index]) {
        throw 'The two FAT copies do not match.'
    }
}
Assert-FileInRoot $kernel 'KERNEL.BIN'
for ($entry = 1; $entry -lt $rootEntries; ++$entry) {
    if ($image[$rootOffset + $entry * 32] -ne 0) {
        throw 'New FAT12 volumes must contain only KERNEL.BIN; unexpected preloaded files found.'
    }
}

Write-Output "FAT12 image check passed: standard BPB, BIOS loaders, $clusterCount data clusters, and KERNEL.BIN."
