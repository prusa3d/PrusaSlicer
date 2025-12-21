# Generating Diagram Images

This directory contains Mermaid flowcharts that visualize the fiber printing integration architecture. The diagrams can be viewed directly in markdown (GitHub renders them), or converted to PNG images.

## Quick Start

### Prerequisites

Install Mermaid CLI:
```bash
npm install -g @mermaid-js/mermaid-cli
```

### Generate All Diagrams

Run the script from the `doc/fiber/` directory:

```bash
cd doc/fiber
python3 generate_diagrams.py
```

Or make it executable and run directly:

```bash
chmod +x generate_diagrams.py
./generate_diagrams.py
```

### Output

The script will:
1. Scan all `.md` files in `doc/fiber/`
2. Extract all Mermaid code blocks
3. Convert each diagram to PNG
4. Save images in `doc/fiber/images/`

## What It Does

- **Finds all markdown files** in `doc/fiber/`
- **Extracts Mermaid diagrams** from ` ```mermaid ` code blocks
- **Converts to PNG** using `mmdc` (Mermaid CLI)
- **Saves images** in `doc/fiber/images/` with descriptive names

## File Naming

Images are named based on the source markdown file:
- `Fiber_Integration_Architecture.md` → `Fiber_Integration_Architecture_diagram_1.png`, `Fiber_Integration_Architecture_diagram_2.png`, etc.

## Alternative: Docker

If you don't want to install Node.js/npm, use Docker:

```bash
docker run --rm -v $(pwd):/data minlag/mermaid-cli \
  -i /data/input.mmd \
  -o /data/output.png \
  -s 3 \
  --backgroundColor white
```

## Manual Conversion

To convert a single diagram manually:

1. Extract Mermaid code from markdown
2. Save as `.mmd` file
3. Run:
```bash
mmdc -i diagram.mmd -o diagram.png -s 3 --backgroundColor white
```

## Troubleshooting

### "mmdc not found"
Install Mermaid CLI:
```bash
npm install -g @mermaid-js/mermaid-cli
```

### "No diagrams found"
Make sure markdown files contain ` ```mermaid ` code blocks.

### Permission errors
Make sure the script is executable:
```bash
chmod +x generate_diagrams.py
```

