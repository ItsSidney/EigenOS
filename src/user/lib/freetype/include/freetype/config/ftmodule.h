/*
 * Minimal FreeType module list for EigenOS ring-3 builds.
 *
 * Only the modules required to rasterize TrueType (glyf) outlines are enabled.
 * This avoids pulling in SVG/PFR/BDF/PCF/Type1/CFF/etc. which would require
 * external libraries (libpng, libxml, etc.) we do not ship.
 *
 * Build with FT_CONFIG_MODULES_H pointing at this file (it is the default
 * <freetype/config/ftmodule.h> shipped in this vendored tree).
 */

FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )

/* EOF */
