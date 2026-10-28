#include "imgui.h"
#include "imgui_internal.h"

// AArch64の実layoutをnmで測る。実行用allocatorやRendererではない。
extern "C" const unsigned char yw1_imgui_context_size[sizeof(ImGuiContext)]{};
