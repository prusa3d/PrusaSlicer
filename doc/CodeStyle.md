# Code Style

PrusaSlicer is a project that has its history. The coding style is not at all consistent among the source files and we are not especially worried about it, as long as the code works and is readable. All new code has to comply with this document, though.

## C++

We use **C++20** standard, but we need to use only such features that are supported on all platforms. There are some limitations 
in the implementation by AppleClang (e.g. missing `std::stop_token`) or MSVC.


## General C++ guidelines
Consider going through [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#main) by B. Stroustrup & H. Sutter. We are all learning. To highlight what we consider important for pull requests revisions:

- Use `const`. A method that can be `const` should be. A reference or pointer that can be `const` **absolutely** should be. It is not needed to make every possible local variable `const`, although it might improve readability in some cases (and make it worse in others). Avoid `const_cast` and `mutable`.
- Prefer const member functions over non-const member functions and static non-member functions over them.
- Use [RAII](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-raii)
- Use modern C++, but not at all costs
- Only use `auto` where it helps:
```
    auto some_var = function_i_never_heard_of();   // BAD  (type not immediately obvious)
    double some_var = function_i_never_heard_of(); // GOOD (the two extra letters won't hurt)
    
    std::vector<const ExtrusionEntityCollection*>::const_iterator eec_it = ee_collections.begin(); // BAD  (way too verbose)
    auto eec_it = ee_collections.cbegin();         // GOOD (the type is clear enough)
```
- Don't homebrew what is already implemented in PrusaSlicer or some library it depends on (STL, Eigen, Clipper, boost, etc.)
- Prefer references over pointers.
- Prefer using `std::unique_ptr` over raw pointers.

## Directory structure and files

- Used file extensions are `.hpp` for headers and `.cpp` for C++ sources.
- Every library within Prusa Slicer have to follow this directory structure:
  - `include` directory containing all PUBLIC headers
  - `src` directory containing all code (including private headers)
  - `test` directory containing all code and data for unit tests
- The directory structure in `include` and `src` must mirror the namespace location. It means that file containing
  `Slic3r::Domain::Project` is located in `include/Slic3r/Domnain/Project.hpp` and `src/Slic3r/Domain/Project.cpp` 
  files.

Example directory structure of `slic3r-shared` library (see `../src/slic3r-shared`):

```
slic3r-shared
├── CMakeLists.txt
├── README.md
├── include
│   └── Slic3r
│       ├── App
│       │   ├── Color.hpp
│       │   ├── ...
│       ├── Biz
│       │   ├── IProjectsChangedListener.hpp
│       │   ├── ...
│       ├── Domain
│       │   ├── Bed.hpp
│       │   ├── ...
│       └── StringUtils.hpp
├── src
│   └── Slic3r
│       ├── App
│       │   ├── Color.cpp
│       │   ├── ...
│       ├── Biz
│       │   ├── ...
│       └── Domain
│           ├── Bed.cpp
│           ├── ...
└── test
    ├── Slic3r
    │   ├── App
    │   │   └── Scene
    │   │       ├── Node.cpp
    │   │       └── Ray.cpp
    │   └── Biz
    │       └── ProjectInteractorTests.cpp
    └── data
        ├── ...
```

## General code styling

- Four spaces for indentation, no tabs
- Keep lines reasonably short - if you exceeded 80 chars, it is usually time to start thinking about a line break (unless the code is heavily indented - use your own judgement)
- Names of variables and functions should be meaningful. The longer the scope of a variable, the more descriptive name it typically needs.

## Naming conventions

### class / struct

  - Names are in `PascalCase`
  - method & variable names: `snake_case`
  - member variables are prepended by `m_` (except for simple POD structs)
  - enum constants: `PascalCase`
  
  ```c++
  class MyWindow : public BaseClass 
  {
  public:
      void my_method(int input_parameter) const;
  public:
      int public_member_var;
  private:
      int m_non_public_member_var;
      static int s_static_var;
  }
  ```

### Enums:

  - use `enum class` unless there is a strong reason against it
  - use `PascalCase` for name and all its constants
 
  ```c++
  enum class BufferUsage {
      StaticDraw,
      DynamicDraw,
      StreamDraw
  };

  ```
  
## Braces

### class/struct/enum and function body

Braces on a separate line:

```c++
class MyClass
{
    
}
```
### control structures (if/for/while/switch) and namespace decls:

Opening brace at the end of same line:

```c++
if (expr) {
    
}
```

## Variables and types

Pointer/reference symbol placement as part of the type, space before identifier:

  ```
  int* ptr_buffer;
  const std::string& ref_string;
  ```

Avoid multi-var def at one line:

```
// DO:
int a=0;
int b=1;

// DON'T:
int a=0, b=1;
```

## Namespaces

Concat nested namespaces:

```
// DO
namespace Slic3r::GUI {

}

// DON'T
namespace Slic3r {
    namespace GUI {
    
    }
}
```

## Documentation comments

- We use [doxygen](https://www.doxygen.nl/manual/docblocks.html)
- We use this type of comment blocks`

  ```c++
  /**
   * Documentation goes here...
   */
  ```
  
- We use doxygen commands prefixed with `@` (e.g. `@brief`)
- We don't use automatic brief generation form first sentence, you have to explicitly 
  denote the brief part (one sentence shown in class/module table of contents) with `@brief` like this:
  
  ```c++
   /**
    * @brief Scenegraph entrypoint
    *
    * Encapsulate scene tree made of Node and provides:
    * - rendering of 3D object, see render()
    * - rendering of 2D GUI overlay, see render_imgui()
    * - camera used for rendering (camera()) and its trackball manipulator (camera_trackball())
    * - ray picking, see pick_at()
    * - also provides common resource caching manager for rendering geometry (geometry_manager()),
    *   and in-memory geometry (triangle_mesh_manager())
    * .
    */
  ```
  
- Note that doxygen [supports subset of markdown](https://www.doxygen.nl/manual/markdown.html) including lists. 
  But note that **lists needs to be explicitly terminated by line with dot** (see the previous example and last doxygen line)


