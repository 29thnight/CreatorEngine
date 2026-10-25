#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Uuid.h"
#include "../build/ModuleAdapters/Creator.Core.Layers.h"
#include "../build/ModuleAdapters/Creator.Core.BitFlag.h"
#else
import Creator.Core.Uuid;
import Creator.Core.Layers;
import Creator.Core.BitFlag;
#endif

uuid::uuid16 EchoCoreUuid(uuid::uuid16 value) { return value; }
std::string EchoCoreString(std::string value) { return value; }
ce::layers::layer_slot EchoCoreLayerSlot(ce::layers::layer_slot value) { return value; }
bit_flag EchoCoreBitFlag(bit_flag value) { return value; }
