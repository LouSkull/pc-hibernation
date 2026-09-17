#include "cursor.h"
#include <objbase.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <iterator>

namespace CursorArt {
namespace {
// White, Mint, Ice, Violet, Rose, Gold: the clock colours, white pushed a little brighter.
constexpr uint32_t Tints[]={0xF2F6FB,0x97F2CF,0x80D1FF,0xBA9EFF,0xFAA3C9,0xF5CC80};

// Pixel coverage of a shape edge `inside` pixels away (negative when outside), 1 px wide.
float cover(float inside) { return std::clamp(inside+0.5f,0.f,1.f); }

std::wstring extension(const std::wstring& path) {
    std::wstring ext=std::filesystem::path(path).extension().wstring();
    for(auto& c:ext) c=static_cast<wchar_t>(std::towlower(c));
    return ext;
}
bool pngClsid(CLSID& clsid) {
    UINT count=0,size=0; Gdiplus::GetImageEncodersSize(&count,&size);
    if(!size) return false;
    std::vector<BYTE> bytes(size);
    auto* encoders=reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.data());
    if(Gdiplus::GetImageEncoders(count,size,encoders)!=Gdiplus::Ok) return false;
    for(UINT i=0;i<count;i++) if(!wcscmp(encoders[i].MimeType,L"image/png")) { clsid=encoders[i].Clsid; return true; }
    return false;
}
std::string base64(const uint8_t* data,size_t size) {
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve((size+2)/3*4);
    for(size_t i=0;i<size;i+=3) {
        const size_t rest=size-i;
        uint32_t block=static_cast<uint32_t>(data[i])<<16;
        if(rest>1) block|=static_cast<uint32_t>(data[i+1])<<8;
        if(rest>2) block|=data[i+2];
        out+=alphabet[(block>>18)&63]; out+=alphabet[(block>>12)&63];
        out+=rest>1?alphabet[(block>>6)&63]:'=';
        out+=rest>2?alphabet[block&63]:'=';
    }
    return out;
}
// Draws a cursor twice, over black and over white. The difference between the two
// gives the real alpha whatever the cursor format is (alpha, masked or inverting).
bool rasterize(HCURSOR cursor,int size,Image& out) {
    ICONINFO info{};
    if(!GetIconInfo(cursor,&info)) return false;
    BITMAP shape{};
    GetObjectW(info.hbmColor?info.hbmColor:info.hbmMask,sizeof(shape),&shape);
    if(info.hbmColor) DeleteObject(info.hbmColor);
    if(info.hbmMask) DeleteObject(info.hbmMask);
    if(shape.bmWidth<=0) return false;
    out.width=out.height=size;
    out.hotX=std::clamp(MulDiv(static_cast<int>(info.xHotspot),size,shape.bmWidth),0,size-1);
    out.hotY=std::clamp(MulDiv(static_cast<int>(info.yHotspot),size,shape.bmWidth),0,size-1);

    BITMAPINFO format{};
    format.bmiHeader.biSize=sizeof(format.bmiHeader);
    format.bmiHeader.biWidth=size; format.bmiHeader.biHeight=-size;
    format.bmiHeader.biPlanes=1; format.bmiHeader.biBitCount=32; format.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(nullptr);
    void* blackBits=nullptr; void* whiteBits=nullptr;
    HBITMAP black=dc?CreateDIBSection(dc,&format,DIB_RGB_COLORS,&blackBits,nullptr,0):nullptr;
    HBITMAP white=dc?CreateDIBSection(dc,&format,DIB_RGB_COLORS,&whiteBits,nullptr,0):nullptr;
    bool ok=black && white && blackBits && whiteBits;
    if(ok) {
        const size_t bytes=static_cast<size_t>(size)*size*4;
        memset(blackBits,0,bytes); memset(whiteBits,255,bytes);
        HGDIOBJ previous=SelectObject(dc,black);
        ok=DrawIconEx(dc,0,0,cursor,size,size,0,nullptr,DI_NORMAL)!=FALSE;
        SelectObject(dc,white);
        ok=DrawIconEx(dc,0,0,cursor,size,size,0,nullptr,DI_NORMAL)!=FALSE && ok;
        SelectObject(dc,previous);
        GdiFlush();
    }
    if(ok) {
        auto* onBlack=static_cast<const uint8_t*>(blackBits);
        auto* onWhite=static_cast<const uint8_t*>(whiteBits);
        out.pixels.assign(static_cast<size_t>(size)*size,0);
        for(size_t i=0;i<out.pixels.size();i++) {
            const uint8_t* b=onBlack+i*4; const uint8_t* w=onWhite+i*4;
            int spread=0;
            for(int c=0;c<3;c++) spread=std::max(spread,static_cast<int>(w[c])-static_cast<int>(b[c]));
            const int alpha=std::clamp(255-spread,0,255);
            if(!alpha) continue;
            auto channel=[&](int c) { return static_cast<uint32_t>(std::min(255,b[c]*255/alpha)); };
            out.pixels[i]=(static_cast<uint32_t>(alpha)<<24)|(channel(2)<<16)|(channel(1)<<8)|channel(0);
        }
        ok=std::any_of(out.pixels.begin(),out.pixels.end(),[](uint32_t p) { return (p>>24)!=0; });
    }
    if(black) DeleteObject(black);
    if(white) DeleteObject(white);
    if(dc) DeleteDC(dc);
    return ok;
}
bool loadPng(const std::wstring& path,int size,bool centerTip,Image& out) {
    Gdiplus::Bitmap source(path.c_str());
    if(source.GetLastStatus()!=Gdiplus::Ok) return false;
    const UINT w=source.GetWidth(),h=source.GetHeight();
    if(!w || !h || w>4096 || h>4096) return false;
    Gdiplus::Bitmap target(size,size,PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&target);
        g.Clear(Gdiplus::Color(0,0,0,0));
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        // Fit into the square; a centred tip centres the picture, a corner tip pins it there.
        const float scale=std::min(static_cast<float>(size)/w,static_cast<float>(size)/h);
        const float dw=w*scale,dh=h*scale;
        const float x=centerTip?(size-dw)/2:0.f,y=centerTip?(size-dh)/2:0.f;
        // Mirrored edges stop bicubic sampling from pulling a dark fringe in from outside.
        Gdiplus::ImageAttributes edges; edges.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        if(g.DrawImage(&source,Gdiplus::RectF(x,y,dw,dh),0,0,static_cast<Gdiplus::REAL>(w),static_cast<Gdiplus::REAL>(h),
                       Gdiplus::UnitPixel,&edges)!=Gdiplus::Ok) return false;
    }
    Gdiplus::Rect area(0,0,size,size);
    Gdiplus::BitmapData data{};
    if(target.LockBits(&area,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)!=Gdiplus::Ok) return false;
    out.width=out.height=size;
    out.hotX=out.hotY=centerTip?size/2:0;
    out.pixels.assign(static_cast<size_t>(size)*size,0);
    const int stride=std::abs(data.Stride);
    for(int y=0;y<size;y++)
        memcpy(&out.pixels[static_cast<size_t>(y)*size],static_cast<const uint8_t*>(data.Scan0)+static_cast<size_t>(y)*stride,static_cast<size_t>(size)*4);
    target.UnlockBits(&data);
    return std::any_of(out.pixels.begin(),out.pixels.end(),[](uint32_t p) { return (p>>24)!=0; });
}
HCURSOR loadCursorFile(const std::wstring& path,int size) {
    auto cursor=static_cast<HCURSOR>(LoadImageW(nullptr,path.c_str(),IMAGE_CURSOR,size,size,LR_LOADFROMFILE));
    if(!cursor && extension(path)==L".ani") cursor=LoadCursorFromFileW(path.c_str());
    return cursor;
}
}

