#pragma once

#include <d3d9.h>

class D3D9Hooks
{
  public:
    static void Hook(HMODULE dx9Module);
    static void Unhook();
};
