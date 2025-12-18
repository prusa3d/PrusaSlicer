#!/usr/bin/env python3
"""
Convert Fiber Printing Mermaid diagrams to PNG images.
Extracts Mermaid code blocks from markdown files in doc/fiber/
and converts them using mmdc (Mermaid CLI).
"""

import subprocess
import re
import os
from pathlib import Path


def extract_all_mermaid_from_markdown(markdown_file):
    """Extract all Mermaid code blocks from markdown file with their section titles."""
    with open(markdown_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    results = []
    current_section = None
    in_mermaid_block = False
    mermaid_lines = []
    
    for i, line in enumerate(lines):
        # Check for section headings (## or ###)
        if line.startswith('##'):
            current_section = line.strip('# \n')
        
        # Check for start of Mermaid block
        if line.strip() == '```mermaid':
            in_mermaid_block = True
            mermaid_lines = []
            continue
        
        # Check for end of Mermaid block
        if in_mermaid_block and line.strip() == '```':
            mermaid_code = '\n'.join(mermaid_lines).strip()
            if mermaid_code:
                results.append((mermaid_code, current_section))
            in_mermaid_block = False
            mermaid_lines = []
            continue
        
        # Collect Mermaid code
        if in_mermaid_block:
            mermaid_lines.append(line.rstrip())
    
    return results


def get_diagram_title(mermaid_code, index, section_title=None):
    """Extract or generate a title for the diagram."""
    # Use section title if provided (from markdown heading before diagram)
    if section_title:
        # Clean up section title for filename
        clean_title = section_title.lower().replace(' ', '_').replace(':', '').replace('-', '_')
        clean_title = ''.join(c for c in clean_title if c.isalnum() or c == '_')
        return clean_title[:50]  # Limit length
    
    # Try to find a title in comments or first line
    lines = mermaid_code.split('\n')
    for line in lines[:5]:
        if 'flowchart' in line.lower():
            return f"flowchart_{index}"
        elif 'classDiagram' in line.lower() or 'classDiagram' in line:
            return f"class_diagram_{index}"
        elif 'graph' in line.lower():
            return f"graph_{index}"
    
    # Default naming
    return f"diagram_{index}"


def mermaid_to_png(input_file, output_file, scale=3):
    """Convert Mermaid file to PNG using mmdc."""
    try:
        subprocess.run([
            "mmdc",
            "-i", str(input_file),
            "-o", str(output_file),
            "-s", str(scale),
            "--backgroundColor", "white"
        ], check=True, capture_output=True, text=True)
        print(f"  ✓ Converted {input_file.name} → {output_file.name}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"  ✗ Error converting {input_file.name}: {e}")
        if e.stderr:
            print(f"    {e.stderr}")
        return False
    except FileNotFoundError:
        print(f"  ✗ Error: mmdc not found. Install with: npm install -g @mermaid-js/mermaid-cli")
        return False


def process_markdown_file(md_file, images_dir, scale=3):
    """Process a markdown file and extract all Mermaid diagrams."""
    md_path = Path(md_file)
    
    if not md_path.exists():
        print(f"⚠ Warning: {md_path} not found, skipping...")
        return []
    
    print(f"\nProcessing {md_path.name}...")
    
    # Extract all Mermaid diagrams
    mermaid_blocks = extract_all_mermaid_from_markdown(md_path)
    
    if not mermaid_blocks:
        print(f"  No Mermaid diagrams found in {md_path.name}")
        return []
    
    print(f"  Found {len(mermaid_blocks)} diagram(s)")
    
    generated_files = []
    base_name = md_path.stem  # filename without extension
    
    # Create temporary directory for .mmd files
    temp_dir = images_dir / "temp"
    temp_dir.mkdir(exist_ok=True)
    
    for idx, (mermaid_code, section_title) in enumerate(mermaid_blocks, 1):
        # Generate filename using section title if available
        diagram_title = get_diagram_title(mermaid_code, idx, section_title)
        mmd_file = temp_dir / f"{base_name}_{diagram_title}.mmd"
        png_file = images_dir / f"{base_name}_{diagram_title}.png"
        
        # Save Mermaid code to .mmd file
        with open(mmd_file, 'w', encoding='utf-8') as f:
            f.write(mermaid_code)
        
        # Convert to PNG
        if mermaid_to_png(mmd_file, png_file, scale=scale):
            generated_files.append(png_file)
    
    return generated_files


def main():
    """Main function to process all Mermaid diagrams in doc/fiber/."""
    
    print("=" * 70)
    print("Fiber Printing Mermaid Diagram Converter")
    print("=" * 70)
    
    # Set up directories
    script_dir = Path(__file__).parent
    fiber_doc_dir = script_dir  # doc/fiber/
    images_dir = fiber_doc_dir / "images"
    images_dir.mkdir(exist_ok=True)
    
    print(f"\nSource directory: {fiber_doc_dir.absolute()}")
    print(f"Output directory: {images_dir.absolute()}")
    
    # Find all markdown files in doc/fiber/
    md_files = list(fiber_doc_dir.glob("*.md"))
    
    if not md_files:
        print("\n⚠ No markdown files found in doc/fiber/")
        return
    
    print(f"\nFound {len(md_files)} markdown file(s)")
    
    # First, show what diagrams will be generated
    print("\n" + "=" * 70)
    print("Diagrams to be generated:")
    print("=" * 70)
    for md_file in md_files:
        if md_file.name == "README.md":
            continue
        mermaid_blocks = extract_all_mermaid_from_markdown(md_file)
        if mermaid_blocks:
            print(f"\n{md_file.name}: {len(mermaid_blocks)} diagram(s)")
            for idx, (code, title) in enumerate(mermaid_blocks, 1):
                diagram_name = get_diagram_title(code, idx, title)
                print(f"  {idx}. {title or 'Untitled'} → {md_file.stem}_{diagram_name}.png")
    print("=" * 70)
    
    all_generated = []
    
    # Process each markdown file
    for md_file in md_files:
        if md_file.name == "README.md":  # Skip README if exists
            continue
        
        generated = process_markdown_file(md_file, images_dir, scale=3)
        all_generated.extend(generated)
    
    # Clean up temporary .mmd files
    temp_dir = images_dir / "temp"
    if temp_dir.exists():
        for mmd_file in temp_dir.glob("*.mmd"):
            mmd_file.unlink()
        temp_dir.rmdir()
        print(f"\n✓ Cleaned up temporary files")
    
    # Summary
    print("\n" + "=" * 70)
    if all_generated:
        print(f"✓ Successfully generated {len(all_generated)} PNG image(s):")
        for png_file in all_generated:
            print(f"  - {png_file.name}")
    else:
        print("⚠ No diagrams were converted.")
        print("\nMake sure:")
        print("  1. Mermaid CLI is installed: npm install -g @mermaid-js/mermaid-cli")
        print("  2. Markdown files contain ```mermaid code blocks")
    
    print("=" * 70)
    
    if not all_generated:
        print("\nInstallation instructions:")
        print("  npm install -g @mermaid-js/mermaid-cli")
        print("\nOr use Docker:")
        print("  docker run --rm -v $(pwd):/data minlag/mermaid-cli -i /data/input.mmd -o /data/output.png")


if __name__ == "__main__":
    main()

