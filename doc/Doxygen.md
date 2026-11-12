# Doxygen guide

## Creating relevant targets

To add a target into doxygen generation, just in your `CMakeLists.txt` call:
```
add_target_to_doxygen(slic3r-shared)
```
(note: replace the `slic3r-shared` with your target).

The `add_target_to_doxygen` is defined in `cmake/modules/DoxygenTarget.cmake` which is included in top-level CMakeLists.txt
file and hence it is available to all CMakeLists.txt files in subdirectories.

This function will for given target:
- extend the target with metadata like found recognized doxygen files (see next section)
- add the target into list of targets which files include in doxygen build

All of these infos are used to generate `slic3r-doxygen` target used to run
generation of all registered library documentation. This  is done by calling add_doxygen_target

## Recognized doxygen files

Following files are processed:
- all source files in `<lib-root>/include` (the default)
- all `*.md` files in `<lib-root>`
- all `*.dox` files in `<lib-root>`

If the source/header files with documentation aren't located in `<lib-root>/include` one can use following form of `add_target_to_doxygen`:

```
add_target_to_doxygen(libslic3r INPUT .)
```

This will all source files in `<lib-root>`.


## Doxygen comment styles

This document outline style of doxygen comments.

In points:
1. Use doxygen commands prefixed with `@` (i.e. use `@brief` instead of `\brief`),
2. Place doxygen comments only into header files
3. Each documented element (class, function, file, global variable etc.) should have at least `@brief` description
4. Use `@param` to document function parameters (if you want to write multiple paragraphs related to specified parameter, 
   enclose the paragraphs within `@parblock` and `@endparblock` commands).
5. Use `@tparam` to doucment template parameters
6. Use `@return` to document
7. Use `@p <param-name>` to reference function parameter from text.
8. If set of free functions is placed in a file, consider using `@file <filename>` section to describe the module, 
   and allow cross-linking by `@ref <filename> "link text"` or `@see <filename>`.
9. For formatting [use makrdown](https://www.doxygen.nl/manual/markdown.html) (instead of doxygen commands)
10. Note that [lists](https://www.doxygen.nl/manual/lists.html) has to be ended with dot on separate line like this:
   ```c++
   /**
    * This is sort of low-level object, for more comfort use
    * - Scene as entry point to scenegraph,
    * - @ref NodeVisitor.hpp "node visitors" to visit and transform (sub-)graph
    * - NodeBuilder to create sub-scenegraph
    * .
    *
    * In terms of tree hierarchy node contains:
    * - parent link: see @ref Node::parent() const
    * - list of children (see Node::children() const
    * .
    */     
   ```
11. If you want to group elements in a structure  (class, struct, enum), you can use `@name`, `@{`, `@}` like this:
    ```c++
    class Node 
    {
        //...
   
        /**
         * @name TransformModifier
         * World Transformation modifier
         * @{
          */
        const INodeTransformModifier* transform_modifier() const { return m_transform_modifier.get(); }
        INodeTransformModifier* transform_modifier() { return m_transform_modifier.get(); }
        void set_transform_modifier(std::unique_ptr<INodeTransformModifier>&& modifier)
        { m_transform_modifier = std::move(modifier); }
        /**@}*/
       
        //...
    }
   
    ```



## Doxygen processing

There is small processing done by CMake integration namely:

1. All source files for given library are enclosed like this:
   ```
   /**
    * @file
    * @addtogroup <library-name>
    * @{
    */
   
   <the actual content of the processed file goes here>
   
   /**
    * @}
    */
   ```
   
   This way all elements of the file are placed into [topic](https://www.doxygen.nl/manual/grouping.html#topics)  
   named after the library the files belong to.  
2. For each library a topic with same name is created (just to make working the topic-file association from previous point).
3. Related `*.md` files are also slightly processed to work better with doxygen, namely:
   1. Fenced code blocks with mermaid diagram markdown as 
      [supported by Github](https://github.blog/developer-skills/github/include-diagrams-markdown-files-mermaid/) 
      are processed so it works also in Doxygen.
   2. All other [fenced code blocks with denoted language](https://www.markdownguide.org/extended-syntax/#syntax-highlighting) 
      for syntax highlighting are processed to strip the language specifier as it is not supported by doxygen 
      (at least not in this form).


