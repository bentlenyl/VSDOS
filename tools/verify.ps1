param(
    [Parameter(Mandatory = $true)]
    [string]$ImagePath,

    [Parameter(Mandatory = $true)]
    [string]$QemuPath
)

$ErrorActionPreference = 'Stop'
$qemu = Get-Command $QemuPath -ErrorAction Stop
$image = (Resolve-Path -LiteralPath $ImagePath).Path
$testDirectory = Split-Path -Parent $image

function Get-FreeTcpPort {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try {
        return ([Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally {
        $listener.Stop()
    }
}

function ConvertTo-QemuArgument([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Start-TestVm([string]$LogPath, [switch]$HardDisk) {
    $script:QmpReader = $null
    $script:QmpWriter = $null
    $script:QmpClient = $null
    $script:QemuProcess = $null
    $port = Get-FreeTcpPort
    $drive = if ($HardDisk) {
        "file=$image,format=raw,if=ide,index=0,media=disk"
    } else {
        "file=$image,format=raw,if=floppy,index=0"
    }
    $bootDevice = if ($HardDisk) { 'c' } else { 'a' }
    $arguments = @(
        '-machine pc'
        '-m 32M'
        ('-drive ' + (ConvertTo-QemuArgument $drive))
        ('-boot ' + $bootDevice)
        '-display none'
        ('-serial ' + (ConvertTo-QemuArgument "file:$LogPath"))
        '-monitor none'
        ('-qmp ' + (ConvertTo-QemuArgument "tcp:127.0.0.1:$port,server=on,wait=off"))
        '-no-reboot'
        '-no-shutdown'
    ) -join ' '

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $qemu.Source
    $startInfo.Arguments = $arguments
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw 'QEMU failed to start.'
    }
    $script:QemuProcess = $process

    $client = [Net.Sockets.TcpClient]::new()
    $script:QmpClient = $client
    $client.ReceiveTimeout = 5000
    $client.SendTimeout = 5000
    $connected = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if ($process.HasExited) {
            throw "QEMU exited during startup with code $($process.ExitCode)."
        }
        try {
            $client.Connect([Net.IPAddress]::Loopback, $port)
            $connected = $true
            break
        } catch [Net.Sockets.SocketException] {
            Start-Sleep -Milliseconds 100
        }
    }

    if (-not $connected) {
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        throw 'Timed out connecting to the QEMU monitor.'
    }

    $stream = $client.GetStream()
    $reader = [IO.StreamReader]::new($stream)
    $writer = [IO.StreamWriter]::new($stream)
    $writer.AutoFlush = $true
    $greeting = $reader.ReadLine()
    if (-not $greeting) {
        throw 'QEMU did not send its monitor greeting.'
    }
    $script:QmpId = 0
    $script:QmpReader = $reader
    $script:QmpWriter = $writer
    $null = Invoke-Qmp 'qmp_capabilities'
}

function Invoke-Qmp([string]$Command, [hashtable]$Arguments = @{}) {
    $script:QmpId++
    $request = @{
        execute = $Command
        id = $script:QmpId
    }
    if ($Arguments.Count -gt 0) {
        $request.arguments = $Arguments
    }
    $script:QmpWriter.WriteLine(($request | ConvertTo-Json -Compress -Depth 6))

    while ($true) {
        $line = $script:QmpReader.ReadLine()
        if (-not $line) {
            throw "QEMU monitor closed while executing $Command."
        }
        $response = $line | ConvertFrom-Json
        if ($response.id -eq $script:QmpId) {
            if ($response.error) {
                throw "QEMU monitor command failed: $($response.error.desc)"
            }
            return $response
        }
    }
}

function Send-GuestKey([string]$Key) {
    $null = Invoke-Qmp 'human-monitor-command' @{
        'command-line' = "sendkey $Key"
    }
    Start-Sleep -Milliseconds 55
}

function Send-GuestCommand([string]$Command) {
    foreach ($character in $Command.ToCharArray()) {
        switch ($character) {
            ' ' { Send-GuestKey 'spc' }
            '.' { Send-GuestKey 'dot' }
            default { Send-GuestKey ([string]$character) }
        }
    }
    Send-GuestKey 'ret'
    Start-Sleep -Milliseconds 150
}

function Stop-TestVm {
    if ($script:QmpWriter) {
        try {
            $null = Invoke-Qmp 'quit'
        } catch {
            if ($script:QemuProcess -and -not $script:QemuProcess.HasExited) {
                $script:QemuProcess.Kill()
            }
        }
    }
    if ($script:QemuProcess -and -not $script:QemuProcess.WaitForExit(5000)) {
        $script:QemuProcess.Kill()
        $script:QemuProcess.WaitForExit()
    }
    if ($script:QmpClient) {
        $script:QmpClient.Dispose()
    }
}

function Assert-KernelFileRead([string]$LogPath) {
    $encoding = [Text.Encoding]::GetEncoding(28591)
    $log = $encoding.GetString([IO.File]::ReadAllBytes($LogPath))
    $kernel = [IO.File]::ReadAllBytes((Join-Path $testDirectory 'kernel.bin'))
    $serialBytes = [IO.MemoryStream]::new()

    foreach ($value in $kernel) {
        if ($value -eq 10) {
            $serialBytes.WriteByte(13)
        }
        $serialBytes.WriteByte($value)
    }

    $command = "TYPE KERNEL.BIN`r`n"
    $commandStart = $log.LastIndexOf($command)
    if ($commandStart -lt 0) {
        throw "QEMU did not execute TYPE KERNEL.BIN in $LogPath."
    }
    $expected = $encoding.GetString($serialBytes.ToArray())
    $dataStart = $commandStart + $command.Length
    if ($log.IndexOf($expected, $dataStart) -lt 0) {
        throw "TYPE KERNEL.BIN did not return the exact on-disk kernel bytes in $LogPath."
    }
}

$shellLog = Join-Path $testDirectory 'verify-shell.log'
$panicLog = Join-Path $testDirectory 'verify-panic.log'
$hardDiskLog = Join-Path $testDirectory 'verify-hard-disk.log'
$verificationPassed = $false
foreach ($log in @($shellLog, $panicLog, $hardDiskLog)) {
    if (Test-Path -LiteralPath $log) {
        Remove-Item -LiteralPath $log
    }
}
try {
    Write-Output '[verify] Booting kernel and exercising the shell...'
    Start-TestVm $shellLog
    foreach ($command in @('help', 'dir', 'type kernel.bin', 'type readme', 'time', 'mem', 'echo verifyok')) {
        Send-GuestCommand $command
        if ($command -eq 'type kernel.bin') {
            Start-Sleep -Seconds 3
        }
    }
    Stop-TestVm

    $shellOutput = [IO.File]::ReadAllText($shellLog)
    foreach ($expected in @(
        'IDT EXCEPTIONS AND PIC INTERRUPTS READY',
        'PIT TIMER AT 100 HZ; KEYBOARD ON IRQ1',
        'FAT12 VOLUME MOUNTED FROM FLOPPY DRIVE A',
        'COMMANDS: HELP VER CLS ECHO DIR TYPE TIME MEM PANIC',
        'DIRECTORY OF A:\',
        'KERNEL.BIN',
        'VSDOS VERSION 0.1',
        'FILE NOT FOUND: README',
        'UPTIME:',
        'KERNEL IMAGE:',
        'VERIFYOK'
    )) {
        if (-not $shellOutput.Contains($expected)) {
            throw "Shell verification failed: expected output '$expected'."
        }
    }
    if ($shellOutput -notmatch 'UPTIME:\s+[0-9]+:[0-9]{2}\.[0-9] SECONDS' -or
        $shellOutput -notmatch 'KERNEL IMAGE:\s+[1-9][0-9]* BYTES') {
        throw 'Timer or kernel memory reporting returned an invalid value.'
    }
    Assert-KernelFileRead $shellLog
    Write-Output '[verify] Boot, timer, keyboard, files, and shell commands passed.'

    Write-Output '[verify] Checking CPU exception reporting...'
    Start-TestVm $panicLog
    Send-GuestCommand 'panic'
    Start-Sleep -Milliseconds 200
    Stop-TestVm

    $panicOutput = [IO.File]::ReadAllText($panicLog)
    foreach ($expected in @('KERNEL EXCEPTION', 'VECTOR: 6', 'INSTRUCTION: 0X', 'SYSTEM HALTED.')) {
        if (-not $panicOutput.Contains($expected)) {
            throw "Exception verification failed: expected output '$expected'."
        }
    }
    Write-Output '[verify] CPU exception diagnostics passed.'
    Write-Output '[verify] Checking IDE hard-disk boot and ATA filesystem access...'
    Start-TestVm $hardDiskLog -HardDisk
    foreach ($command in @('dir', 'type kernel.bin')) {
        Send-GuestCommand $command
        if ($command -eq 'type kernel.bin') {
            Start-Sleep -Seconds 3
        }
    }
    Stop-TestVm

    $hardDiskOutput = [IO.File]::ReadAllText($hardDiskLog)
    foreach ($expected in @(
        'FAT12 VOLUME MOUNTED FROM IDE HARD DISK C',
        'DIRECTORY OF C:\',
        'KERNEL.BIN'
    )) {
        if (-not $hardDiskOutput.Contains($expected)) {
            throw "Hard-disk verification failed: expected output '$expected'."
        }
        Assert-KernelFileRead $hardDiskLog
    }
    Write-Output '[verify] IDE hard-disk boot and ATA filesystem access passed.'
    $verificationPassed = $true
    Write-Output 'All VeryStupidDOS QEMU checks passed.'
} finally {
    Stop-TestVm
    if ($verificationPassed) {
        foreach ($log in @($shellLog, $panicLog, $hardDiskLog)) {
            if (Test-Path -LiteralPath $log) {
                Remove-Item -LiteralPath $log
            }
        }
    }
}