uint32_t tint(int index) { return Tints[std::clamp(index,0,static_cast<int>(std::size(Tints))-1)]; }

std::string hex(uint32_t rgb) {
    char text[8]; snprintf(text,sizeof(text),"#%06x",rgb&0xFFFFFF); return text;
}

int baseSize(int step) {
    constexpr int sizes[]={24,28,32};
    return sizes[std::clamp(step,0,2)];
}

Image orbit(int size,uint32_t rgb) {
    Image image;
    size=std::clamp(size,12,256);
    image.width=image.height=size;
    image.hotX=image.hotY=size/2;
    image.pixels.assign(static_cast<size_t>(size)*size,0);
    const float s=static_cast<float>(size),centre=s*.5f;
    const float ring=s*.285f;                        // middle of the ring stroke
    const float half=std::max(1.f,s*.046f);          // half the ring width
    const float rim=std::max(.8f,s*.024f);           // dark edge that keeps it visible on bright scenes
    const float dot=std::max(1.3f,s*.058f);          // centre dot
    const float spread=s*.105f;                      // glow falloff
    const float tr=((rgb>>16)&255)/255.f,tg=((rgb>>8)&255)/255.f,tb=(rgb&255)/255.f;
    for(int y=0;y<size;y++) for(int x=0;x<size;x++) {
        const float dx=x+.5f-centre,dy=y+.5f-centre,d=std::sqrt(dx*dx+dy*dy);
        float r=0,g=0,b=0,a=0;                       // premultiplied, composited back to front
        auto over=[&](float cr,float cg,float cb,float ca) {
            if(ca<=0) return;
            r=cr*ca+r*(1-ca); g=cg*ca+g*(1-ca); b=cb*ca+b*(1-ca); a=ca+a*(1-ca);
        };
        // Glow hugging the ring, faded out before the square's edge so no box shows.
        const float fall=(d-ring)/spread;
        const float edge=std::clamp((centre-d)/(s*.07f),0.f,1.f);
        over(tr,tg,tb,.46f*std::exp(-fall*fall)*edge);
        const float band=std::abs(d-ring);
        over(0,0,0,.5f*cover(half+rim-band));
        over(tr,tg,tb,cover(half-band));
        // A thin light line along the inner edge gives the ring some depth.
        over(1,1,1,.5f*cover(half*.34f-std::abs(d-(ring-half*.52f))));
        over(0,0,0,.5f*cover(dot+rim-d));
        over(.72f+.28f*tr,.72f+.28f*tg,.72f+.28f*tb,cover(dot-d));
        if(a<.004f) continue;
        auto byte=[&](float premultiplied) { return static_cast<uint32_t>(std::lround(std::clamp(premultiplied/a,0.f,1.f)*255)); };
        const uint32_t alpha=static_cast<uint32_t>(std::lround(std::clamp(a,0.f,1.f)*255));
        image.pixels[static_cast<size_t>(y)*size+x]=(alpha<<24)|(byte(r)<<16)|(byte(g)<<8)|byte(b);
    }
    return image;
}

