# Independent product-pixel expectations and same-backend repeatability.
# Dot-source from the PHASE 4 baseline. PowerShell/.NET only; no Python runtime dependency.
if (-not ('PbrProductPixels' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
public static class PbrProductPixels {
    public static float[] Read(string file, int count) {
        byte[] bytes = File.ReadAllBytes(file);
        if (bytes.Length != (long)count * 4) throw new Exception("readback size mismatch: " + file);
        float[] values = new float[count];
        Buffer.BlockCopy(bytes, 0, values, 0, bytes.Length);
        foreach (float x in values) if (float.IsNaN(x) || float.IsInfinity(x))
            throw new Exception("non-finite readback: " + file);
        return values;
    }
    // max delta, RMSE, exceeded samples. The established PBR abs/relative tolerance.
    public static double[] Compare(float[] a, float[] b) {
        if (a.Length != b.Length || a.Length == 0) throw new Exception("incompatible readbacks");
        double max = 0, sum = 0, exceeded = 0;
        for (int i=0; i<a.Length; ++i) {
            double d = Math.Abs((double)a[i]-b[i]);
            if (double.IsNaN(d) || double.IsInfinity(d)) throw new Exception("non-finite comparison");
            max = Math.Max(max,d); sum += d*d;
            if (d > .002 + .005*Math.Max(Math.Abs(a[i]),Math.Abs(b[i]))) ++exceeded;
        }
        return new double[] { max, Math.Sqrt(sum/a.Length), exceeded };
    }
    public static double[] Project(double x, double y, double z, double[] world,
                                    double[] view, double[] projection) {
        double[] v = {x,y,z,1};
        foreach (double[] m in new double[][] {world,view,projection}) {
            if (m.Length != 16) throw new Exception("matrix must have 16 components");
            double[] next = new double[4];
            for (int col=0; col<4; ++col) for (int row=0; row<4; ++row)
                next[col] += v[row]*m[row*4+col];
            v = next;
        }
        if (v[3] <= 0) throw new Exception("fixture is behind the camera");
        return new double[] {v[0]/v[3]*.5+.5, .5-v[1]/v[3]*.5, v[2]/v[3]};
    }
    public static double[] Patch(float[] values, int width, int height, int channels, double u, double v) {
        int cx=(int)(u*width), cy=(int)(v*height), radius=3;
        if (cx-radius<0 || cx+radius>=width || cy-radius<0 || cy+radius>=height)
            throw new Exception("fixture patch is outside capture");
        double[] mean = new double[channels];
        for (int y=cy-radius;y<=cy+radius;++y) for (int x=cx-radius;x<=cx+radius;++x)
            for (int c=0;c<channels;++c) mean[c]+=values[(y*width+x)*channels+c]/49.0;
        return mean;
    }
}
'@
}

function Read-PbrPixelCapture([string]$Directory) {
    $m = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    $data = @{}
    foreach ($a in $m.attachments) {
        if ($data.ContainsKey($a.name) -or $a.width -le 0 -or $a.height -le 0 -or $a.channels -le 0) {
            throw 'Invalid or duplicate attachment'
        }
        $data[$a.name] = [PbrProductPixels]::Read((Join-Path $Directory $a.file), ($a.width*$a.height*$a.channels))
    }
    $expected = @('baseColor','metalRough','normal','emissive','depth','preToneHdr','display')
    if ($data.Count -ne 7 -or @($expected | Where-Object { -not $data.ContainsKey($_) }).Count) {
        throw 'Product pixel validation requires all seven attachments'
    }
    return @{ manifest=$m; pixels=$data }
}

function Assert-PbrRepeatability([string]$Left, [string]$Right, [string]$Output) {
    if ([IO.Path]::GetFullPath($Left) -eq [IO.Path]::GetFullPath($Right)) { throw 'Two independent captures are required' }
    $a = Read-PbrPixelCapture $Left
    $b = Read-PbrPixelCapture $Right
    foreach ($capture in @($a,$b)) {
        $m = $capture.manifest
        if ($m.backend -ne 'dx12' -or $m.captureMode -ne 'static-repeatability-v1' -or
            $m.historyPolicy -ne 'restart-ssgi-fog' -or $m.totalSeconds -ne 0 -or
            $m.deltaSeconds -ne 0 -or $m.sampleIndex -ne 0) { throw 'Uncontrolled capture is not a repeatability reference' }
    }
    if ($a.manifest.frameId -ge $b.manifest.frameId) { throw 'Repeat capture must use a later product frame' }
    foreach ($key in @('camera','lights','skyBoxPath','width','height','sceneEpoch','viewId','historyRevision')) {
        if (($a.manifest.$key | ConvertTo-Json -Depth 20 -Compress) -cne
            ($b.manifest.$key | ConvertTo-Json -Depth 20 -Compress)) { throw "Repeatability input changed: $key" }
    }
    # Exclude only volatile frame/descriptor identities. Static material/world inputs must match.
    $leftDraws = @($a.manifest.draws | Select-Object route,world,modelId,meshId,modelGeneration,
        shaderMetaSlot,shaderMetaGeneration,permutation,propertyBytes,useNormalMap,coverageFlags,alphaCutoff,textures)
    $rightDraws = @($b.manifest.draws | Select-Object route,world,modelId,meshId,modelGeneration,
        shaderMetaSlot,shaderMetaGeneration,permutation,propertyBytes,useNormalMap,coverageFlags,alphaCutoff,textures)
    if (($leftDraws | ConvertTo-Json -Depth 20 -Compress) -cne
        ($rightDraws | ConvertTo-Json -Depth 20 -Compress)) { throw 'Static draw inputs changed between captures' }
    $metrics = @()
    foreach ($attachment in $a.manifest.attachments) {
        $name = $attachment.name
        $other = @($b.manifest.attachments | Where-Object name -eq $name)[0]
        if ($attachment.width -ne $other.width -or $attachment.height -ne $other.height -or
            $attachment.channels -ne $other.channels) { throw "Attachment layout changed: $name" }
        $delta = [PbrProductPixels]::Compare($a.pixels[$name], $b.pixels[$name])
        $metrics += [ordered]@{ name=$name; maxDelta=$delta[0]; rmse=$delta[1]; exceeded=$delta[2]; gated=$true }
    }
    # Mutation control from real readbacks: both formerly ungated outputs must fail the same predicate.
    $mutationRejected = @()
    foreach ($name in @('preToneHdr','display')) {
        [float[]]$changed = $b.pixels[$name].Clone()
        $changed[0] += 1.0
        $mutation = [PbrProductPixels]::Compare($b.pixels[$name],$changed)
        if ($mutation[2] -ne 1) { throw "Pixel comparator did not reject $name mutation" }
        $mutationRejected += $name
    }
    $passed = @($metrics | Where-Object exceeded -gt 0).Count -eq 0
    [ordered]@{ mode='static-repeatability'; left=$Left; right=$Right; passed=$passed;
        tolerance='abs 0.002 + rel 0.5%'; attachments=$metrics; mutationRejected=$mutationRejected } |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Output -Encoding utf8
    if (-not $passed) { throw "Product HDR/display repeatability failed: $Output" }
    Write-Output "Product repeatability PASS (all seven attachments, HDR/display mutations rejected): $Output"
}

function Assert-PbrLightingPixels([string]$Directory, [string]$ModelId, [string]$Output) {
    $capture = Read-PbrPixelCapture $Directory
    $m = $capture.manifest
    $draws = @($m.draws | Where-Object modelId -eq $ModelId)
    if ($draws.Count -ne 6 -or @($draws | Where-Object route -ne 'gbuffer').Count) {
        throw 'Lighting fixture requires six production GBuffer draws'
    }
    $world = [double[]]$draws[0].world
    $centers = @(@(-1.2,.65),@(0,.65),@(1.2,.65),@(-1.2,-.65),@(0,-.65),@(1.2,-.65))
    $samples = @()
    foreach ($center in $centers) {
        $uv = [PbrProductPixels]::Project($center[0],$center[1],0,$world,
            [double[]]$m.camera.view,[double[]]$m.camera.projection)
        $sample = [ordered]@{ u=$uv[0]; v=$uv[1]; projectedDepth=$uv[2] }
        foreach ($a in $m.attachments) {
            $sample[$a.name] = [PbrProductPixels]::Patch($capture.pixels[$a.name],$a.width,$a.height,$a.channels,$uv[0],$uv[1])
        }
        if ([math]::Abs($sample.depth[0]-$uv[2]) -gt .0001) { throw 'Fixture sample does not hit the expected geometry' }
        $samples += $sample
    }
    $errors = [Collections.Generic.List[string]]::new()
    for ($i=0; $i -lt 6; $i++) {
        $expectedAO = if ($i -eq 0) {64.0/255} else {1.0}
        if ([math]::Abs($samples[$i].metalRough[0]-$expectedAO) -gt .005) { $errors.Add("patch $i AO") }
        if ([math]::Abs($samples[$i].metalRough[1]-1) -gt .005 -or
            [math]::Abs($samples[$i].metalRough[2]) -gt .005) { $errors.Add("patch $i roughness/metallic") }
        for ($c=0; $c -lt 3; $c++) {
            $base = if ($i -lt 3) {.6} else {0}
            if ([math]::Abs($samples[$i].baseColor[$c]-$base) -gt .005) { $errors.Add("patch $i base $c") }
            $emission = 0.0
            if ($i -eq 3 -or $i -eq 4) { $emission = @(.5,1,2)[$c] }
            if ($i -eq 4) {
                $encoded = @(128,64,32)[$c]/255.0
                $linear = if ($encoded -le .04045) {$encoded/12.92} else {[math]::Pow(($encoded+.055)/1.055,2.4)}
                $emission *= $linear
            }
            if ([math]::Abs($samples[$i].emissive[$c]-$emission) -gt .003) { $errors.Add("patch $i emission $c") }
        }
    }
    $aoLit = ($samples[0].preToneHdr[0]+$samples[0].preToneHdr[1]+$samples[0].preToneHdr[2])/3
    $neutralLit = ($samples[1].preToneHdr[0]+$samples[1].preToneHdr[1]+$samples[1].preToneHdr[2])/3
    if ($neutralLit - $aoLit -lt .01) { $errors.Add('Material AO does not darken product indirect lighting') }
    for ($c=0; $c -lt 3; $c++) {
        if ($samples[3].preToneHdr[$c]-$samples[5].preToneHdr[$c] -lt @(.05,.1,.2)[$c]) {
            $errors.Add("Constant emission does not reach HDR channel $c")
        }
    }
    if ($samples[3].display[2]-$samples[5].display[2] -lt .05) { $errors.Add('Emission does not reach final display') }
    [ordered]@{ passed=($errors.Count -eq 0); modelId=$ModelId; errors=@($errors.ToArray()); samples=$samples } |
        ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Output -Encoding utf8
    if ($errors.Count) { throw "Product AO/emission pixel failure: $($errors -join ', '); $Output" }
    Write-Output "Product AO/emission pixel expectations PASS: $Output"
}

function Assert-PbrMaterialPixels([string]$Directory, [string]$ModelId, [string]$Kind, [string]$Output) {
    $capture = Read-PbrPixelCapture $Directory
    $m = $capture.manifest
    $draws = @($m.draws | Where-Object modelId -eq $ModelId)
    $required = if ($Kind -eq 'NormalPair') {2} else {3}
    if ($draws.Count -ne $required) { throw "$Kind product draw count is wrong" }
    $probes = @()
    if ($Kind -eq 'NormalPair') {
        $probes = @(@{x=-.85;y=0;expected=@()},@{x=.6;y=0;expected=@()})
    } else {
        $quads = @(@(-1.65,-.55),@(-.45,.45),@(.55,1.65))
        $colors = @(@(255,0,0),@(255,255,0),@(0,0,255),@(0,255,0))
        for ($i=0;$i -lt 3;$i++) {
            foreach ($uv in @(@(1.25,.25),@(1.75,.25),@(1.25,1.25))) {
                $u = if ($i -eq 0) {$uv[0]-1} elseif ($i -eq 1) {1.0} else {2-$uv[0]}
                $v = if ($uv[1] -lt 1) {$uv[1]} elseif ($i -eq 2) {2-$uv[1]} else {$uv[1]-1}
                $color = $colors[($(if($v -lt .5){0}else{2}))+($(if($u -lt .5){0}else{1}))]
                $expected = @($color | ForEach-Object { $s=$_/255.0; if($s -le .04045){$s/12.92}else{[math]::Pow(($s+.055)/1.055,2.4)} })
                $probes += @{ x=$quads[$i][0]+($quads[$i][1]-$quads[$i][0])*$uv[0]/2;
                    y=.5-$uv[1]/2; expected=$expected }
            }
        }
    }
    $samples = @()
    foreach ($probe in $probes) {
        $uv = [PbrProductPixels]::Project($probe.x,$probe.y,0,[double[]]$draws[0].world,
            [double[]]$m.camera.view,[double[]]$m.camera.projection)
        $sample = [ordered]@{ x=$probe.x; y=$probe.y; expected=$probe.expected }
        foreach ($name in @('depth','normal','baseColor')) {
            $a = @($m.attachments | Where-Object name -eq $name)[0]
            $sample[$name] = [PbrProductPixels]::Patch($capture.pixels[$name],$a.width,$a.height,$a.channels,$uv[0],$uv[1])
        }
        if ([math]::Abs($sample.depth[0]-$uv[2]) -gt .0001) { throw "$Kind probe does not hit its fixture" }
        $samples += $sample
    }
    $passed = $true
    if ($Kind -eq 'NormalPair') {
        $tilted = @($samples[0].normal | ForEach-Object {$_*2-1})
        $flat = @($samples[1].normal | ForEach-Object {$_*2-1})
        $passed = [math]::Abs($tilted[0]) -gt .3 -and [math]::Abs($tilted[1]) -lt .05 -and
            $tilted[2] -lt -.6 -and [math]::Abs($flat[0]) -lt .015 -and
            [math]::Abs($flat[1]) -lt .015 -and $flat[2] -lt -.95
    } else {
        foreach ($sample in $samples) {
            for ($c=0;$c -lt 3;$c++) {
                if ([math]::Abs($sample.baseColor[$c]-$sample.expected[$c]) -gt .015) { $passed=$false }
            }
        }
    }
    [ordered]@{ passed=$passed; kind=$Kind; modelId=$ModelId; samples=$samples } |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Output -Encoding utf8
    if (-not $passed) { throw "$Kind product pixel expectations failed: $Output" }
    Write-Output "$Kind product pixel expectations PASS: $Output"
}
