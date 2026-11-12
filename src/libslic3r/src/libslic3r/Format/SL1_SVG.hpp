/////|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros
/////|/
/////|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
/////|/
#ifndef SL1_SVG_HPP
#define SL1_SVG_HPP

#include "SL1.hpp"

namespace Slic3r {

//void store_sl1_svg(const std::string& file_path, const Biz::Slicing::SLAResult& data) { store_sl1(file_path, data);}

/// <summary>
/// Factory for create file rasterizer for the '.sl1' file format
/// </summary>
/// <param name="cfg">Printer configuration like resolution, dimension etc</param>
/// <returns>Copyable(for each layer) rasterizer</returns>
std::unique_ptr<ISlaRasterizer> create_sl1_svg_rasterizer(const SLAPrintConfigView& cfg);

} // namespace Slic3r

#endif // SL1_SVG_HPP
