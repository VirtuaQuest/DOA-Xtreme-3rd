# What the setup window does, without the window: checks and installs the build tools, works out what's in a folder of
# game files, extracts them, recompiles the game's code and compiles the result. Setup.ps1 runs these on a background
# thread. Messages go to $Shared.Queue as "KIND|text"; the window reads them. Key values are never printed or logged.

$script:MsysRoot = 'C:\msys64'   # (the Makefile's instructions and the launcher expect it here)
$script:GameIdPrefix = '01009cc00c97'
$script:Packages = @('make', 'mingw-w64-ucrt-x86_64-binutils', 'mingw-w64-ucrt-x86_64-clang', 'mingw-w64-ucrt-x86_64-lld',
    'mingw-w64-ucrt-x86_64-sdl3', 'mingw-w64-ucrt-x86_64-ffmpeg', 'mingw-w64-ucrt-x86_64-shaderc',
    'mingw-w64-ucrt-x86_64-vulkan-headers', 'mingw-w64-ucrt-x86_64-vulkan-loader',
    'mingw-w64-ucrt-x86_64-vulkan-memory-allocator')
$script:PythonPins = @('unicorn==2.1.4', 'capstone==5.0.9', 'lz4==4.4.5', 'pycryptodome==3.23.0')
$script:FreeGbToExtract = 20    # (about 11 GB of game files with all DLC, plus the build)
$script:FreeGbToBuild = 3

function Send-Message([string]$Kind, [string]$Text) {
    if ($Shared.OnMessage) { & $Shared.OnMessage $Kind $Text }  # (Setup.ps1 -Prepare: progress lines instead of a window)
    else { $Shared.Queue.Enqueue("$Kind|$Text") }
    if ($Kind -eq 'LOG') {
        try { [IO.File]::AppendAllText((Join-Path $Root 'setup.log'), "$Text`r`n") } catch { }
    }
}

function Write-Step([string]$Text) { Send-Message 'LOG' $Text }

function Get-QuotedArgs([string[]]$Items) {
    ($Items | ForEach-Object { if ($_ -match '[\s&()^]' -or $_ -eq '') { '"' + $_ + '"' } else { $_ } }) -join ' '
}

function Get-BuildEnv {
    @{ PATH = "$MsysRoot\ucrt64\bin;$MsysRoot\usr\bin;$env:PATH" }
}

# Runs a program, its output and errors line by line through OnLine (default: the log). Returns its exit code.
function Invoke-Tool([string]$Exe, [string[]]$Arguments, [hashtable]$Environment = @{}, [scriptblock]$OnLine = $null) {
    if ($Shared.Cancel) { throw 'Cancelled.' }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = "$env:WINDIR\System32\cmd.exe"
    $info.Arguments = '/d /s /c ""' + $Exe + '" ' + (Get-QuotedArgs $Arguments) + ' 2>&1"'
    $info.WorkingDirectory = $Root
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.CreateNoWindow = $true
    $info.StandardOutputEncoding = [Text.Encoding]::UTF8
    foreach ($k in $Environment.Keys) { $info.EnvironmentVariables[$k] = $Environment[$k] }
    $p = [Diagnostics.Process]::Start($info)
    $Shared.ChildPid = $p.Id
    while ($null -ne ($line = $p.StandardOutput.ReadLine())) {
        if ($OnLine) { & $OnLine $line } else { Write-Step $line }
    }
    $p.WaitForExit()
    $Shared.ChildPid = 0
    if ($Shared.Cancel) { throw 'Cancelled.' }
    return $p.ExitCode
}

function Find-Python {
    $found = @()
    if (Get-Command py.exe -ErrorAction SilentlyContinue) {
        $found += (& py.exe -V:3.12 -c 'import sys; print(sys.executable)' 2>$null)
    }
    $found += "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe", 'C:\Python312\python.exe'
    foreach ($p in $found) { if ($p -and (Test-Path $p)) { return $p } }
    return $null
}

