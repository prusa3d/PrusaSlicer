#pragma once

#include <map>
#include <vector>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

struct NSVGimage;

namespace Slic3r::App::WX {

class BitmapCache
{
public:
	BitmapCache();
	~BitmapCache() { clear(); }
	void 			clear();
	double			scale() { return m_scale; }

	wxBitmapBundle* 	  find_bndl(const std::string &name)		{ auto it = m_bndl_map.find(name); return (it == m_bndl_map.end()) ? nullptr : it->second; }
	const wxBitmapBundle* find_bndl(const std::string &name) const	{ return const_cast<BitmapCache*>(this)->find_bndl(name); }
	wxBitmap* 		find(const std::string &name) 		{ auto it = m_map.find(name); return (it == m_map.end()) ? nullptr : it->second; }
	const wxBitmap* find(const std::string &name) const { return const_cast<BitmapCache*>(this)->find(name); }

	wxBitmapBundle*			insert_bndl(const std::string& bitmap_key, const char* data, size_t width, size_t height);
	wxBitmapBundle* 		insert_bndl(const std::string& bitmap_key, const wxBitmapBundle &bmp);
	wxBitmapBundle*			insert_bndl(const std::string& bitmap_key, const wxVector<wxBitmap>& bmps);
	wxBitmapBundle*			insert_bndl(const std::string& name, const std::vector<wxBitmapBundle*>& bmps);
	wxBitmapBundle* 		insert_raw_rgba_bndl(const std::string &bitmap_key, unsigned width, unsigned height, const unsigned char *raw_data, const bool grayscale = false);

	wxBitmap*       insert(const std::string &name, size_t width, size_t height, double scale = -1.0);
	wxBitmap* 		insert(const std::string &name, const wxBitmap &bmp);
	wxBitmap* 		insert_raw_rgba(const std::string &bitmap_key, unsigned width, unsigned height, const unsigned char *raw_data, const bool grayscale = false);

	// Load png from resources/icons. bitmap_key is given without the .png suffix. Bitmap will be rescaled to provided height/width if nonzero.
    wxBitmap* 		load_png(const std::string &bitmap_key, unsigned width = 0, unsigned height = 0, const bool grayscale = false);

	// Parses SVG file from a file, returns SVG image as paths.
	// And makes replases befor parsing
	// replace_map containes old_value->new_value
	static NSVGimage* nsvgParseFromFileWithReplace(const char* filename, const char* units, float dpi, const std::map<std::string, std::string>& replaces);
	// Gets a data from SVG file and makes replases
	// replace_map containes old_value->new_value
    static void		nsvgGetDataFromFileWithReplace(const char* filename, std::string& data_str, const std::map<std::string, std::string>& replaces);
	wxBitmapBundle* from_svg(const std::string& bitmap_name, unsigned target_width, unsigned target_height, const bool dark_mode, const std::string& new_color = "");
	wxBitmapBundle* from_png(const std::string& bitmap_name, unsigned width, unsigned height);
	// Load svg from resources/icons. bitmap_key is given without the .svg suffix. SVG will be rasterized to provided height/width.
    wxBitmap* 		load_svg(const std::string &bitmap_key, unsigned width = 0, unsigned height = 0, const bool grayscale = false, const bool dark_mode = false, const std::string& new_color = "");

	// Create wxBitmapBundle from SVG and replace light colors with neutral gray
	// to improve contrast in native macOS menu items.
    wxBitmapBundle* from_svg_for_mac_menu(const std::string& bitmap_name, unsigned target_width, unsigned target_height);

	wxBitmapBundle	mksolid(size_t width, size_t height, unsigned char r, unsigned char g, unsigned char b, unsigned char transparency, size_t border_width = 0, bool dark_mode = false);
	wxBitmapBundle*	mksolid_bndl(size_t width, size_t height, const std::string& color = std::string(), size_t border_width = 0, bool dark_mode = false);
	wxBitmapBundle* mkclear_bndl(size_t width, size_t height) { return 	mksolid_bndl(width, height); }

private:
    std::map<std::string, wxBitmap*>	m_map;
    std::map<std::string, wxBitmapBundle*>	m_bndl_map;
    double	m_gs	= 0.2;	// value, used for image.ConvertToGreyscale(m_gs, m_gs, m_gs)
	double	m_scale = 1.0;	// value, used for correct scaling of SVG icons on Retina display
};

} // namespace Slic3r::App::WX 
