# GCode Metadata specification

This directory contains:
- [JSON schema](gcode_metadata.schema.json) for validating GCode Metadata embedded in ASCII and binary GCode files.

## GCode Metadata purpose

The purpose of GCode Metadata is to describe the specific state the Prusa SLicer created the GCode for, it describes
(besides others) these things:
- Printer HW Configuration:
  - including printer model identification (and its features)
  - attached printing tools (and its features like nozzle_diameter),
  - attached multimaterial units (aka feeders)
  - loaded material description
- Preset settings for individual components (printer, print, tool, material)
- Print statistics like time and filament usage

## Usages of GCode metadata

The GCode metadata are used in following cases:
- in ASCII and binary GCode---to e.g. described the GCode requirements,
- in Prusa Slicer 3MF as part of _config container_ description,
- partially in Connect telemetry (provided to Slicer and as well as interchanged between Connect and printer firmware).  
  In this case the data provided here is just subset of GCode metadata, and containing other telemetry related data 
  fitted into same topology/terminology. 
