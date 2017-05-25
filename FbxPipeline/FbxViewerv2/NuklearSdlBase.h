#pragma once

#include <SDL.h>
#include <IAppSurface.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include <nuklear.h>

namespace apemode {
    class NuklearSdlBase {
    public:
        enum Theme { Black, White, Red, Blue, Dark };

        struct Vertex {
            float   position[ 2 ];
            float   uv[ 2 ];
            nk_byte col[ 4 ];
        };

        nk_context           Context;
        IAppSurface *        pSurface;
        void *               pRenderAssets;
        nk_draw_null_texture NullTexture;
        nk_font *            pDefaultFont;
        nk_font_atlas        Atlas;
        nk_buffer            RenderCmds;

    public:
        nk_context *  Init( IAppSurface *win );
        void          FontStashBegin( nk_font_atlas **atlas );
        void          FontStashEnd( );
        int           HandleEvent( SDL_Event *evt );
        void          Shutdown( );
        void          SetStyle( Theme theme );

    public:
        virtual void  Render( nk_anti_aliasing, int max_vertex_buffer, int max_element_buffer );
        virtual void  DeviceDestroy( );
        virtual void  DeviceCreate( );
        virtual void *DeviceUploadAtlas( const void *image, int width, int height );

    public:
        static void SdlClipbardPaste( nk_handle usr, struct nk_text_edit *edit );
        static void SdlClipbardCopy( nk_handle usr, const char *text, int len );
    };
}