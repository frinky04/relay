param(
    [string]$BuildDir = 'build',
    [string]$OutputDir = 'dist/release'
)
$ErrorActionPreference = 'Stop'
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$version = (Get-Content -LiteralPath "$BuildDir/version.txt" -Raw).Trim()
$sdkVersion = (Get-Content -LiteralPath "$BuildDir/velopack-version.txt" -Raw).Trim()
$toolDir = Join-Path $BuildDir "vpk/$sdkVersion"
$vpk = Join-Path $toolDir 'vpk.exe'
if (!(Test-Path -LiteralPath $vpk)) {
    dotnet tool install vpk --version $sdkVersion --tool-path $toolDir
    if ($LASTEXITCODE -ne 0) { throw 'Velopack tool installation failed' }
}

# Fresh staging prevents removed resources from leaking into later packages.
$staging = Join-Path $BuildDir ('packaging/' + [guid]::NewGuid().ToString('N'))
$app = Join-Path $staging 'app'
$release = Join-Path $staging 'release'
New-Item -ItemType Directory -Path $app -Force | Out-Null
cmake --install $BuildDir --config Release --prefix $app
if ($LASTEXITCODE -ne 0) { throw 'Release staging failed' }

function Pack-Relay([string]$PackageVersion, [string]$Destination) {
    # Do not execute the GUI as part of packaging; the SDK is exercised below.
    & $vpk pack --packId frinky04.Relay --packVersion $PackageVersion --packDir $app `
        --mainExe relay.exe --packTitle Relay --packAuthors Frinky --runtime win-x64 `
        --icon "$app/assets/relay.ico" --shortcuts StartMenuRoot --delta None --skipVeloAppCheck `
        --outputDir $Destination
    if ($LASTEXITCODE -ne 0) { throw "Packaging $PackageVersion failed" }
}
Pack-Relay $version $release

# A second package exercises the actual SDK's version/feed/hash/download path.
# It is a local fixture only; never copied to the release output.
$next = [version]$version
$fixtureVersion = "$($next.Major).$($next.Minor).$($next.Build + 1)"
$fixture = Join-Path $staging 'fixture'
$portable = Join-Path $staging 'portable'
Pack-Relay $fixtureVersion $fixture
Expand-Archive -LiteralPath "$release/frinky04.Relay-win-Portable.zip" -DestinationPath $portable
& "$BuildDir/Release/relay_test.exe" update-feed $portable $fixture
if ($LASTEXITCODE -ne 0) { throw 'Packaged update verification failed' }

$files = @('frinky04.Relay-win-Setup.exe', 'frinky04.Relay-win-Portable.zip',
    "frinky04.Relay-$version-full.nupkg", 'releases.win.json', 'RELEASES', 'assets.win.json')
$checksums = foreach ($name in $files) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $release $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
$checksums | Set-Content -LiteralPath "$release/SHA256SUMS" -Encoding ascii
$commit = if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { (git rev-parse HEAD).Trim() }
@{ version = $version; commit = $commit } | ConvertTo-Json | Set-Content "$release/build-info.json" -Encoding utf8NoBOM
python "$PSScriptRoot/verify_release.py" $release $version $commit
if ($LASTEXITCODE -ne 0) { throw 'Release file verification failed' }

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
foreach ($name in $files + @('SHA256SUMS', 'build-info.json')) {
    Copy-Item -LiteralPath (Join-Path $release $name) -Destination (Join-Path $OutputDir $name) -Force
}
Write-Host "Verified release files: $OutputDir"
