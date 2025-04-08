#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_2.h>
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
    HRESULT hr = S_OK;
    const wchar_t* font_name = L"Segoe UI";
    const float font_size = 64.0f;
    const float dpi = 96.0f;
    const UINT32 codepoint = U'γ';

    IDWriteFactory2* factory = nullptr;
    RETURN_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(factory), (IUnknown**)&factory));

    IDWriteFontCollection* fontCollection = nullptr;
    RETURN_IF_FAILED(factory->GetSystemFontCollection(&fontCollection, FALSE));

    UINT32 familyIndex = 0;
    BOOL familyExists = FALSE;
    RETURN_IF_FAILED(fontCollection->FindFamilyName(font_name, &familyIndex, &familyExists));
    if (!familyExists) {
        return E_FAIL;
    }

    IDWriteFontFamily* fontFamily = nullptr;
    RETURN_IF_FAILED(fontCollection->GetFontFamily(familyIndex, &fontFamily));

    IDWriteFont* font = nullptr;
    RETURN_IF_FAILED(fontFamily->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font));

    IDWriteFontFace* fontFace = nullptr;
    RETURN_IF_FAILED(font->CreateFontFace(&fontFace));

    UINT16 glyphIndices[1] = {0};
    UINT32 codePoints[1] = {codepoint};
    RETURN_IF_FAILED(fontFace->GetGlyphIndicesW(&codePoints[0], 1, &glyphIndices[0]));

    FLOAT glyphAdvances[1] = {0};
    DWRITE_GLYPH_OFFSET glyphOffsets[1] = {};
    DWRITE_GLYPH_RUN run = {
        .fontFace = fontFace,
        .fontEmSize = font_size,
        .glyphCount = 1,
        .glyphIndices = glyphIndices,
        .glyphAdvances = glyphAdvances,
        .glyphOffsets = glyphOffsets,
    };

    IDWriteRenderingParams* rendering_params = nullptr;
    RETURN_IF_FAILED(factory->CreateRenderingParams(&rendering_params));

    DWRITE_RENDERING_MODE renderingMode = DWRITE_RENDERING_MODE_DEFAULT;
    RETURN_IF_FAILED(fontFace->GetRecommendedRenderingMode(run.fontEmSize, dpi / 96.0f, DWRITE_MEASURING_MODE_NATURAL, rendering_params, &renderingMode));

    RECT bounds{};
    BYTE* bitmap = nullptr;
    size_t bitmap_size = 0;

    if (renderingMode == DWRITE_RENDERING_MODE_OUTLINE) {
        // TODO: Here's where you would create a ID2D1SimplifiedGeometrySink with Direct2D.
        // ...I guess? I find DWRITE_RENDERING_MODE_OUTLINE a little confusing.
        // Fact is, DWRITE_RENDERING_MODE_OUTLINE does not work with CreateGlyphRunAnalysis.
        assert(false);
        IDWriteGeometrySink* geometrySink = nullptr;

        RETURN_IF_FAILED(fontFace->GetGlyphRunOutline(
            /* emSize        */ run.fontEmSize,
            /* glyphIndices  */ run.glyphIndices,
            /* glyphAdvances */ run.glyphAdvances,
            /* glyphOffsets  */ run.glyphOffsets,
            /* glyphCount    */ run.glyphCount,
            /* isSideways    */ run.isSideways,
            /* isRightToLeft */ run.bidiLevel & 1,
            /* geometrySink  */ geometrySink
        ));
    } else {
        IDWriteGlyphRunAnalysis* analysis = nullptr;
        RETURN_IF_FAILED(factory->CreateGlyphRunAnalysis(
            /* glyphRun         */ &run,
            /* transform        */ nullptr,
            /* renderingMode    */ renderingMode,
            /* measuringMode    */ DWRITE_MEASURING_MODE_NATURAL,
            /* gridFitMode      */ DWRITE_GRID_FIT_MODE_DEFAULT,
            /* antialiasMode    */ DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
            /* baselineOriginX  */ 0.0f,
            /* baselineOriginY  */ 0.0f,
            /* glyphRunAnalysis */ &analysis
        ));

        RETURN_IF_FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds));

        bitmap_size = (bounds.right - bounds.left) * (bounds.bottom - bounds.top);
        bitmap = new BYTE[bitmap_size];
        RETURN_IF_FAILED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &bounds, bitmap, (UINT32)bitmap_size));
    }

    IWICImagingFactory* wicFactory = nullptr;
    RETURN_IF_FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    RETURN_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory)));

    IWICBitmap* wicBitmap = nullptr;
    RETURN_IF_FAILED(wicFactory->CreateBitmap(bounds.right - bounds.left, bounds.bottom - bounds.top, GUID_WICPixelFormat8bppGray, WICBitmapCacheOnLoad, &wicBitmap));

    {
        IWICBitmapLock* lock = nullptr;
        WICRect rect = {0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
        RETURN_IF_FAILED(wicBitmap->Lock(&rect, WICBitmapLockWrite, &lock));

        UINT stride = 0;
        UINT bufferSize = 0;
        BYTE* data = nullptr;
        RETURN_IF_FAILED(lock->GetStride(&stride));
        RETURN_IF_FAILED(lock->GetDataPointer(&bufferSize, &data));

        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        for (int y = 0; y < height; y++) {
            memcpy(data + y * stride, bitmap + y * width, width);
        }

        SAFE_RELEASE(lock);
    }

    IWICStream* stream = nullptr;
    RETURN_IF_FAILED(wicFactory->CreateStream(&stream));
    RETURN_IF_FAILED(stream->InitializeFromFilename(L"output.png", GENERIC_WRITE));

    IWICBitmapEncoder* encoder = nullptr;
    RETURN_IF_FAILED(wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    RETURN_IF_FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache));

    IWICBitmapFrameEncode* frame = nullptr;
    RETURN_IF_FAILED(encoder->CreateNewFrame(&frame, nullptr));
    RETURN_IF_FAILED(frame->Initialize(nullptr));
    RETURN_IF_FAILED(frame->WriteSource(wicBitmap, nullptr));
    RETURN_IF_FAILED(frame->Commit());
    RETURN_IF_FAILED(encoder->Commit());

    return hr;
}
