# Diagrams in Fiber_Integration_Architecture.md

This file contains **9 Mermaid diagrams** that will be converted to PNG images:

1. **High-Level Architecture Flow** - Shows FFF vs SLA vs Fiber print types
2. **Detailed Fiber Integration Flow** - Complete workflow from STL to G-code
3. **Class Hierarchy Integration** - UML diagram showing class relationships
4. **Data Flow: STL to G-code with Fiber** - Data flow through the system
5. **Integration Points with Existing Code** - What's reused vs what's new
6. **Processing Steps Comparison** - FFF vs SLA vs Fiber side-by-side
7. **File Structure Integration** - Directory structure and file locations
8. **Configuration Flow** - How settings are loaded and applied
9. **G-code Generation Flow** - How G-code is written with fiber commands

## Running the Script

```bash
cd doc/fiber
python3 generate_diagrams.py
```

This will generate 9 PNG files in `doc/fiber/images/`:
- `Fiber_Integration_Architecture_High_Level_Architecture_Flow.png`
- `Fiber_Integration_Architecture_Detailed_Fiber_Integration_Flow.png`
- `Fiber_Integration_Architecture_Class_Hierarchy_Integration.png`
- `Fiber_Integration_Architecture_Data_Flow_STL_to_G_code_with_Fiber.png`
- `Fiber_Integration_Architecture_Integration_Points_with_Existing_Code.png`
- `Fiber_Integration_Architecture_Processing_Steps_Comparison.png`
- `Fiber_Integration_Architecture_File_Structure_Integration.png`
- `Fiber_Integration_Architecture_Configuration_Flow.png`
- `Fiber_Integration_Architecture_G_code_Generation_Flow.png`

## Prerequisites

Install Mermaid CLI:
```bash
npm install -g @mermaid-js/mermaid-cli
```

