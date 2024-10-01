** Dear ImGui is a bloat-free graphical user interface library for C++.**

For more information go to https://github.com/ocornut/imgui

THIS DIRECTORY CONTAINS THE imgui-1.83 ad5d1a8 SOURCE DISTRIBUTION.


Customized with the following commits:
f93d0001baa5443da2c6510d11b03c675e652418
b71d787f695c779e571865d5214d4da8d50aa7c5

imgui_stdlib.h + imgui_stdlib.cpp are move from directory /imgui/misc/cpp/
InputText() wrappers for C++ standard library (STL) type: std::string.
This is also an example of how you may wrap your own similar types.

imstb_truetype.h modification:

Hot fix for open symbolic fonts on windows
62bdfe6f8d04b88e8bd511cd613be80c0baa7f55
Add case STBTT_MS_EID_SYMBOL to swith in file imstb_truetype.h on line 1440.

Hot fix for open curved fonts mainly on MAC
2148e49f75d82cb19dc6ec409fb7825296ed005c
viz. https://github.com/nothings/stb/issues/1296
In file imstb_truetype.h line 1667 change malloc size from:
vertices = (stbtt_vertex *) STBTT_malloc((m + 1) * sizeof(vertices[0]), info->userdata);
to:
vertices = (stbtt_vertex *) STBTT_malloc(m * sizeof(vertices[0]), info->userdata);