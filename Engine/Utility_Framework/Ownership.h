#pragma once

// One reviewed header and its default configuration across every engine module.
// Keep own handles inside the engine C++ boundary, never in the managed/C ABI.
#include "../../ThirdParty/ownership_cpp/include/own/ownership.hpp"
