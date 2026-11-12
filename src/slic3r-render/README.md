# slic3r-render

This library provide low-level rendering hardware interface (RHI) abstraction and its implementation in OpenGL. 
In the future we may consider implement other rendering API like Metal. In case upper level components uses 
solely this interface for 3D rendering, these will not need any changes when migrating to other rendering API.

The inspirations for this API are:
- [Facebook IGL](https://github.com/facebook/igl) (used in VR applications)
- [Google Filament](https://github.com/google/filament) (used mostly for mobile apps and WebGL)
- [LLGL: Low level graphics library](https://github.com/LukasBanana/LLGL)
- [Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine): including this related [GameDevelper lengthy write-up](https://www.gamedeveloper.com/programming/designing-a-modern-cross-platform-low-level-graphics-library)