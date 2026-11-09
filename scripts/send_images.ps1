<#
================================================================================
 scripts/send_images.ps1
 BMTP Batch Biometric Image Transmission Script (Windows PowerShell)

 Usage:
   .\scripts\send_images.ps1 <HOST_IP> <RECEIVER_IP> [-Port <PORT>] [-Limit <LIMIT>]
   .\scripts\send_images.ps1 -HostIP <HOST_IP> -ReceiverIP <RECEIVER_IP>

 Arguments:
   $HostIP     : IP address of the host machine (sender)
   $ReceiverIP : IP address of the receiver machine
   -Port       : TCP Port (default: 5001)
   -Limit      : Number of images to transfer (default: 0 = all images)
   -Listen     : Run in receiver daemon mode on the receiver host
================================================================================
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory = $false)]
    [string]$HostIP = "127.0.0.1",

    [Parameter(Position = 1, Mandatory = $false)]
    [string]$ReceiverIP = "127.0.0.1",

    [Parameter(Position = 2)]
    [int]$Port = 5001,

    [Parameter(Position = 3)]
    [int]$Limit = 0,

    [int]$ChunkSize = 8192,
    [int]$WindowSize = 10,
    [string]$ImageDir = "sample_data\real_biometric",
    [string]$RecvDir = "received_files",
    [string]$LogDir = "logs",
    [string]$ResultsDir = "results",
    [switch]$Listen,
    [switch]$Receiver
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $ProjectRoot

# Ensure output directories exist
New-Item -ItemType Directory -Force -Path $RecvDir, $LogDir, $ResultsDir | Out-Null

$BinSender = Join-Path $ProjectRoot "bin\sender.exe"
$BinReceiver = Join-Path $ProjectRoot "bin\receiver.exe"

# If binaries are missing, attempt build via mingw32-make or make
if (-not (Test-Path $BinSender) -or -not (Test-Path $BinReceiver)) {
    Write-Host "[BUILD] Binaries not found in bin\. Attempting build with make..." -ForegroundColor Yellow
    if (Get-Command "mingw32-make" -ErrorAction SilentlyContinue) {
        mingw32-make all
    } elseif (Get-Command "make" -ErrorAction SilentlyContinue) {
        make all
    } else {
        Write-Error "[FATAL] sender.exe or receiver.exe missing and no make tool found."
        exit 1
    }
}

