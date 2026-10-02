function Resolve-EnkiDependencies([string]$Repository) {
    # MSBuild's installed root already contains the triplet; vcpkg appends it again.
    # Also support the conventional layout, but never mix headers and binaries.
    foreach ($relative in @('vcpkg_installed/x64-windows/x64-windows', 'vcpkg_installed/x64-windows')) {
        $candidate = Join-Path $Repository $relative
        $required = @('include/enkiTS/TaskScheduler.h', 'bin/enkiTS.dll', 'lib/enkiTS.lib',
                      'debug/bin/enkiTS.dll', 'debug/lib/enkiTS.lib')
        $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $candidate $_)) })
        if ($missing.Count -eq 0) { return $candidate }
    }
    throw 'Complete Debug/Release enkiTS installation not found under vcpkg_installed. Restore manifest dependencies before running this gate.'
}