function Get-VenvPython { Join-Path $Root '.venv\Scripts\python.exe' }

function Get-ToolStatus {
    $pacman = "$MsysRoot\usr\bin\pacman.exe"
    $missing = @()
    if (Test-Path $pacman) {
        $out = & cmd.exe /d /c ('"' + $pacman + '" -Q ' + ($Packages -join ' ') + ' 2>&1')
        foreach ($line in $out) {
            if ($line -match "package '(.+)' was not found") { $missing += $Matches[1] }
        }
    }
    $python = Find-Python
    $venv = $false
    $vp = Get-VenvPython
    if (Test-Path $vp) {
        & $vp -I -c 'import unicorn, capstone, lz4, Crypto' 2>$null
        $venv = ($LASTEXITCODE -eq 0)
    }
    [pscustomobject]@{
        Msys     = (Test-Path $pacman)
        Missing  = $missing
        Python   = $python
        Venv     = $venv
        Ready    = ((Test-Path $pacman) -and $missing.Count -eq 0 -and $venv)
        HasWinget = [bool](Get-Command winget.exe -ErrorAction SilentlyContinue)
    }
}

function Install-Tools {
    $status = Get-ToolStatus
    if (-not $status.Msys -or -not $status.Python) {
        if (-not $status.HasWinget) {
            throw 'winget is not available on this PC. Install MSYS2 (https://www.msys2.org, to C:\msys64) and Python 3.12 (https://www.python.org), then press this button again.'
        }
    }
    $wingetArgs = @('-e', '--accept-source-agreements', '--accept-package-agreements', '--silent')
    if (-not $status.Msys) {
        Write-Step 'Installing MSYS2 (compilers and libraries)...'
        $null = Invoke-Tool 'winget.exe' (@('install', '--id', 'MSYS2.MSYS2') + $wingetArgs)
        if (-not (Test-Path "$MsysRoot\usr\bin\pacman.exe")) { throw "MSYS2 wasn't installed to $MsysRoot." }
        $status = Get-ToolStatus
    }
    if ($status.Missing.Count -gt 0) {
        $bash = "$MsysRoot\usr\bin\bash.exe"
        $bashEnv = @{ MSYSTEM = 'UCRT64'; CHERE_INVOKING = '1' }
        Write-Step 'Updating MSYS2...'
        # (twice: a core update ends the first pass early. A partial update can break MSYS2, so packages come after)
        $null = Invoke-Tool $bash @('-lc', 'pacman -Syuu --noconfirm') $bashEnv
        $null = Invoke-Tool $bash @('-lc', 'pacman -Syuu --noconfirm') $bashEnv
        Write-Step 'Installing the compilers and libraries...'
        $code = Invoke-Tool $bash @('-lc', ('pacman -S --needed --noconfirm ' + ($Packages -join ' '))) $bashEnv
        if ($code -ne 0) { throw "pacman failed (exit code $code). See the log." }
    }
    if (-not $status.Python) {
        Write-Step 'Installing Python 3.12...'
        $null = Invoke-Tool 'winget.exe' (@('install', '--id', 'Python.Python.3.12', '--scope', 'user') + $wingetArgs)
    }
    $python = Find-Python
    if (-not $python) { throw "Python 3.12 wasn't found after installing it." }
    $vp = Get-VenvPython
    if (-not (Test-Path $vp)) {
        Write-Step 'Creating the Python environment...'
        $code = Invoke-Tool $python @('-m', 'venv', (Join-Path $Root '.venv'))
        if ($code -ne 0) { throw "Creating .venv failed (exit code $code)." }
    }
    Write-Step 'Installing the Python packages...'
    $code = Invoke-Tool $vp (@('-m', 'pip', 'install', '--disable-pip-version-check') + $PythonPins)
    if ($code -ne 0) { throw "pip failed (exit code $code). See the log." }
    $final = Get-ToolStatus
    if (-not $final.Ready) { throw 'Some build tools are still missing. See the log.' }
    Write-Step 'Build tools are ready.'
}

