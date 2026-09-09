# Renders a sample of every voice and a sweep of every parameter, so the whole
# published range can be listened to rather than taken on trust.
param(
    [string]$Probe  = "$PSScriptRoot\..\build_x64\bin\Release\infovox_probe.exe",
    [string]$OutDir = "$PSScriptRoot\..\samples"
)

$phrases = @{
    american  = "Hello, this is the American voice of the Infovox two ten synthesizer, speaking on Windows eleven in 2026."
    british   = "Hello, this is the British voice of the Infovox two ten synthesizer, speaking on Windows eleven in 2026."
    danish    = "Hej, dette er den danske stemme fra Infovox to ti. Klokken er tolv, og der er 1996 boeger."
    finnish   = "Hei, tama on Infovox kaksi kymmenen suomenkielinen aani. Kello on kaksitoista."
    french    = "Bonjour, voici la voix francaise du synthetiseur Infovox deux cent dix. Il est douze heures."
    german    = "Hallo, dies ist die deutsche Stimme des Infovox zwei zehn Sprachsynthesizers. Es ist zwoelf Uhr."
    icelandic = "Hallo, thetta er islenska roddin fra Infovox tveir tiu talgervlinum."
    italian   = "Ciao, questa e la voce italiana del sintetizzatore Infovox due dieci. Sono le dodici."
    norwegian = "Hei, dette er den norske stemmen fra Infovox to ti talesyntese. Klokken er tolv."
    spanish   = "Hola, esta es la voz espanola del sintetizador Infovox dos diez. Son las doce."
    swedish   = "Hej, det har ar den svenska rosten fran Infovox tva tio. Klockan ar tolv."
}
$variantName = @{ 1 = "1-male"; 2 = "2-female"; 3 = "3-deep-male"; 4 = "4-child"; 5 = "5-ghost" }

if (-not (Test-Path $Probe)) { throw "probe not found: $Probe" }
New-Item -ItemType Directory -Force -Path "$OutDir\voices" | Out-Null
New-Item -ItemType Directory -Force -Path "$OutDir\parameters" | Out-Null

# --- one sample per voice -------------------------------------------------
$list = & $Probe list
$voices = @()
foreach ($line in $list) {
    if ($line -match '^(\w{4})\s+(\w+)\s+') { $voices += ,@($Matches[1], $Matches[2]) }
}
Write-Output "rendering $($voices.Count) voice samples"
foreach ($v in $voices) {
    $id = $v[0]; $pack = $v[1]
    $variant = [int]$id.Substring(3,1)
    $name = "{0}_{1}_{2}.wav" -f $pack, $variantName[$variant], $id
    & $Probe speak $id "$OutDir\voices\$name" 15 50 25 0 100 $phrases[$pack] | Out-Null
}

# --- parameter sweeps -----------------------------------------------------
# Each slider runs 0..100, where 0 is the engine minimum and 100 its maximum.
$text = "The quick brown fox jumps over the lazy dog."
$sweeps = @(
    @{ name = "rate";      idx = 0; values = @(0, 10, 15, 25, 50, 75, 100) },
    @{ name = "pitch";     idx = 1; values = @(0, 25, 50, 75, 100) },
    @{ name = "pitchmod";  idx = 2; values = @(0, 25, 50, 75, 100) },
    @{ name = "breath";    idx = 3; values = @(0, 15, 30, 50, 70) },
    @{ name = "volume";    idx = 4; values = @(10, 25, 50, 75, 100) }
)
foreach ($s in $sweeps) {
    foreach ($val in $s.values) {
        $p = @(15, 50, 25, 0, 100, 40)
        $p[$s.idx] = $val
        $name = "{0}_{1:d3}.wav" -f $s.name, $val
        & $Probe speak AM01 "$OutDir\parameters\$name" $p[0] $p[1] $p[2] $p[3] $p[4] $p[5] $text | Out-Null
    }
}

# Consonant clarity: the engine stops at 4 kHz, so /s/, /f/ and /h/ are faint.
# 0 is the untouched 1996 output; 40 is what a fresh install uses.
$sib = "She sells sea shells by the sea shore. His house has fish. Sixty-six."
foreach ($val in @(0, 20, 40, 60, 80, 100)) {
    $name = "clarity_{0:d3}.wav" -f $val
    & $Probe speak AM01 "$OutDir\parameters\$name" 15 50 25 0 100 $val $sib | Out-Null
    $name2 = "clarity_female_{0:d3}.wav" -f $val
    & $Probe speak AM02 "$OutDir\parameters\$name2" 15 50 25 0 100 $val $sib | Out-Null
}

# A couple of combinations that show the extremes reached together.
& $Probe speak AM02 "$OutDir\parameters\extreme_slow_low.wav"   0 0 0 0 100 40 $text | Out-Null
& $Probe speak AM02 "$OutDir\parameters\extreme_fast_high.wav" 100 100 100 0 100 40 $text | Out-Null
& $Probe speak AM04 "$OutDir\parameters\child_breathy.wav"      15 90 57 40 100 40 $text | Out-Null

$n = (Get-ChildItem -Path $OutDir -Recurse -Filter *.wav).Count
Write-Output "wrote $n wav files to $OutDir"
