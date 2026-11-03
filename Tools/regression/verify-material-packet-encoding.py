"""Compile and run the production packet-encoding contract with boundary inputs."""
import argparse
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument('--configuration', choices=['Debug', 'Release'], default='Debug')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
output = root / 'Build' / 'Verification' / ('MaterialPacketEncoding-' + args.configuration)
output.mkdir(parents=True, exist_ok=True)
executable = output / 'probe.exe'
source = Path(__file__).with_name('material_packet_encoding_probe.cpp')
options = ['/Od', '/RTC1', '/MDd'] if args.configuration == 'Debug' else ['/O2', '/MD']
subprocess.run(['cl', '/nologo', '/EHsc', '/std:c++20', '/W4', '/WX', *options,
                '/Fo' + str(output / 'probe.obj'), '/Fe' + str(executable), str(source)], check=True, cwd=output)
subprocess.run([str(executable)], check=True, timeout=30, cwd=output)
