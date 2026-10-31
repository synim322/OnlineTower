# Run while the editor is closed so it cannot overwrite these user settings on exit.
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$path=Join-Path $projectRoot 'Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini'
$section='[/Script/UnrealEd.LevelEditorPlaySettings]'
$settings=[ordered]@{
    PlayNetMode='PIE_ListenServer'
    PlayNumberOfClients='2'
    RunUnderOneProcess='True'
    LastExecutedPlayModeType='PlayMode_InEditorFloating'
    LastExecutedPlayModeLocation='PlayLocation_DefaultPlayerStart'
    NewWindowWidth='960'
    NewWindowHeight='640'
    GameGetsMouseControl='True'
}
$lines=if(Test-Path -LiteralPath $path){@(Get-Content -LiteralPath $path)}else{@()}
$result=[Collections.Generic.List[string]]::new()
$inside=$false
$found=$false
foreach($line in $lines){
    if($line -match '^\['){
        $inside=$line -eq $section
        $result.Add($line)
        if($inside){$found=$true;foreach($key in $settings.Keys){$result.Add("$key=$($settings[$key])")}}
        continue
    }
    if($inside -and $line -match '^([^=]+)=' -and $settings.Contains($Matches[1])){continue}
    $result.Add($line)
}
if(-not $found){$result.Add('');$result.Add($section);foreach($key in $settings.Keys){$result.Add("$key=$($settings[$key])")}}
New-Item -ItemType Directory -Force -Path (Split-Path $path -Parent) | Out-Null
$result | Set-Content -LiteralPath $path -Encoding UTF8
Write-Output 'PIE configured: two windows, Listen Server, 960x640, mouse capture. Open /Game/Maps/Hub/L_Hub and press Play.'
