# The setup window: point it at your game and your keys, press Build. Start it with Setup.bat.
# -Game, -Keys: fill the boxes in. -AutoStart: press Build once they're checked. -Snapshot <png>: run off screen, save a
# picture of the window when idle, exit (0: ready or built, 1: an error). The last two are for testing.
# -KeepExtracted: tick "Keep the game files already extracted". -CloseWhenDone: close after a build, exit 0 if it was built.
# -Prepare: no window. Installs the tools if needed, builds the game from -Game (a folder, or the base game's .nsp) and
# -Keys (default: prod.keys in that folder, then ~/.switch), printing "progress <0..1> <text>" lines; exit 0 when built.
# For RecompLauncher, through DOAX3Launcher.exe --prepare. An error is one line on stderr.
param([string]$Game = '', [string]$Keys = '', [switch]$AutoStart, [string]$Snapshot = '', [switch]$KeepExtracted,
    [switch]$CloseWhenDone, [switch]$Prepare)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase, System.Windows.Forms

$Root = Split-Path -Parent $PSScriptRoot
$Core = Join-Path $PSScriptRoot 'SetupCore.ps1'
$SettingsFile = Join-Path $PSScriptRoot 'settings.json'

if ($Prepare) {
    $Shared = [hashtable]::Synchronized(@{ Queue = New-Object 'Collections.Concurrent.ConcurrentQueue[string]'; Cancel = $false; ChildPid = 0 })
    . $Core
    $script:at = 0.0
    $script:dlc = $false
    function Say([double]$Fraction, [string]$Text) {
        if ($Fraction -gt $script:at) { $script:at = [math]::Min(1.0, $Fraction) }
        [Console]::Out.WriteLine([string]::Format([Globalization.CultureInfo]::InvariantCulture, 'progress {0:0.000} {1}', $script:at, $Text))
        [Console]::Out.Flush()
    }
    $Shared.OnMessage = {
        param($Kind, $Text)
        try { [IO.File]::AppendAllText((Join-Path $Root 'setup.log'), "$Text`r`n") } catch { }
        switch ($Kind) {
            'SCANNING' { $i, $n = $Text -split ' of '; Say (0.10 + 0.05 * [int]$i / [int]$n) "Reading your game files ($Text)" }
            'DETAIL' { $i, $n = ($Text -replace '^DLC ', '') -split ' of '; Say (0.27 + 0.18 * [int]$i / [int]$n) "Extracting $Text" }
            'STAGE' {
                switch ($Text) {
                    'extract|running' { Say 0.15 'Extracting your game files' }
                    'recompile|running' { Say 0.45 "Recompiling the game's code" }
                    'compile|running' { Say 0.60 'Compiling the game' }
                }
            }
            'LOG' { if ($Text -match '^(Installing MSYS2|Installing the|Installing Python|Updating MSYS2|Creating the Python)') { Say 0.01 $Text } }
        }
    }
    try {
        if (-not $Game) { throw 'No game folder was given (-Game).' }
        Say 0 'Checking the build tools'
        if (-not (Get-ToolStatus).Ready) { Install-Tools }
        $keysFile = Find-KeysFile $Game $Keys
        Say 0.10 'Reading your game files'
        $files = Get-GameFiles $Game $keysFile
        if (-not $files.Base) {
            $bad = @($files.Unreadable).Count
            if ($bad -gt 0 -and $null -eq $files.Update -and @($files.Dlc).Count -eq 0) {
                throw "None of the $bad .nsp files could be read with that prod.keys. The keys may be older than your game files (dump them again from your Switch), or from another console."
            }
            if ($bad -gt 0) { throw "The base game's .nsp couldn't be found: $bad of the files couldn't be read with that prod.keys, and it may be one of them. The keys may be older than the game files." }
            throw "The base game's .nsp isn't in that folder (title ID 01009CC00C97C000). It's needed, with the update and DLC if you have them."
        }
        # (RecompLauncher sets up again after each update: the same files are not extracted again)
        $keep = Test-ExtractedFrom $files
        if ($keep) { Write-Step 'The game files were extracted from these same files before: kept.' }
        Invoke-Pipeline $files $keysFile $keep
        try { @{ Game = $Game; Keys = $keysFile } | ConvertTo-Json | Set-Content -LiteralPath $SettingsFile -Encoding UTF8 } catch { }
        Say 1 'Ready to play'
        exit 0
    } catch {
        try { [IO.File]::AppendAllText((Join-Path $Root 'setup.log'), "Failed: $($_.Exception.Message)`r`n") } catch { }
        [Console]::Error.WriteLine($_.Exception.Message)
        exit 1
    }
}

