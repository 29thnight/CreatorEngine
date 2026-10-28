param([string]$Ready,[string]$Stop)
$ErrorActionPreference='Stop'
[Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity=[IntPtr]1
Add-Type -TypeDefinition @"
using System;
using System.Threading;
public static class MemoryLoad {
 public static long Run(EventWaitHandle ready, EventWaitHandle stop) {
  byte[] data=new byte[128*1024*1024];
  for(int i=0;i<data.Length;i+=4096) data[i]=(byte)i;
  uint state=1; long sum=0; ready.Set();
  do { for(int j=0;j<1000000;j++){state=state*1664525+1013904223; int p=(int)((state & 0x1fffff) << 6); sum+=data[p]; data[p]=(byte)(data[p]+1);} } while(!stop.WaitOne(0));
  GC.KeepAlive(data); return sum;
 }
}
"@
$r=[Threading.EventWaitHandle]::OpenExisting($Ready);$s=[Threading.EventWaitHandle]::OpenExisting($Stop)
try { [MemoryLoad]::Run($r,$s) } finally { $r.Dispose();$s.Dispose() }
