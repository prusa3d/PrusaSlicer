# Low level rendering layer

The `Slic3r::App::Render` namespace contains low-level rendering 
classes providing *Rendering API Agnostic* interface with compile-time
polymorphism. 

To make Slic3r render API agnostic, only classes of this namespace
can do calls to OpenGL (or Metal in future), the rest of the app
has to use this low-level rendering API (i.e. no OpenGL calls are 
allows), or higher-level API like scene-graph in `Slic3r::App::Scene`
(which in turn uses this API).

## Design Goals

1. Separation of render API agnostic interface and render API 
   specific implementation.
2. Start with minimal API and grow it according to needs. 
   We don't need to have all features the rendering API provides. 
   Only the sub-set we need for our implementation. 
   Implement addition features when needed only. 
3. Provide minimal helper classes like Geometry or Image to simplify 
   common tasks.


## Compile-time "polymorphism"

At the moment there is only one Rendring API implementation,
namely the OpenGL one. Once we will be adding other Rendering
API support (like Apple Metal), for each RenderAPI specific 
class there will be its Metal implementation alongside 
the OpenGL.

The implementation will be chosen at compile time, 
which makes 
- header files providing cross-API interface, and 
- cpp files providing render API specific implementation

The **benefit** of this approach is that it will mitigate the need
of virtual call for all rendering specific API.

The **downside** is that only one render API implementation will
be present in the runtime.

