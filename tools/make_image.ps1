param(
    [Parameter(Mandatory = $true)]
    [string]$StageDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string]$ShellPath,

    [string]$AutoexecPath,

    [string]$AtaDriverPath,

    [string]$FloppyDriverPath
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

function Add-Directory([string]$Name) {
    if ($script:nextFreeCluster + 1 -gt $clusterCount + 2) {
        throw "Not enough FAT12 clusters for $Name."
    }
    $cluster = $script:nextFreeCluster
    ++$script:nextFreeCluster
    Add-FatEntry $cluster 0xFFF
    $offset = ($dataStart + $cluster - 2) * $sectorSize
    for ($index = 0; $index -lt $sectorSize; ++$index) {
        $image[$offset + $index] = 0
    }
    $dotName = '.' + (' ' * 10)
    $dotDotName = '..' + (' ' * 9)
    $dot = [Text.Encoding]::ASCII.GetBytes($dotName)
    $dotDot = [Text.Encoding]::ASCII.GetBytes($dotDotName)
    [Array]::Copy($dot, 0, $image, $offset, 11)
    $image[$offset + 11] = 0x10
    $image[$offset + 26] = [byte]($cluster -band 0xFF)
    $image[$offset + 27] = [byte](($cluster -shr 8) -band 0xFF)
    [Array]::Copy($dotDot, 0, $image, $offset + 32, 11)
    $image[$offset + 43] = 0x10
    $image[$offset + 58] = 0
    $image[$offset + 59] = 0
    Add-RootEntry $Name 0x10 $cluster 0
    return $cluster
}

$script:vsdosDirectoryIndex = 2

function Add-DirectoryFile([int]$DirectoryCluster, [string]$Name, [byte[]]$Contents) {
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

    $directoryOffset = ($dataStart + $DirectoryCluster - 2) * $sectorSize
    $entryOffset = $directoryOffset + $script:vsdosDirectoryIndex * 32
    $parts = $Name.ToUpperInvariant().Split('.')
    $encodedName = $parts[0].PadRight(8) + $(if ($parts.Count -eq 2) { $parts[1].PadRight(3) } else { '   ' })
    $encoded = [Text.Encoding]::ASCII.GetBytes($encodedName)
    [Array]::Copy($encoded, 0, $image, $entryOffset, 11)
    $image[$entryOffset + 11] = 0x20
    $image[$entryOffset + 26] = [byte]($firstCluster -band 0xFF)
    $image[$entryOffset + 27] = [byte](($firstCluster -shr 8) -band 0xFF)
    $image[$entryOffset + 28] = [byte]($Contents.Length -band 0xFF)
    $image[$entryOffset + 29] = [byte](($Contents.Length -shr 8) -band 0xFF)
    $image[$entryOffset + 30] = [byte](($Contents.Length -shr 16) -band 0xFF)
    $image[$entryOffset + 31] = [byte](($Contents.Length -shr 24) -band 0xFF)
    ++$script:vsdosDirectoryIndex
}

if ($ShellPath -and [IO.File]::Exists($ShellPath)) {
    Add-File 'COMMAND.APP' ([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $ShellPath)))
}
if ($AutoexecPath -and [IO.File]::Exists($AutoexecPath)) {
    Add-File 'AUTOEXEC.BAT' ([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $AutoexecPath)))
}
$vsdosCluster = Add-Directory 'VSDOS'
Add-DirectoryFile $vsdosCluster 'KERNEL.BIN' $kernel
if ($AtaDriverPath -and [IO.File]::Exists($AtaDriverPath)) {
    Add-DirectoryFile $vsdosCluster 'ATA.DRV' ([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $AtaDriverPath)))
}
if ($FloppyDriverPath -and [IO.File]::Exists($FloppyDriverPath)) {
    Add-DirectoryFile $vsdosCluster 'FLOPPY.DRV' ([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $FloppyDriverPath)))
}

for ($copy = 0; $copy -lt $fatCount; ++$copy) {
    [Array]::Copy($fat, 0, $image, ($reservedSectors + $copy * $sectorsPerFat) * $sectorSize, $fat.Length)
}
[Array]::Copy($root, 0, $image, $rootStart * $sectorSize, $root.Length)

[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output)) | Out-Null
[IO.File]::WriteAllBytes($output, $image)
Write-Output "Created FAT12 floppy image: $output"
Write-Output 'Volume: 1.44 MB FAT12, 512-byte sectors'
Write-Output 'The volume contains AUTOEXEC.BAT, COMMAND.APP, the VSDOS directory, and external storage drivers.'
