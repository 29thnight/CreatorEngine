#include "../../Engine/RenderEngine/Effects/IEffectSystem.h"
#include <stdio.h>

// Link this same consumer with/without the actual Effect archive. In particular,
// /GL + /LTCG + /OPT:REF must retain registration through the production anchor.
int main(int argc, char** argv)
{
    const bool expected = argc == 2 && std::string(argv[1]) == "included";
    if ((ce::effects::RegisteredFactory() != nullptr) != expected) return 1;
    std::string error;
    {
        ce::effects::Session session;
        if (!session.Initialize(error) || (session.Get() != nullptr) != expected) return 2;
    }
    std::puts(expected ? "EFFECT_OPTIMIZED_ARCHIVE_OK included" : "EFFECT_OPTIMIZED_ARCHIVE_OK excluded");
}
