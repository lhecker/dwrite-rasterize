#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <dwrite_3.h>
#include <wincodec.h>

#include <cassert>

#pragma comment(lib, "dwrite.lib")

#define RETURN_IF_FAILED(hr) \
    {                        \
        HRESULT _hr = (hr);  \
        if (FAILED(_hr)) {   \
            return _hr;      \
        }                    \
    }

#define SAFE_RELEASE(ptr) \
    if (ptr) {            \
        ptr->Release();   \
        ptr = nullptr;    \
    }

int wmain(int argc, const wchar_t* argv[])
{
    const bool cleartype = false;
    const wchar_t* font_name = L"Segoe UI";
    const float font_size = 128;
    const float dpi = 96;
    const UINT32 codepoint = U'γ';

    HRESULT hr = S_OK;

    IDWriteFactory3* dwrite_factory = nullptr;
    RETURN_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(dwrite_factory), (IUnknown**)&dwrite_factory));

    IDWriteFontCollection* font_collection = nullptr;
    RETURN_IF_FAILED(dwrite_factory->GetSystemFontCollection(&font_collection, FALSE));

    UINT32 family_index = 0;
    BOOL family_exists = FALSE;
    RETURN_IF_FAILED(font_collection->FindFamilyName(font_name, &family_index, &family_exists));
    if (!family_exists) {
        return E_FAIL;
    }

    IDWriteFontFamily* font_family = nullptr;
    RETURN_IF_FAILED(font_collection->GetFontFamily(family_index, &font_family));

    IDWriteFont* font = nullptr;
    RETURN_IF_FAILED(font_family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font));

    IDWriteFontFace* font_face = nullptr;
    RETURN_IF_FAILED(font->CreateFontFace(&font_face));

    IDWriteFontFace3* font_face2 = nullptr;
    RETURN_IF_FAILED(font_face->QueryInterface(__uuidof(font_face2), (void**)&font_face2));

    UINT16 glyph_indices[1] = {0};
    UINT32 codepoints[1] = {codepoint};
    RETURN_IF_FAILED(font_face->GetGlyphIndicesW(&codepoints[0], 1, &glyph_indices[0]));

    FLOAT glyph_advances[1] = {0};
    DWRITE_GLYPH_OFFSET glyph_offsets[1] = {};
    DWRITE_GLYPH_RUN run = {
        .fontFace = font_face,
        .fontEmSize = font_size * dpi / 72.0f, // Convert font size from points (72 DPI) to pixels
        .glyphCount = 1,
        .glyphIndices = glyph_indices,
        .glyphAdvances = glyph_advances,
        .glyphOffsets = glyph_offsets,
    };

    IDWriteRenderingParams* rendering_params = nullptr;
    RETURN_IF_FAILED(dwrite_factory->CreateRenderingParams(&rendering_params));

    DWRITE_RENDERING_MODE1 rendering_mode = DWRITE_RENDERING_MODE1_DEFAULT;
    DWRITE_GRID_FIT_MODE grid_fit_mode = DWRITE_GRID_FIT_MODE_DEFAULT;
    RETURN_IF_FAILED(font_face2->GetRecommendedRenderingMode(
        /* fontEmSize       */ run.fontEmSize,
        /* dpiX             */ 96.0f,
        /* dpiY             */ 96.0f,
        /* transform        */ nullptr,
        /* isSideways       */ run.isSideways,
        /* outlineThreshold */ DWRITE_OUTLINE_THRESHOLD_ANTIALIASED,
        /* measuringMode    */ DWRITE_MEASURING_MODE_NATURAL,
        /* renderingParams  */ rendering_params,
        /* renderingMode    */ &rendering_mode,
        /* gridFitMode      */ &grid_fit_mode
    ));

    RECT bounds{};
    BYTE* bitmap_data = nullptr;
    size_t bitmap_size = 0;

    // CreateGlyphRunAnalysis does not support DWRITE_RENDERING_MODE_OUTLINE.
    //
    // Option 1: You don't care about super large font sizes (many hundreds of points).
    // You can just force it to use the next best thing: DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC.
    if (rendering_mode == DWRITE_RENDERING_MODE1_OUTLINE) {
        rendering_mode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
    }

    // Option 2: You do care about it and you properly use a IDWriteGeometrySink.
    if (rendering_mode == DWRITE_RENDERING_MODE1_OUTLINE) {
        assert(false); // TODO: Here's where you would create a ID2D1SimplifiedGeometrySink with Direct2D.
        IDWriteGeometrySink* geometry_sink = nullptr;

        RETURN_IF_FAILED(font_face->GetGlyphRunOutline(
            /* emSize        */ run.fontEmSize,
            /* glyphIndices  */ run.glyphIndices,
            /* glyphAdvances */ run.glyphAdvances,
            /* glyphOffsets  */ run.glyphOffsets,
            /* glyphCount    */ run.glyphCount,
            /* isSideways    */ run.isSideways,
            /* isRightToLeft */ run.bidiLevel & 1,
            /* geometrySink  */ geometry_sink
        ));
    } else {
        IDWriteGlyphRunAnalysis* analysis = nullptr;
        RETURN_IF_FAILED(dwrite_factory->CreateGlyphRunAnalysis(
            /* glyphRun         */ &run,
            /* transform        */ nullptr,
            /* renderingMode    */ rendering_mode,
            /* measuringMode    */ DWRITE_MEASURING_MODE_NATURAL,
            /* gridFitMode      */ grid_fit_mode,
            /* antialiasMode    */ cleartype ? DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE : DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
            /* baselineOriginX  */ 0.0f,
            /* baselineOriginY  */ 0.0f,
            /* glyphRunAnalysis */ &analysis
        ));

        const DWRITE_TEXTURE_TYPE type = cleartype ? DWRITE_TEXTURE_CLEARTYPE_3x1 : DWRITE_TEXTURE_ALIASED_1x1;
        RETURN_IF_FAILED(analysis->GetAlphaTextureBounds(type, &bounds));

        // TODO: If the type is DWRITE_TEXTURE_CLEARTYPE_3x1 and the above call failed,
        // it indicates that the font doesn't support DWRITE_TEXTURE_CLEARTYPE_3x1.
        // In that case you should retry with DWRITE_TEXTURE_ALIASED_1x1.
        //
        // TODO: You should skip bitmap generation if the returned bounds are empty (= whitespace).

        bitmap_size = (bounds.right - bounds.left) * (bounds.bottom - bounds.top);
        if (cleartype) {
            bitmap_size *= 3; // 24bpp RGB
        }

        bitmap_data = new BYTE[bitmap_size];
        RETURN_IF_FAILED(analysis->CreateAlphaTexture(type, &bounds, bitmap_data, (UINT32)bitmap_size));
    }

    IWICImagingFactory* wic_factory = nullptr;
    RETURN_IF_FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    RETURN_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_factory)));

    const REFWICPixelFormatGUID format = cleartype ? GUID_WICPixelFormat24bppRGB : GUID_WICPixelFormat8bppGray;
    UINT stride = bounds.right - bounds.left;
    if (cleartype) {
        stride *= 3; // 24bpp RGB
    }

    IWICBitmap* bitmap = nullptr;
    RETURN_IF_FAILED(wic_factory->CreateBitmapFromMemory(
        /* uiWidth      */ bounds.right - bounds.left,
        /* uiHeight     */ bounds.bottom - bounds.top,
        /* pixelFormat  */ format,
        /* cbStride     */ stride,
        /* cbBufferSize */ (UINT)bitmap_size,
        /* pbBuffer     */ bitmap_data,
        /* ppIBitmap    */ &bitmap
    ));

    IWICStream* stream = nullptr;
    RETURN_IF_FAILED(wic_factory->CreateStream(&stream));
    RETURN_IF_FAILED(stream->InitializeFromFilename(L"output.png", GENERIC_WRITE));

    IWICBitmapEncoder* encoder = nullptr;
    RETURN_IF_FAILED(wic_factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    RETURN_IF_FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache));

    IWICBitmapFrameEncode* frame = nullptr;
    RETURN_IF_FAILED(encoder->CreateNewFrame(&frame, nullptr));
    RETURN_IF_FAILED(frame->Initialize(nullptr));
    RETURN_IF_FAILED(frame->WriteSource(bitmap, nullptr));
    RETURN_IF_FAILED(frame->Commit());
    RETURN_IF_FAILED(encoder->Commit());

    return hr;
}
