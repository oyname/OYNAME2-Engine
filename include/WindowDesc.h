#pragma once

#include <string>

struct WindowDesc
{
    int         width      = 1280;
    int         height     = 720;
    std::string title      = "GDX";
    bool        resizable  = true;
    bool        borderless = true;
    bool        fullscreen = false;  
};
