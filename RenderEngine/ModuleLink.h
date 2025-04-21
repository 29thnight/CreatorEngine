#pragma once
#include <iostream>
#include <string>

using NodeID = uint32_t;
using PinID = uint32_t;

enum class PinKind { Input, Output };

struct NodePin
{
    PinID id;
    PinKind kind;
    std::string name;
};

struct ModuleLink
{
    PinID fromPinID; // Output
    PinID toPinID;   // Input
};
