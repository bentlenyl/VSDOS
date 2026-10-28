param(
    [Parameter(Mandatory = $true)]
    [string]$StageDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$stage = (Resolve-Path -LiteralPath $StageDirectory).Path
$output = [IO.Path]::GetFullPath($OutputPath)
$boot = [IO.File]::ReadAllBytes((Join-Path $stage 'boot.bin'))
$stage2 = [IO.File]::ReadAllBytes((Join-Path $stage 'stage2.bin'))
$kernel = [IO.File]::ReadAllBytes((Join-Path $stage 'kernel.bin'))

$sectorSize = 512
$totalSectors = 2880
$reservedSectors = 9
$sectorsPerFat = 9
$fatCount = 2
$rootEntryCount = 224
$rootSectors = [int][Math]::Ceiling($rootEntryCount * 32.0 / $sectorSize)
$rootStart = $reservedSectors + $fatCount * $sectorsPerFat
$dataStart = $rootStart + $rootSectors
$clusterCount = $totalSectors - $dataStart
$maximumKernelBytes = 127 * $sectorSize

if ($boot.Length -ne $sectorSize -or
    $boot[510] -ne 0x55 -or $boot[511] -ne 0xAA) {
    throw 'Boot sector must be 512 bytes and end with signature 0xAA55.'
}
if ($stage2.Length -ne 8 * $sectorSize) {
    throw 'Second-stage bootloader must occupy exactly eight sectors.'
}
if ($kernel.Length -eq 0 -or $kernel.Length -gt $maximumKernelBytes) {
    throw "Kernel must be 1-$maximumKernelBytes bytes for this BIOS loader."
}
if ($clusterCount -ge 4085) {
    throw 'Configured volume is not in the FAT12 cluster range.'
}

$root = New-Object byte[] ($rootEntryCount * 32)
$fat = New-Object byte[] ($sectorsPerFat * $sectorSize)
$image = New-Object byte[] ($totalSectors * $sectorSize)
[Array]::Copy($boot, 0, $image, 0, $boot.Length)
[Array]::Copy($stage2, 0, $image, $sectorSize, $stage2.Length)
$fat[0] = 0xF0
$fat[1] = 0xFF
$fat[2] = 0xFF
$nextFreeCluster = 2
$rootIndex = 0

function Add-FatEntry([int]$Cluster, [int]$Value) {
    $offset = $Cluster + [int][Math]::Floor($Cluster / 2)
    if (($Cluster -band 1) -eq 0) {
        $fat[$offset] = [byte]($Value -band 0xFF)
        $fat[$offset + 1] = [byte](($fat[$offset + 1] -band 0xF0) -bor (($Value -shr 8) -band 0x0F))
    } else {
        $fat[$offset] = [byte](($fat[$offset] -band 0x0F) -bor (($Value -shl 4) -band 0xF0))
        $fat[$offset + 1] = [byte](($Value -shr 4) -band 0xFF)
    }
}

function Add-RootEntry([string]$Name, [int]$Attributes, [int]$FirstCluster, [int]$Length) {
    if ($script:rootIndex -ge $rootEntryCount) {
        throw 'FAT12 root directory is full.'
    }
    $offset = $script:rootIndex * 32
    $shortName = $Name.ToUpperInvariant()
    $parts = $shortName.Split('.')
    if ($parts.Count -gt 2 -or $parts[0].Length -gt 8 -or
        ($parts.Count -eq 2 -and $parts[1].Length -gt 3)) {
        throw "Name is not a valid FAT 8.3 name: $Name"
    }
    $encodedName = $parts[0].PadRight(8) + $(if ($parts.Count -eq 2) { $parts[1].PadRight(3) } else { '   ' })
    $encoded = [Text.Encoding]::ASCII.GetBytes($encodedName)
    [Array]::Copy($encoded, 0, $root, $offset, 11)
    $root[$offset + 11] = [byte]$Attributes
    $root[$offset + 26] = [byte]($FirstCluster -band 0xFF)
    $root[$offset + 27] = [byte](($FirstCluster -shr 8) -band 0xFF)
    $root[$offset + 28] = [byte]($Length -band 0xFF)
    $root[$offset + 29] = [byte](($Length -shr 8) -band 0xFF)
    $root[$offset + 30] = [byte](($Length -shr 16) -band 0xFF)
    $root[$offset + 31] = [byte](($Length -shr 24) -band 0xFF)
    ++$script:rootIndex
}

function Add-File([string]$Name, [byte[]]$Contents) {
    $clusterCountForFile = [int][Math]::Ceiling($Contents.Length / [double]$sectorSize)
    $firstCluster = 0
    if ($clusterCountForFile -gt 0) {
        $firstCluster = $script:nextFreeCluster
        if ($firstCluster + $clusterCountForFile -gt $clusterCount + 2) {
            throw "Not enough FAT12 clusters for $Name."
        }
        for ($clusterOffset = 0; $clusterOffset -lt $clusterCountForFile; ++$clusterOffset) {
            $cluster = $firstCluster + $clusterOffset
            $next = if ($clusterOffset + 1 -eq $clusterCountForFile) { 0xFFF } else { $cluster + 1 }
            Add-FatEntry $cluster $next
            $sourceOffset = $clusterOffset * $sectorSize
            $dataLength = [Math]::Min($sectorSize, $Contents.Length - $sourceOffset)
            $destinationOffset = ($dataStart + $cluster - 2) * $sectorSize
            [Array]::Copy($Contents, $sourceOffset, $image, $destinationOffset, $dataLength)
        }
        $script:nextFreeCluster += $clusterCountForFile
    }
    Add-RootEntry $Name 0x20 $firstCluster $Contents.Length
}

Add-File 'KERNEL.BIN' $kernel

for ($copy = 0; $copy -lt $fatCount; ++$copy) {
    [Array]::Copy($fat, 0, $image, ($reservedSectors + $copy * $sectorsPerFat) * $sectorSize, $fat.Length)
}
[Array]::Copy($root, 0, $image, $rootStart * $sectorSize, $root.Length)

[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output)) | Out-Null
[IO.File]::WriteAllBytes($output, $image)
Write-Output "Created FAT12 floppy image: $output"
Write-Output 'Volume: 1.44 MB FAT12, 512-byte sectors, root file KERNEL.BIN'
Write-Output 'The volume is freshly formatted; add user files with a FAT12 image tool after building.'