# What's in the folder, from each file's own headers (names can say anything). Returns base, update, DLC list, problems.
function Get-GameFiles([string]$Folder, [string]$KeysFile) {
    if (-not (Test-Path -LiteralPath $KeysFile -PathType Leaf)) { throw "Keys file not found: $KeysFile" }
    if (-not (Select-String -LiteralPath $KeysFile -Pattern '^\s*header_key\s*=' -Quiet)) {
        throw "That keys file doesn't have a header_key line. It should be the prod.keys dumped from your console."
    }
    if (Test-Path -LiteralPath $Folder -PathType Leaf) { $files = @(Get-Item -LiteralPath $Folder) }
    else { $files = @(Get-ChildItem -LiteralPath $Folder -Recurse -File -Filter '*.nsp' -ErrorAction SilentlyContinue | Sort-Object FullName) }
    if ($files.Count -eq 0) { throw 'No .nsp files in that folder.' }
    $vp = Get-VenvPython
    $script_ = Join-Path $Root 'tools\nsp_extract.py'
    $result = [ordered]@{ Base = $null; Update = $null; UpdateVersion = -1; Dlc = @(); Duplicates = 0; Other = @(); Unreadable = @() }
    $seen = @{}
    $i = 0
    foreach ($f in $files) {
        $i++
        Send-Message 'SCANNING' "$i of $($files.Count)"
        $lines = New-Object Collections.ArrayList
        $code = Invoke-Tool $vp @('-I', '-u', $script_, $f.FullName, $KeysFile, (Join-Path $env:TEMP 'doax3-scan'), '--list') @{} { param($l) [void]$lines.Add($l) }
        if ($code -ne 0) {
            $reason = ($lines | Where-Object { $_ -match 'Error|personalized' } | Select-Object -Last 1)
            Write-Step "Can't read $($f.Name): $reason"
            $result.Unreadable += $f.Name
            continue
        }
        $text = $lines -join "`n"
        $m = [regex]::Match($text, '(program|publicdata)\s+id\s+([0-9a-f]{16})')
        if (-not $m.Success) { $result.Other += $f.Name; continue }
        $kind, $id = $m.Groups[1].Value, $m.Groups[2].Value
        if (-not $id.StartsWith($GameIdPrefix)) { $result.Other += $f.Name; continue }
        if ($kind -eq 'publicdata') {
            if ($seen.ContainsKey($id)) { $result.Duplicates++ } else { $seen[$id] = 1; $result.Dlc += $f.FullName }
        } elseif ($text -match 'romfs\(patch\)') {
            $v = [regex]::Match($f.Name, '\[v(\d+)\]')
            $version = if ($v.Success) { [int64]$v.Groups[1].Value } else { 0 }
            if ($null -ne $result.Update) { $result.Duplicates++ }
            if ($version -gt $result.UpdateVersion) { $result.Update = $f.FullName; $result.UpdateVersion = $version }
        } else {
            if ($null -ne $result.Base) { $result.Duplicates++ } else { $result.Base = $f.FullName }
        }
    }
    [pscustomobject]$result
}

function Test-FreeSpace([int]$NeedGb) {
    $drive = (Get-Item -LiteralPath $Root).PSDrive.Name
    $free = [math]::Floor((Get-PSDrive $drive).Free / 1GB)
    if ($free -lt $NeedGb) { throw "This needs about $NeedGb GB free on $drive`:, and there's $free GB." }
}

