param(
    [Parameter(Mandatory=$true)][string]$Zig,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$')][string]$Version='0.1.0'
)
$ErrorActionPreference='Stop'
$projectRoot=$PSScriptRoot
& "$projectRoot/native/build.ps1" -Zig $Zig
if($LASTEXITCODE -ne 0){throw 'Build failed'}
$dist=Join-Path $projectRoot 'dist'
$packageName="Final-Spatial-Audio-$Version-windows-x64"
$package=Join-Path $dist $packageName
if(Test-Path -LiteralPath $package){throw "Output already exists: $package. Use a fresh checkout or another version."}
New-Item -ItemType Directory -Path "$package/bin","$package/licenses" -Force | Out-Null
Copy-Item -LiteralPath "$projectRoot/native/bin/FinalSpatialAudio.exe" -Destination "$package/bin/FinalSpatialAudio.exe"
Copy-Item -LiteralPath "$projectRoot/native/final-spatial-audio.example.ini" -Destination "$package/final-spatial-audio.ini"
Copy-Item -LiteralPath "$projectRoot/docs/使用说明.txt" -Destination "$package/使用说明.txt"
Copy-Item -LiteralPath "$projectRoot/docs/User-Guide.en.txt" -Destination "$package/User-Guide.en.txt"
Copy-Item -LiteralPath "$projectRoot/LICENSE" -Destination "$package/LICENSE.txt"
Copy-Item -LiteralPath "$projectRoot/THIRD_PARTY_NOTICES.md" -Destination "$package/THIRD_PARTY_NOTICES.md"
Copy-Item -LiteralPath "$projectRoot/native/assets/LICENSE.txt" -Destination "$package/licenses/Icon-assets.txt"
Get-ChildItem -LiteralPath "$projectRoot/licenses" -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination "$package/licenses"}
$utf8=New-Object System.Text.UTF8Encoding($false)
$manifest=Get-ChildItem -LiteralPath $package -File -Recurse | Sort-Object FullName | ForEach-Object {
    $relative=$_.FullName.Substring($package.Length+1).Replace('\','/')
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$relative
}
[IO.File]::WriteAllLines("$package/SHA256SUMS.txt",$manifest,$utf8)
$zip=Join-Path $dist "$packageName.zip"
Compress-Archive -LiteralPath $package -DestinationPath $zip -CompressionLevel Optimal
$hash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$zip.sha256",$hash+'  '+[IO.Path]::GetFileName($zip)+"`n",$utf8)
Write-Output "Created $zip"