bool supportedFile(const std::wstring& path) {
    const auto ext=extension(path);
    return ext==L".png" || ext==L".cur" || ext==L".ani";
}

bool loadFile(const std::wstring& path,int size,bool centerTip,Image& out) {
    out=Image{};
    size=std::clamp(size,12,256);
    if(!supportedFile(path)) return false;
    if(extension(path)==L".png") return loadPng(path,size,centerTip,out);
    HCURSOR cursor=loadCursorFile(path,size);
    if(!cursor) return false;
    const bool ok=rasterize(cursor,size,out);
    DestroyCursor(cursor);
    if(!ok) out=Image{};
    return ok;
}

HCURSOR toCursor(const Image& image) {
    if(image.empty() || image.width<=0 || image.height<=0) return nullptr;
    const int w=image.width,h=image.height;
    BITMAPV5HEADER header{};
    header.bV5Size=sizeof(header); header.bV5Width=w; header.bV5Height=-h;
    header.bV5Planes=1; header.bV5BitCount=32; header.bV5Compression=BI_BITFIELDS;
    header.bV5RedMask=0x00FF0000; header.bV5GreenMask=0x0000FF00;
    header.bV5BlueMask=0x000000FF; header.bV5AlphaMask=0xFF000000;
    HDC screen=GetDC(nullptr);
    void* bits=nullptr;
    HBITMAP color=CreateDIBSection(screen,reinterpret_cast<BITMAPINFO*>(&header),DIB_RGB_COLORS,&bits,nullptr,0);
    ReleaseDC(nullptr,screen);
    if(!color || !bits) { if(color) DeleteObject(color); return nullptr; }
    memcpy(bits,image.pixels.data(),static_cast<size_t>(w)*h*4);
    // The AND mask follows the alpha, so shadows and legacy paths see the real outline.
    const int row=((w+15)/16)*2;
    std::vector<uint8_t> maskBits(static_cast<size_t>(row)*h,0);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++)
        if((image.pixels[static_cast<size_t>(y)*w+x]>>24)==0)
            maskBits[static_cast<size_t>(y)*row+x/8]|=static_cast<uint8_t>(0x80>>(x%8));
    HBITMAP mask=CreateBitmap(w,h,1,1,maskBits.data());
    HCURSOR cursor=nullptr;
    if(mask) {
        ICONINFO info{FALSE,static_cast<DWORD>(std::clamp(image.hotX,0,w-1)),static_cast<DWORD>(std::clamp(image.hotY,0,h-1)),mask,color};
        cursor=static_cast<HCURSOR>(CreateIconIndirect(&info));
        DeleteObject(mask);
    }
    DeleteObject(color);
    return cursor;
}

HCURSOR loadNative(const std::wstring& path,int size,bool centerTip) {
    size=std::clamp(size,12,256);
    if(!supportedFile(path)) return nullptr;
    if(extension(path)!=L".png") return loadCursorFile(path,size);
    Image image;
    return loadPng(path,size,centerTip,image)?toCursor(image):nullptr;
}

std::string pngDataUrl(const Image& image) {
    if(image.empty()) return {};
    CLSID png{};
    if(!pngClsid(png)) return {};
    std::vector<uint32_t> copy=image.pixels;   // GDI+ wants a writable buffer
    Gdiplus::Bitmap bitmap(image.width,image.height,image.width*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(copy.data()));
    IStream* stream=nullptr;
    if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)) || !stream) return {};
    std::string url;
    if(bitmap.Save(stream,&png)==Gdiplus::Ok) {
        LARGE_INTEGER zero{}; ULARGE_INTEGER end{};
        HGLOBAL memory=nullptr;
        if(SUCCEEDED(stream->Seek(zero,STREAM_SEEK_CUR,&end)) && SUCCEEDED(GetHGlobalFromStream(stream,&memory)) && memory) {
            if(auto* data=static_cast<const uint8_t*>(GlobalLock(memory))) {
                url="data:image/png;base64,"+base64(data,static_cast<size_t>(end.QuadPart));
                GlobalUnlock(memory);
            }
        }
    }
    stream->Release();
    return url;
}
}
