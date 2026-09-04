# slic3r-platform-wx

Implementation of [slic3r-platform](../slic3r-platform) runtime using wxWidgets library.
This implementation is intended to be used to implement full-featured desktop application 
using mix of imgui and wx GUI elements to get best of the both worlds.

## Expected inter-lib dependencies

```mermaid
graph RL;
    slic3r-platform
    slic3r-platform-sdl-->slic3r-platform
    slic3r-platform-wx-->slic3r-platform
    style slic3r-platform-wx fill:#f93,stroke:#862,stroke-width:2px
    slic3r-shared-->slic3r-platform
    slic3r-app-webviewer-->slic3r-platform
    slic3r-app-webviewer-->slic3r-platform-sdl
    slic3r-app-webviewer-->slic3r-shared
    slic3r-app-desktop-->slic3r-shared
    slic3r-app-desktop-->slic3r-platform-wx
    slic3r-app-desktop-->slic3r-platform
```

