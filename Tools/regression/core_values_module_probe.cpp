#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#include "../../Engine/SceneRuntime/KeyBitFlag.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Uuid.h"
#include "../build/ModuleAdapters/Creator.Core.Layers.h"
#include "../build/ModuleAdapters/Creator.Core.BitFlag.h"
#else
import Creator.Core.Uuid;
import Creator.Core.Layers;
import Creator.Core.BitFlag;
#endif

int RunStringStorageContract();
uuid::uuid16 EchoCoreUuid(uuid::uuid16 value);
std::string EchoCoreString(std::string value);
ce::layers::layer_slot EchoCoreLayerSlot(ce::layers::layer_slot value);
bit_flag EchoCoreBitFlag(bit_flag value);

namespace
{
    int failures = 0;

    void Check(bool value, const char* label)
    {
        if (!value)
        {
            std::fprintf(stderr, "[CORE VALUES] FAIL: %s\n", label);
            ++failures;
        }
    }
}

int main()
{
    static_assert(sizeof(uuid::uuid16) == 16);
    static_assert(std::is_trivially_copyable_v<uuid::uuid16>);
    static_assert(sizeof(bit_flag) == sizeof(unsigned int));
    static_assert(std::is_same_v<flag, unsigned int>);

    const auto dns = uuid::parse("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
    const auto uuid = uuid::from_name(dns, "www.widgets.com");
    Check(uuid::to_string(uuid) == "21f7f8de-8051-5b89-8680-0195ef798b6a", "RFC UUID v5 vector");
    Check(EchoCoreUuid(uuid) == uuid, "UUID type identity across translation units");
    uuid::uuid16 parsed;
    Check(uuid::try_parse("{21F7F8DE80515B8986800195EF798B6A}", parsed) && parsed == uuid,
          "uppercase compact UUID with braces");
    Check(!uuid::try_parse("21f7-f8de80515b8986800195ef798b6a", parsed) && parsed == uuid,
          "malformed UUID preserves output");
    bool threw = false;
    try
    {
        (void)uuid::parse("");
    }
    catch (const std::runtime_error& error)
    {
        threw = std::string_view(error.what()) == "Invalid UUID string at position 0: unexpected end of input";
    }
    Check(threw, "UUID exception type and message");

    const auto slot = ce::layers::layer_slot::Make(31);
    Check(slot && slot->Mask() == 0x80000000u && EchoCoreLayerSlot(*slot) == *slot, "layer slot 31 and type identity");
    Check(!ce::layers::layer_slot::Make(32), "layer slot upper bound");
    Check(ce::layers::ValidLayerName("Player") && !ce::layers::ValidLayerName(""), "layer names");
    Check(!ce::layers::ValidLayerName(std::string_view("a\0b", 3)), "layer embedded NUL");
    Check(!ce::layers::ValidLayerName("\xc0\xaf"), "layer malformed UTF-8");

    bit_flag bits;
    bits.set(0);
    bits.set(31);
    Check(bits.test(0) && bits.test(31), "bit flag edge bits");
    bits.toggle(0);
    bits.clear(31);
    Check(static_cast<flag>(bits) == 0, "bit flag toggle and clear");
    bits |= 7;
    Check(EchoCoreBitFlag(bits).test(7), "bit flag operators and type identity");

    static_assert(sizeof(key_bit_flag) == 32);
    static_assert(sizeof(mouse_bit_flag) == 4);
    static_assert(sizeof(gamepad_bit_flag) == 4);
    key_bit_flag keyboard;
    for (const auto key : {0u, 31u, 32u, 127u, 255u})
    {
        keyboard.set(static_cast<std::uint8_t>(key));
        Check(keyboard.test(static_cast<std::uint8_t>(key)), "keyboard chunk boundary");
    }
    keyboard.reset();
    for (std::uint32_t key = 0; key < 256; ++key)
    {
        Check(!keyboard.test(static_cast<std::uint8_t>(key)), "keyboard reset");
    }
    mouse_bit_flag mouse;
    mouse.set(2);
    Check(mouse.test(2), "mouse utility set/test");
    mouse.clear(2);
    Check(!mouse.test(2), "mouse utility clear");
    gamepad_bit_flag pad;
    pad.set(31);
    Check(pad.test(31), "gamepad upper bit");
    pad.reset();
    Check(!pad.test(31), "gamepad reset");

    const std::string name("module identity");
    Check(EchoCoreString(name) == name, "std::string type identity across translation units");
    const int storageResult = RunStringStorageContract();
    std::printf("[CORE VALUES] failures=%d storage=%d\n", failures, storageResult);
    return failures == 0 && storageResult == 0 ? 0 : 1;
}
