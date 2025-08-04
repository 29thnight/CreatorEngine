#pragma once
using ulong = unsigned long;
using byte = unsigned char;
using uint8 = unsigned char;
using int8 = signed char;
using uint16 = unsigned short;
using ushort = uint16;
using int16 = short;
using uint32 = unsigned int;
using int32 = int;
using uint64 = unsigned long long;
using int64 = long long;
using guid = uint64;
using flag = uint32;
using mask = uint32;
using constant = uint32;
using bool32 = uint32;

#define cbuffer struct alignas(16) 