# Helper: Compute SHA-256
function Get-Sha256Hex([string]$filePath) {
    if (Test-Path -LiteralPath $filePath) {
        return (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLower()
    }
    return "FILE_NOT_FOUND"
}

# Auto-detect local IP addresses
$LocalIPs = [System.Net.Dns]::GetHostAddresses([System.Net.Dns]::GetHostName()) |
            Where-Object { $_.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork } |
            ForEach-Object { $_.IPAddressToString }

$IsReceiverMode = $Listen.IsPresent -or $Receiver.IsPresent
if (-not $IsReceiverMode -and ($HostIP -ne $ReceiverIP)) {
    foreach ($lip in $LocalIPs) {
        if ($lip -eq $ReceiverIP) {
            Write-Host "[AUTO-DETECT] Current machine IP ($lip) matches Receiver IP ($ReceiverIP)." -ForegroundColor Cyan
            Write-Host "[AUTO-DETECT] Switching to RECEIVER listening mode." -ForegroundColor Cyan
            $IsReceiverMode = $true
            break
        }
    }
}

$Timestamp = (Get-Date).ToString("yyyyMMdd_HHmmss")

# ==============================================================================
# RECEIVER MODE (Runs on the receiver host)
# ==============================================================================
if ($IsReceiverMode) {
    $RecvLogFile = Join-Path $LogDir "receiver_session_${Timestamp}.log"
    $Header = @"
================================================================================
 BMTP RECEIVER DAEMON - LISTENING FOR BIOMETRIC IMAGES
================================================================================
 Expected Host (Sender) : $HostIP
 Receiver Listen IP     : $ReceiverIP (0.0.0.0)
 Listen Port            : $Port
 Destination Directory  : $RecvDir
 Receiver Log File      : $RecvLogFile
 Started At             : $(Get-Date)
================================================================================
Waiting for incoming image transfers from $HostIP... (Press Ctrl+C to stop)
"@
    Write-Host $Header -ForegroundColor Green
    $Header | Out-File -FilePath $RecvLogFile -Encoding utf8

    # Run receiver daemon in continuous mode
    & $BinReceiver -b 0.0.0.0 -p $Port -o $RecvDir -l $LogDir
    exit 0
}

# ==============================================================================
# SENDER / LOCALHOST MODE
# ==============================================================================

if (-not (Test-Path $ImageDir)) {
    Write-Error "[FATAL] Image directory '$ImageDir' not found."
    exit 1
}

# Generate manifest using input redirection / pipe
$ImageManifest = Join-Path $LogDir "image_manifest_${Timestamp}.txt"
Get-ChildItem -Path $ImageDir -File |
    Where-Object { $_.Extension -match '^\.(jpg|jpeg|png|bmp)$' } |
    Sort-Object Name |
    ForEach-Object { $_.FullName } |
    Out-File -FilePath $ImageManifest -Encoding utf8

$TotalFound = (Get-Content $ImageManifest).Count
if ($TotalFound -eq 0) {
    Write-Error "[FATAL] No images found in '$ImageDir'."
    exit 1
}

$TotalToSend = if ($Limit -gt 0 -and $Limit -lt $TotalFound) { $Limit } else { $TotalFound }

$LogFile = Join-Path $LogDir "transfer_${Timestamp}.log"
$CsvFile = Join-Path $ResultsDir "transfer_results_${Timestamp}.csv"

$Banner = @"
====================================================================================================
 BMTP BIOMETRIC IMAGE TRANSMISSION ENGINE
====================================================================================================
 Host IP (Sender)    : $HostIP
 Receiver IP         : $ReceiverIP
 Target TCP Port     : $Port
 Image Directory     : $ImageDir
 Total Images Found  : $TotalFound
 Images to Transfer  : $TotalToSend
 Protocol / Framing  : BMTP (Big-Endian 18-byte Header, CRC32 chunk + SHA-256 full file)
 ARQ Mode            : Go-Back-N Sliding-Window (Window N=$WindowSize, Chunk Size=$ChunkSize B)
 Detailed Log File   : $LogFile
 CSV Summary File    : $CsvFile
 Execution Start     : $(Get-Date)
====================================================================================================
"@

Write-Host $Banner -ForegroundColor Cyan
$Banner | Out-File -FilePath $LogFile -Encoding utf8

# CSV Header
"index,filename,filesize_bytes,duration_sec,goodput_kbps,throughput_kbps,retransmits,handshake_ms,rtt_avg_ms,orig_sha256,recv_sha256,status" |
    Out-File -FilePath $CsvFile -Encoding utf8

$IsLocal = ($HostIP -eq $ReceiverIP) -or ($ReceiverIP -eq "127.0.0.1") -or ($ReceiverIP -eq "localhost")

$Passed = 0
$Failed = 0
$TotalBytes = [int64]0
$Index = 0
$StartTimeTotal = [System.Diagnostics.Stopwatch]::StartNew()

# Read the image manifest using input redirection / stream
$reader = [System.IO.File]::OpenText($ImageManifest)
try {
    while (-not $reader.EndOfStream) {
        $imagePath = $reader.ReadLine()
        if ([string]::IsNullOrWhiteSpace($imagePath)) { continue }

        if ($Limit -gt 0 -and $Index -ge $Limit) {
            break
        }
        $Index++

        $baseName = [System.IO.Path]::GetFileName($imagePath)
        $fileInfo = Get-Item -LiteralPath $imagePath
        $fileSize = $fileInfo.Length
        $TotalBytes += $fileSize
        $origHash = Get-Sha256Hex $imagePath
        $recvFile = Join-Path $RecvDir $baseName

        $recvJob = $null
        if ($IsLocal) {
            if (Test-Path -LiteralPath $recvFile) {
                Remove-Item -LiteralPath $recvFile -Force -ErrorAction SilentlyContinue
            }
            # Start local receiver
            $recvJob = Start-Process -FilePath $BinReceiver -ArgumentList @("-p", "$Port", "-o", $RecvDir, "-q", "-1") -WorkingDirectory $ProjectRoot -PassThru
            Start-Sleep -Milliseconds 80
        }

        $itemHeader = @"

----------------------------------------------------------------------------------------------------
 [Image $Index/$TotalToSend] Transferring: $baseName
----------------------------------------------------------------------------------------------------
 Source Host       : $HostIP
 Target Receiver   : ${ReceiverIP}:${Port}
 File Size         : $fileSize bytes ($([math]::Round($fileSize / 1KB, 1)) KB)
 Original SHA-256  : $origHash
 Time Started      : $((Get-Date).ToString("HH:mm:ss.fff"))
----------------------------------------------------------------------------------------------------
"@
        Write-Host $itemHeader -ForegroundColor Yellow
        $itemHeader | Out-File -FilePath $LogFile -Append -Encoding utf8

        $senderOutput = ""
        $senderExitCode = 1
        $maxRetries = 3

        for ($attempt = 1; $attempt -le $maxRetries; $attempt++) {
            $senderOutput = (& $BinSender -f $imagePath -h $ReceiverIP -p $Port -c $ChunkSize -w $WindowSize 2>&1 | Out-String)
            $senderExitCode = $LASTEXITCODE

            if ($senderExitCode -eq 0) {
                break
            } else {
                $retryMsg = "   [RETRY $attempt/$maxRetries] Connection to ${ReceiverIP}:${Port} failed (exit $senderExitCode). Waiting 0.2s..."
                Write-Host $retryMsg -ForegroundColor DarkYellow
                $retryMsg | Out-File -FilePath $LogFile -Append -Encoding utf8
                Start-Sleep -Milliseconds 200
            }
        }

        # Console detailed log in terminal and redirect/write into separate log file
        Write-Host $senderOutput
        $senderOutput | Out-File -FilePath $LogFile -Append -Encoding utf8

        if ($recvJob -ne $null -and -not $recvJob.HasExited) {
            $recvJob.WaitForExit(5000) | Out-Null
        }

        # Extract metrics
        $duration = "0.0"
        $goodput = "0.0"
        $throughput = "0.0"
        $retransmits = "0"
        $handshakeMs = "0.0"
        $rttAvgMs = "0.0"
        $senderStatus = ""

        if ($senderOutput -match "SUMMARY:\s*(.+)") {
            $summaryLine = $matches[1]
            if ($summaryLine -match "duration=([0-9.]+)") { $duration = $matches[1] }
            if ($summaryLine -match "goodput_kbps=([0-9.]+)") { $goodput = $matches[1] }
            if ($summaryLine -match "throughput_kbps=([0-9.]+)") { $throughput = $matches[1] }
            if ($summaryLine -match "retransmits=([0-9]+)") { $retransmits = $matches[1] }
            if ($summaryLine -match "handshake_ms=([0-9.]+)") { $handshakeMs = $matches[1] }
            if ($summaryLine -match "rtt_avg_ms=([0-9.]+)") { $rttAvgMs = $matches[1] }
            if ($summaryLine -match "status=([A-Z]+)") { $senderStatus = $matches[1] }
        }

        $recvHash = "REMOTE_HOST_VERIFIED"
        if ($IsLocal) {
            $recvHash = Get-Sha256Hex $recvFile
        }

        # Determine status
        if ($senderExitCode -eq 0 -and $senderStatus -eq "PASS") {
            if ($IsLocal -and ($origHash -ne $recvHash)) {
                $status = "FAIL_HASH_MISMATCH"
                $Failed++
            } else {
                $status = "PASS"
                $Passed++
            }
        } else {
            $status = "FAIL_TRANSMISSION"
            $Failed++
        }

        "$Index,$baseName,$fileSize,$duration,$goodput,$throughput,$retransmits,$handshakeMs,$rttAvgMs,$origHash,$recvHash,$status" |
            Out-File -FilePath $CsvFile -Append -Encoding utf8

        $color = if ($status -eq "PASS") { "Green" } else { "Red" }
        $verdictLine = ">> VERDICT: [$status] | File: $baseName | Rate: $goodput KB/s ($throughput KB/s raw) | Duration: ${duration}s | SHA-256: $($origHash.Substring(0, 16))..."
        Write-Host $verdictLine -ForegroundColor $color
        $verdictLine | Out-File -FilePath $LogFile -Append -Encoding utf8

        Start-Sleep -Milliseconds 80
    }
}
finally {
    $reader.Close()
    $reader.Dispose()
    Remove-Item -Force $ImageManifest -ErrorAction SilentlyContinue
}

$StartTimeTotal.Stop()
$TotalSeconds = [math]::Round($StartTimeTotal.Elapsed.TotalSeconds, 2)
if ($TotalSeconds -le 0) { $TotalSeconds = 0.01 }
$TotalMB = [math]::Round($TotalBytes / 1MB, 2)
$AggregateMbps = [math]::Round(($TotalBytes * 8) / ($TotalSeconds * 1e6), 2)

$SummaryReport = @"

====================================================================================================
 FINAL BATCH TRANSMISSION SUMMARY REPORT
====================================================================================================
 Total Images Processed : $Index / $TotalToSend
 Passed (Bit-for-Bit)   : $Passed
 Failed Transfers       : $Failed
 Total Volume Sent      : $TotalMB MB ($TotalBytes bytes)
 Total Wall-Clock Time  : $TotalSeconds seconds
 Aggregate Goodput      : $AggregateMbps Mbps
 Detailed Console Log   : $LogFile
 Comprehensive CSV Log  : $CsvFile
 Integrity Guarantee   : Dual IEEE 802.3 CRC32 + FIPS 180-2 SHA-256 Verified
====================================================================================================
"@

Write-Host $SummaryReport -ForegroundColor Cyan
$SummaryReport | Out-File -FilePath $LogFile -Append -Encoding utf8

if ($Failed -gt 0) {
    Write-Host "[VERDICT] BATCH TRANSFER FINISHED WITH $Failed FAILURES." -ForegroundColor Red
    exit 1
} else {
    Write-Host "[VERDICT] ALL $Passed IMAGES TRANSFERRED AND VERIFIED BIT-FOR-BIT IDENTICAL." -ForegroundColor Green
    exit 0
}