function Invoke-Extract($Files, [string]$KeysFile) {
    if (-not $Files.Base) { throw 'The base game is needed: its .nsp is not in that folder.' }
    $vp = Get-VenvPython
    $script_ = Join-Path $Root 'tools\nsp_extract.py'
    $out = Join-Path $Root 'extracted'
    Write-Step 'Extracting the game...'
    if ($Files.Update) { $list = @($script_, $Files.Update, $KeysFile, $out, '--base', $Files.Base) }
    else { $list = @($script_, $Files.Base, $KeysFile, $out) }
    $code = Invoke-Tool $vp (@('-I', '-u') + $list)
    if ($code -ne 0) { throw "Extracting the game failed (exit code $code). See the log." }
    $i = 0
    foreach ($d in $Files.Dlc) {
        $i++
        Send-Message 'DETAIL' "DLC $i of $($Files.Dlc.Count)"
        $code = Invoke-Tool $vp @('-I', '-u', $script_, $d, $KeysFile, $out)
        if ($code -ne 0) { throw "Extracting $([IO.Path]::GetFileName($d)) failed (exit code $code). See the log." }
    }
    if (-not (Test-Path (Join-Path $out 'exefs\main'))) { throw "Extraction finished without the game's program." }
}

function Invoke-Build {
    $make = "$MsysRoot\usr\bin\make.exe"
    $environment = Get-BuildEnv
    Send-Message 'STAGE' 'recompile|running'
    Write-Step 'Recompiling the game code...'
    $code = Invoke-Tool $make @('gen') $environment
    if ($code -ne 0) { Send-Message 'STAGE' 'recompile|failed'; throw "Recompiling failed (exit code $code). See the log." }
    Send-Message 'STAGE' 'recompile|done'
    Send-Message 'STAGE' 'compile|running'
    Write-Step 'Compiling...'
    $code = Invoke-Tool $make @("-j$([Environment]::ProcessorCount)") $environment
    if ($code -ne 0) { Send-Message 'STAGE' 'compile|failed'; throw "Compiling failed (exit code $code). See the log." }
    foreach ($f in 'doax3recomp.exe', 'doaxgame.dll', 'DOAX3Launcher.exe') {
        if (-not (Test-Path (Join-Path $Root "build\$f"))) { Send-Message 'STAGE' 'compile|failed'; throw "The build finished without build\$f." }
    }
    # (which release it was built from: DOAX3Launcher.exe --play builds again after an update)
    $release = Join-Path $Root 'release.txt'
    $from = if (Test-Path -LiteralPath $release) { (Get-Content -LiteralPath $release -TotalCount 1).Trim() } else { 'source' }
    Set-Content -LiteralPath (Join-Path $Root 'build\built-from.txt') -Value $from -Encoding ASCII
    Send-Message 'STAGE' 'compile|done'
    Write-Step 'The build is finished.'
}

# prod.keys: the one given, else in the game's folder, else ~/.switch/prod.keys
function Find-KeysFile([string]$Game, [string]$Given) {
    if ($Given) {
        if (-not (Test-Path -LiteralPath $Given -PathType Leaf)) { throw "Keys file not found: $Given" }
        return $Given
    }
    $folder = if (Test-Path -LiteralPath $Game -PathType Leaf) { Split-Path -Parent $Game } else { $Game }
    foreach ($p in (Join-Path $folder 'prod.keys'), (Join-Path $env:USERPROFILE '.switch\prod.keys')) {
        if (Test-Path -LiteralPath $p -PathType Leaf) { return $p }
    }
    throw "prod.keys wasn't found. Put your console's prod.keys in the folder with your game files."
}

function Invoke-Pipeline($Files, [string]$KeysFile, [bool]$SkipExtract) {
    Test-FreeSpace $(if ($SkipExtract) { $FreeGbToBuild } else { $FreeGbToExtract })
    if ($SkipExtract) {
        if (-not (Test-Path (Join-Path $Root 'extracted\exefs\main'))) { throw 'There are no extracted game files to keep.' }
        Send-Message 'STAGE' 'extract|done'
    } else {
        Send-Message 'STAGE' 'extract|running'
        try { Invoke-Extract $Files $KeysFile } catch { Send-Message 'STAGE' 'extract|failed'; throw }
        Send-Message 'STAGE' 'extract|done'
    }
    Invoke-Build
}
