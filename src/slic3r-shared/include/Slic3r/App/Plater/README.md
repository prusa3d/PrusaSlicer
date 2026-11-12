# Slic3r::App::Plater

This namespace accommodates complete plater application layer.

This is basic flow of interaction with _Project_ (or its part like Model) looks like this:

```mermaid
graph TB
    subgraph namespace_App_Plater[Slic3r::App::Plater]
        PlaterRenderModule
        ScenePresenter
        GizmoManager
    end
    subgraph namespace_Biz_Scene[Slic3r::Biz::Scene]
        SceneInteractor
    end
    subgraph namespace_Domain[Slic3r::Domain]
        Project
    end

    PlaterRenderModule --> |event| GizmoManager
    GizmoManager -->|operation invoked| SceneInteractor
    ScenePresenter -.-> |request new frame| PlaterRenderModule
    SceneInteractor --> |modify project| Project
    SceneInteractor -.-> |notify about project change| ScenePresenter
    
```

GizmoManager flow:

```mermaid
graph TB
    PlaterRenderModule --> |Event| GizmoManager
    GizmoManager --> |1| tool[Active Tool]
    GizmoManager --> |2| Selection
    GizmoManager --> |3| QuickDrag
    GizmoManager --> |4| CameraManipulation
```
