#include "ExternUI.h"
#include "InputGraphWindow.h"
#include "InputSessionComponent.h"

void ImGuiDrawHelperInputSession(InputSessionComponent* input)
{
    if (input)
    {
        editor::input_editing::DrawInspector(*input);
    }
}
