$ErrorActionPreference='Stop'
$dir=$PSScriptRoot
[void](New-Item -ItemType Directory -Force "$dir/cpu-context")
foreach($name in 'main.cpp','context.h','context.cpp','benchmark_support.h'){Copy-Item "$dir/$name" "$dir/cpu-context/$name"}
$p="$dir/cpu-context/benchmark_support.h"
$s=[IO.File]::ReadAllText($p).Replace('  array_context context{};',"  array_context context{};`n  double kernel_ns=0,user_ns=0;")
$s=$s.Replace('  last_diagnostic.context = context_end();',"  last_diagnostic.kernel_ns=double(ticks(k1)-ticks(k0))*100.0;`n  last_diagnostic.user_ns=double(ticks(u1)-ticks(u0))*100.0;`n  last_diagnostic.context = context_end();")
$s=$s.Replace('  std::sort(x.begin(), x.end());','  for(size_t i=0;i<x.size();++i){ std::printf("thread_parts,%s,%zu,%zu,custom,%.0f,%.0f\n",name,n,i,dx[i].kernel_ns,dx[i].user_ns); std::printf("thread_parts,%s,%zu,%zu,stl,%.0f,%.0f\n",name,n,i,dy[i].kernel_ns,dy[i].user_ns); }'+"`n"+'  std::sort(x.begin(), x.end());')
[IO.File]::WriteAllText($p,$s)
$s=[IO.File]::ReadAllText("$dir/controlled_process.cpp").Replace('#include <string>',"#include <string>`n#include <cwchar>").Replace('if (argc < 3)','if (argc < 4)').Replace('for (int i = 2; i < argc; ++i)','for (int i = 3; i < argc; ++i)').Replace('if (i != 2)','if (i != 3)').Replace('CreateProcessW(argv[2]','CreateProcessW(argv[3]')
$at=$s.IndexOf('    const auto observed =')
$s=$s.Insert($at,@'
    const unsigned cpu = static_cast<unsigned>(std::wcstoul(argv[2], nullptr, 10));
    if (cpu >= 64 || !SetProcessAffinityMask(process.hProcess, DWORD_PTR(1) << cpu))
    {
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job);
        return 9;
    }
'@+"`n")
[IO.File]::WriteAllText("$dir/controlled_cpu.cpp",$s)
$s=[IO.File]::ReadAllText("$dir/with-guard.ps1").Replace('[switch]$High)','[switch]$High,[uint32]$Cpu=5)').Replace('@(''5'',','@("$Cpu",').Replace('cpu=5;peerCpu=4;','cpu=$Cpu;peerCpu=($Cpu-1);')
[IO.File]::WriteAllText("$dir/with-cpu-guard.ps1",$s)
$s=[IO.File]::ReadAllText("$dir/variance_load.cpp").Replace('    const bool memory =','    const bool stream = std::strcmp(argv[1], "stream") == 0;'+"`n"+'    const bool memory =').Replace('std::strcmp(argv[1], "memory") == 0;','std::strcmp(argv[1], "memory") == 0 || stream;')
$s=$s.Replace('        for (unsigned i = 0; i < 16384; ++i)',@'
        if (stream)
        {
            volatile std::uint64_t* target = storage.data();
            for (size_t i = 0; i < storage.size(); ++i) target[i] = target[i] + 1;
            continue;
        }
        for (unsigned i = 0; i < 16384; ++i)
'@)
[IO.File]::WriteAllText("$dir/stream_load.cpp",$s)
$s=[IO.File]::ReadAllText("$dir/with-load.ps1").Replace("'integer','memory'","'integer','memory','stream'").Replace('build/Release/variance_load.exe','build/Release/stream_load.exe')
[IO.File]::WriteAllText("$dir/with-stream-load.ps1",$s)
