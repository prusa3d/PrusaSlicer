# SLM (Selective Laser Melting) Integration Documentation

This directory contains documentation for integrating SLM (metal-based 3D printing) technology into PrusaSlicer.

## 📚 Documentation Files

### Planning & Architecture
- **[SLM_TAB_ARCHITECTURE_DISCUSSION.md](./SLM_TAB_ARCHITECTURE_DISCUSSION.md)** - Detailed architecture discussion and design decisions
- **[SLM_Implementation_Roadmap.md](./SLM_Implementation_Roadmap.md)** - Complete implementation roadmap with all phases
- **[SLM_Roadmap_Quick_Summary.md](./SLM_Roadmap_Quick_Summary.md)** - Quick reference summary of the roadmap

## 🎯 Overview

The goal is to add **SLM as a fourth printing technology** in PrusaSlicer, alongside:
- **FFF** (Fused Filament Fabrication)
- **SLA** (Stereolithography)
- **Fiber** (Fiber Reinforcement)

This integration follows the **SLA integration pattern** and leverages the **libSLM library** for format handling.

## 🚀 Quick Start

1. **Read the Architecture Discussion** - Understand the design decisions
2. **Review the Roadmap** - Understand the implementation phases
3. **Start with Phase 0** - Foundation and structure setup

## 📋 Implementation Phases

### Core Phases (MVP)
- **Phase 0**: Foundation & Architecture Setup
- **Phase 1**: Core Classes & Data Structures
- **Phase 2**: Configuration System
- **Phase 3**: Slicing Integration
- **Phase 4**: libSLM Integration & Conversion Layer ⚠️ Most Complex
- **Phase 5**: Export Functionality
- **Phase 6**: GUI Integration

### Additional Phases
- **Phase 7**: Advanced Features (optional)
- **Phase 8**: Testing & Validation
- **Phase 9**: Documentation & Polish

## 🔑 Key Components

### Core Classes
- `SLMPrint` - Main SLM print class (extends `PrintBase`)
- `SLMPrintObject` - SLM print object (similar to `SLAPrintObject`)
- `SLMExporter` - Conversion layer (PrusaSlicer → libSLM)

### GUI Components
- `TabSLMPrint` - SLM print settings tab
- `TabSLMMaterial` - SLM material tab (optional)

### Configuration
- `SLMPrintConfig` - SLM-specific configuration options
- Preset system integration

## 💡 Key Features

- ✅ Multiple SLM formats (.slm, .mtt, .sli, .cli, .rea)
- ✅ Laser parameters (power, speed, exposure time)
- ✅ Hatch patterns (grid, stripe, custom)
- ✅ Scan strategies (contour-first, hatch-first)
- ✅ Layer parameters (thickness, cooling, addition time)

## 📊 Timeline

**MVP (Phases 0-6):** 8-12 weeks (2-3 months)

**Full Version (All Phases):** 14-18 weeks (3.5-4.5 months)

## 🔧 Dependencies

- **libSLM** - Library for SLM format handling
  - Supports multiple formats (.slm, .mtt, .sli, .cli, .rea)
  - Provides data structures (Layer, Model, Header)
  - Provides format writers

## 📖 Related Documentation

For understanding how similar integrations were done:
- `../fiber/How_SLA_Was_Added_Guide.md` - How SLA was integrated (template)
- `../fiber/Fiber_Implementation_Roadmap.md` - Fiber integration roadmap (reference)

## 🎯 Success Criteria

**MVP Success:**
- Can select SLM printer technology
- Can configure basic SLM parameters
- Can slice a model
- Can export .slm file
- Basic GUI for settings

**Full Success:**
- All SLM parameters configurable
- Multiple formats supported
- Advanced features implemented
- Polished GUI
- Comprehensive documentation

---

*This documentation is for planning and implementation purposes. Details may evolve during development.*