$window = [Windows.Markup.XamlReader]::Parse((Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Setup.xaml') -Raw))
$ui = @{}
foreach ($n in 'Logo', 'InstallButton', 'ToolsText', 'GameBox', 'GameBrowse', 'KeysBox', 'KeysBrowse', 'GameText', 'BuildButton',
    'CancelButton', 'BuildHint', 'KeepBox', 'StageExtract', 'StageRecompile', 'StageCompile', 'Progress', 'BuildText', 'LogToggle',
    'LogBox', 'PlayButton', 'FolderButton') { $ui[$n] = $window.FindName($n) }

$uri = New-Object Uri (Join-Path $Root 'assets\logo.png')
$ui.Logo.Source = New-Object Windows.Media.Imaging.BitmapImage $uri
$window.Icon = [Windows.Media.Imaging.BitmapFrame]::Create((New-Object Uri (Join-Path $Root 'assets\icon.ico')))

# ---- shared with the background thread
$Shared = [hashtable]::Synchronized(@{
        Queue = New-Object 'Collections.Concurrent.ConcurrentQueue[string]'
        Cancel = $false; ChildPid = 0
    })
$state = @{ Busy = $false; Tools = $false; Files = $null; ScanKey = ''; Failed = $false; Stage = @{}; Started = $null; AutoPending = [bool]$AutoStart; Action = '' }

function Start-Work([string]$Action, [string]$Body, [hashtable]$Arguments = @{}) {
    # Runs Body with the core's functions, on its own thread. Its result goes back as JSON in a RESULT message.
    $state.Busy = $true
    $state.Action = $Action
    $Shared.Cancel = $false
    $rs = [runspacefactory]::CreateRunspace()
    $rs.Open()
    $rs.SessionStateProxy.SetVariable('Shared', $Shared)
    $rs.SessionStateProxy.SetVariable('Root', $Root)
    $rs.SessionStateProxy.SetVariable('A', $Arguments)
    $rs.SessionStateProxy.SetVariable('CoreFile', $Core)
    $ps = [powershell]::Create()
    $ps.Runspace = $rs
    $script = @"
`$ErrorActionPreference = 'Stop'
. `$CoreFile
try {
    `$out = & { $Body }
    `$Shared.Queue.Enqueue('RESULT|' + (ConvertTo-Json -InputObject `$out -Depth 4 -Compress))
} catch {
    `$Shared.Queue.Enqueue('LOG|' + `$_.ScriptStackTrace)
    `$Shared.Queue.Enqueue('FAIL|' + `$_.Exception.Message)
}
"@
    [void]$ps.AddScript($script)
    [void]$ps.BeginInvoke()
    Update-Buttons
}

function Update-Buttons {
    $ready = $state.Tools -and $state.Files -and $state.Files.Base
    $haveExtracted = Test-Path (Join-Path $Root 'extracted\exefs\main')
    $ui.BuildButton.IsEnabled = (-not $state.Busy) -and ($ready -or ($state.Tools -and $haveExtracted -and $ui.KeepBox.IsChecked))
    $ui.InstallButton.IsEnabled = -not $state.Busy
    $ui.GameBrowse.IsEnabled = -not $state.Busy
    $ui.KeysBrowse.IsEnabled = -not $state.Busy
    $ui.KeepBox.Visibility = if ($haveExtracted) { 'Visible' } else { 'Collapsed' }
    $ui.CancelButton.Visibility = if ($state.Busy -and $state.Action -eq 'build') { 'Visible' } else { 'Collapsed' }
    $ui.Progress.IsIndeterminate = $state.Busy -and $state.Action -in 'build', 'install', 'scan'
    $ui.PlayButton.IsEnabled = (-not $state.Busy) -and (Test-Path (Join-Path $Root 'build\DOAX3Launcher.exe')) -and $haveExtracted
}

function Set-Stage([string]$Name, [string]$How) {
    $map = @{ extract = @($ui.StageExtract, 'Extract game files'); recompile = @($ui.StageRecompile, 'Recompile game code'); compile = @($ui.StageCompile, 'Compile') }
    $box, $label = $map[$Name]
    $sign, $colour = switch ($How) {
        'running' { [char]0x25D4, '#2DD4BF' }
        'done' { [char]0x2713, '#34D399' }
        'failed' { [char]0x2717, '#F87171' }
        default { [char]0x25CB, '#94A3B8' }
    }
    $box.Text = "$sign  $label"
    $box.Foreground = (New-Object Windows.Media.BrushConverter).ConvertFromString($colour)
}

function Add-Log([string]$Text) {
    $box = $ui.LogBox
    if ($box.LineCount -gt 4000) { $box.Text = ($box.Text -split "`r?`n" | Select-Object -Last 2000) -join "`r`n" }
    $box.AppendText($Text + "`r`n")
    if ($box.Visibility -eq 'Visible') { $box.ScrollToEnd() }
}

function Set-Colour($Box, [string]$Colour) { $Box.Foreground = (New-Object Windows.Media.BrushConverter).ConvertFromString($Colour) }

function Save-Paths {
    try { @{ Game = $ui.GameBox.Text; Keys = $ui.KeysBox.Text } | ConvertTo-Json | Set-Content -LiteralPath $SettingsFile -Encoding UTF8 } catch { }
}

# ---- the steps
function Start-ToolCheck {
    $ui.ToolsText.Text = 'Checking...'
    Start-Work 'check' 'Get-ToolStatus'
}

function Start-Scan {
    $g, $k = $ui.GameBox.Text.Trim('" '), $ui.KeysBox.Text.Trim('" ')
    if ($state.Busy -or -not $state.Tools -or -not $g -or -not $k) { return }
    if (-not (Test-Path -LiteralPath $g)) { Show-GameError "That game folder doesn't exist."; return }
    if ($state.ScanKey -eq "$g|$k") { return }
    $state.ScanKey = "$g|$k"
    $state.Files = $null
    Set-Colour $ui.GameText '#94A3B8'
    $ui.GameText.Text = 'Reading your game files...'
    Start-Work 'scan' 'Get-GameFiles $A.Game $A.Keys' @{ Game = $g; Keys = $k }
}

function Show-GameError([string]$Text) {
    Set-Colour $ui.GameText '#F87171'
    $ui.GameText.Text = $Text
    $state.Files = $null
    Update-Buttons
}

function Show-Files($f) {
    $state.Files = $f
    $bits = @()
    if ($f.Base) { $bits += 'base game' }
    if ($f.Update) { $bits += 'update' }
    if ($f.Dlc) { $bits += "$(@($f.Dlc).Count) DLC" }
    if ($f.Base) {
        Set-Colour $ui.GameText '#34D399'
        $text = "Found: " + ($bits -join ', ') + '.'
        if ($f.Duplicates) { $text += " $($f.Duplicates) duplicate file(s) will be skipped." }
        if (@($f.Other).Count) { $text += " $(@($f.Other).Count) file(s) belong to something else." }
        if (@($f.Unreadable).Count) { $text += " $(@($f.Unreadable).Count) file(s) couldn't be read (see the details below)." }
        $ui.GameText.Text = $text
    } elseif ($f.Update -or $f.Dlc) {
        Show-GameError ("Found " + ($bits -join ', ') + ", but the base game's .nsp is needed too.")
    } elseif (@($f.Unreadable).Count) {
        Show-GameError "None of those files could be read. Your keys may be older than the game files: see the details below."
    } else {
        Show-GameError "That folder doesn't have this game in it."
    }
    Save-Paths
}

function Start-Build {
    foreach ($s in 'extract', 'recompile', 'compile') { Set-Stage $s 'waiting' }
    $state.Started = Get-Date
    Set-Colour $ui.BuildText '#94A3B8'
    $ui.BuildText.Text = 'Starting...'
    $keep = [bool]$ui.KeepBox.IsChecked
    Start-Work 'build' 'Invoke-Pipeline $A.Files $A.Keys $A.Keep; "built"' @{ Files = $state.Files; Keys = $ui.KeysBox.Text.Trim('" '); Keep = $keep }
}

function Finish-Work([bool]$Ok, [string]$Payload) {
    $action = $state.Action
    $state.Busy = $false
    $state.Action = ''
    switch ($action) {
        'check' {
            if (-not $Ok) { $ui.ToolsText.Text = $Payload; Set-Colour $ui.ToolsText '#F87171'; break }
            $s = ConvertFrom-Json $Payload
            $state.Tools = [bool]$s.Ready
            if ($s.Ready) {
                Set-Colour $ui.ToolsText '#34D399'
                $ui.ToolsText.Text = 'Ready: MSYS2 compilers and libraries, and Python 3.12.'
                $ui.InstallButton.Visibility = 'Collapsed'
            } else {
                $need = @()
                if (-not $s.Msys) { $need += 'MSYS2 (compilers and libraries)' } elseif (@($s.Missing).Count) { $need += "$(@($s.Missing).Count) MSYS2 packages" }
                if (-not $s.Python) { $need += 'Python 3.12' } elseif (-not $s.Venv) { $need += 'the Python packages' }
                Set-Colour $ui.ToolsText '#94A3B8'
                $ui.ToolsText.Text = 'Needed: ' + ($need -join ', ') + '. These are free downloads, a few GB in all. Windows may ask for permission.'
                $ui.InstallButton.Visibility = 'Visible'
                if ($state.AutoPending -and -not $state.Failed) {
                    $state.Failed = $true  # (until the install succeeds: one attempt only)
                    Start-Work 'install' 'Install-Tools; "ok"'
                    return
                }
            }
        }
        'install' {
            if ($Ok) { $ui.BuildText.Text = ''; $state.Failed = $false }
            else { Set-Colour $ui.ToolsText '#F87171'; $ui.ToolsText.Text = $Payload; $state.Failed = $true; break }
            Update-Buttons
            Start-ToolCheck
            return
        }
        'scan' {
            if ($Ok) { Show-Files (ConvertFrom-Json $Payload) } else { $state.ScanKey = ''; Show-GameError $Payload; $state.Failed = $true }
        }
        'build' {
            $took = (Get-Date) - $state.Started
            $min = [math]::Round($took.TotalMinutes)
            if ($Ok) {
                $state.Built = $true
                Set-Colour $ui.BuildText '#34D399'
                $ui.BuildText.Text = "Finished in $min minutes. Press Play."
                if ($CloseWhenDone) { $window.Close() }
            } else {
                $state.Failed = -not ($Payload -eq 'Cancelled.')
                Set-Colour $ui.BuildText $(if ($Payload -eq 'Cancelled.') { '#94A3B8' } else { '#F87171' })
                $ui.BuildText.Text = $Payload
            }
        }
    }
    Update-Buttons
    if ($state.AutoPending -and -not $state.Busy -and $state.Tools) {
        if ($KeepExtracted -and $action -eq 'check' -and (Test-Path (Join-Path $Root 'extracted\exefs\main'))) {
            $state.AutoPending = $false; $ui.KeepBox.IsChecked = $true; Start-Build; return
        }
        if ($ui.GameBox.Text -and $null -eq $state.Files -and $action -ne 'scan') { Start-Scan; return }
        if ($state.Files -and $state.Files.Base -and $action -eq 'scan') { $state.AutoPending = $false; Start-Build; return }
    } elseif ($state.Tools -and -not $state.Busy -and $action -eq 'check') { Start-Scan }
}

# ---- messages from the background thread
$timer = New-Object Windows.Threading.DispatcherTimer
$timer.Interval = [TimeSpan]::FromMilliseconds(120)
$timer.Add_Tick({
        $m = $null
        $n = 0
        while ($n -lt 400 -and $Shared.Queue.TryDequeue([ref]$m)) {
            $n++
            $kind, $text = $m -split '\|', 2
            switch ($kind) {
                'LOG' { Add-Log $text; if ($state.Action -eq 'build' -or $state.Action -eq 'install') { $ui.BuildText.Text = $text } }
                'DETAIL' { Add-Log $text }
                'SCANNING' { $ui.GameText.Text = "Reading your game files ($text)..." }
                'STAGE' { $name, $how = $text -split '\|'; Set-Stage $name $how }
                'RESULT' { Finish-Work $true $text }
                'FAIL' { Add-Log "Failed: $text"; Finish-Work $false $text }
            }
        }
        if ($Snapshot -and -not $state.Busy -and -not $state.AutoPending -and -not $state.SnapshotDone) {
            $state.SnapshotDone = $true
            $window.Dispatcher.BeginInvoke([Action] { Save-Snapshot }, [Windows.Threading.DispatcherPriority]::ApplicationIdle) | Out-Null
        }
    })

function Save-Snapshot {
    $window.UpdateLayout()
    $content = $window.Content
    $w = [int]$content.ActualWidth + 40
    $h = [int]$content.ActualHeight + 30
    $bmp = New-Object Windows.Media.Imaging.RenderTargetBitmap $w, $h, 96, 96, ([Windows.Media.PixelFormats]::Pbgra32)
    $back = New-Object Windows.Media.DrawingVisual
    $dc = $back.RenderOpen()
    $dc.DrawRectangle($window.Background, $null, (New-Object Windows.Rect 0, 0, $w, $h))
    $dc.Close()
    $bmp.Render($back)
    $bmp.Render($content)
    $enc = New-Object Windows.Media.Imaging.PngBitmapEncoder
    $enc.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bmp))
    $file = [IO.File]::Create($Snapshot)
    $enc.Save($file)
    $file.Close()
    $window.Close()
}

# ---- the buttons
$ui.InstallButton.Add_Click({
        Set-Colour $ui.ToolsText '#94A3B8'
        $ui.ToolsText.Text = 'Installing. This can take a while...'
        Start-Work 'install' 'Install-Tools; "ok"'
    })
$ui.GameBrowse.Add_Click({
        $d = New-Object Windows.Forms.FolderBrowserDialog
        $d.Description = "Choose the folder with your game's .nsp files"
        if ($ui.GameBox.Text -and (Test-Path -LiteralPath $ui.GameBox.Text)) { $d.SelectedPath = $ui.GameBox.Text }
        if ($d.ShowDialog() -eq 'OK') { $ui.GameBox.Text = $d.SelectedPath; Find-Keys; Start-Scan }
    })
$ui.KeysBrowse.Add_Click({
        $d = New-Object Microsoft.Win32.OpenFileDialog
        $d.Title = 'Choose the prod.keys from your console'
        $d.Filter = 'Keys (*.keys)|*.keys|All files (*.*)|*.*'
        if ($d.ShowDialog($window)) { $ui.KeysBox.Text = $d.FileName; Start-Scan }
    })
$ui.GameBox.Add_LostFocus({ Start-Scan })
$ui.KeysBox.Add_LostFocus({ Start-Scan })
$ui.KeepBox.Add_Click({ Update-Buttons })
$ui.BuildButton.Add_Click({ Start-Build })
$ui.CancelButton.Add_Click({
        $Shared.Cancel = $true
        $ui.BuildText.Text = 'Stopping...'
        if ($Shared.ChildPid) { & taskkill.exe /T /F /PID $Shared.ChildPid 2>&1 | Out-Null }
    })
$ui.LogToggle.Add_Click({
        if ($ui.LogBox.Visibility -eq 'Visible') { $ui.LogBox.Visibility = 'Collapsed'; $ui.LogToggle.Content = 'Show details' }
        else { $ui.LogBox.Visibility = 'Visible'; $ui.LogToggle.Content = 'Hide details'; $ui.LogBox.ScrollToEnd() }
    })
$ui.PlayButton.Add_Click({ Start-Process -FilePath (Join-Path $Root 'build\DOAX3Launcher.exe') -WorkingDirectory (Join-Path $Root 'build') })
$ui.FolderButton.Add_Click({ Start-Process explorer.exe $Root })
$window.Add_Closing({
        param($source, $e)
        if ($state.Busy -and $state.Action -in 'build', 'install' -and -not $Snapshot) {
            $r = [Windows.MessageBox]::Show($window, 'Stop the work in progress and close?', 'DOAX3 Recomp Setup', 'YesNo', 'Warning')
            if ($r -ne 'Yes') { $e.Cancel = $true; return }
            $Shared.Cancel = $true
            if ($Shared.ChildPid) { & taskkill.exe /T /F /PID $Shared.ChildPid 2>&1 | Out-Null }
        }
    })

function Find-Keys {
    if ($ui.KeysBox.Text) { return }
    $places = @()
    if ($ui.GameBox.Text) { $places += (Join-Path $ui.GameBox.Text 'prod.keys') }
    $places += (Join-Path $env:USERPROFILE '.switch\prod.keys')
    foreach ($p in $places) { if (Test-Path -LiteralPath $p) { $ui.KeysBox.Text = $p; return } }
}

# ---- start
if (Test-Path -LiteralPath $SettingsFile) {
    try {
        $saved = Get-Content -LiteralPath $SettingsFile -Raw | ConvertFrom-Json
        if ($saved.Game) { $ui.GameBox.Text = $saved.Game }
        if ($saved.Keys) { $ui.KeysBox.Text = $saved.Keys }
    } catch { }
}
if ($KeepExtracted) { $ui.KeepBox.IsChecked = $true }
if ($Game) { $ui.GameBox.Text = $Game }
if ($Keys) { $ui.KeysBox.Text = $Keys }
Find-Keys
if ($Snapshot) { $window.WindowStartupLocation = 'Manual'; $window.Left = -20000; $window.Top = 0; $window.ShowInTaskbar = $false }
$window.Add_ContentRendered({ $timer.Start(); Update-Buttons; Start-ToolCheck })
[void]$window.ShowDialog()
$timer.Stop()
if ($Snapshot) { exit $(if ($state.Failed) { 1 } else { 0 }) }
if ($CloseWhenDone) { exit $(if ($state.Built) { 0 } else { 1 }) }
