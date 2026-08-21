/* freetype_stub.c — host-only stubs for FreeType (link-test imgui backend).
 * Includes the REAL FreeType headers so struct layouts match exactly;
 * stubs only the function bodies with trivial return values.
 * NEVER builds into ISO; only `make hostcheck`. */
#include <ft2build.h>
#include FT_FREETYPE_H

FT_Error FT_Init_FreeType(FT_Library* alibrary) {
    *alibrary = (FT_Library)0x1; return 0;
}
FT_Error FT_Done_FreeType(FT_Library library) {
    (void)library; return 0;
}
FT_Error FT_New_Memory_Face(FT_Library library, const FT_Byte* file_base,
                            FT_Long file_size, FT_Long face_index,
                            FT_Face* aface) {
    (void)library;(void)file_base;(void)file_size;(void)face_index;
    static FT_FaceRec rec; memset(&rec, 0, sizeof(rec));
    *aface = &rec;
    return 0;
}
FT_Error FT_Done_Face(FT_Face face) { (void)face; return 0; }

FT_Error FT_Set_Pixel_Sizes(FT_Face face, FT_UInt pixel_width, FT_UInt pixel_height) {
    (void)pixel_width;
    /* give every glyph a 12x12 blank bitmap, advance 12px, so the
       atlas baker in imguitest produces a valid (if empty) atlas. */
    face->size = (void*)0x1;  /* non-NULL so Set_Pixel_Sizes "succeeds" */
    return 0;
}

FT_Error FT_Load_Char(FT_Face face, FT_ULong char_code, FT_Int load_flags) {
    (void)char_code; (void)load_flags;
    /* The glyph slot must have a bitmap for the baker to read.
       Point glyph->bitmap at a static blank buffer. */
    static FT_Byte blank[64*64];
    memset(blank, 0, sizeof(blank));
    if (face->glyph) {
        face->glyph->bitmap.width = 12;
        face->glyph->bitmap.rows  = 12;
        face->glyph->bitmap.pitch  = 12;
        face->glyph->bitmap.buffer = blank;
        face->glyph->bitmap_left   = 0;
        face->glyph->bitmap_top    = 12;
        face->glyph->advance.x     = 12 << 6;  /* 12px * 64 */
        face->glyph->advance.y     = 0;
    }
    return 0;
}

/* app reads these for logging; stubs are fine */
const char* ft_error_string(FT_Error e) { (void)e; return "ft stub"; }
