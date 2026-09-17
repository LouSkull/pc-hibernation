#include "../src/cursor.h"
#include <objbase.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
void check(bool ok,const char* name) { if(!ok) { std::cerr<<name<<'\n'; std::exit(1); } }
uint32_t alphaAt(const CursorArt::Image& image,int x,int y) { return image.pixels[static_cast<size_t>(y)*image.width+x]>>24; }

// A classic .cur file (32-bit DIB plus AND mask) with its own click point.
void writeCur(const std::wstring& path,const CursorArt::Image& image,int hotX,int hotY) {
    const int w=image.width,h=image.height,maskRow=((w+31)/32)*4;
    std::vector<uint8_t> body(40+static_cast<size_t>(w)*h*4+static_cast<size_t>(maskRow)*h,0);
    BITMAPINFOHEADER info{}; info.biSize=40; info.biWidth=w; info.biHeight=h*2; info.biPlanes=1; info.biBitCount=32;
    memcpy(body.data(),&info,40);
    for(int y=0;y<h;y++) {                      // DIB rows run bottom-up
        const size_t row=40+static_cast<size_t>(h-1-y)*w*4;
        memcpy(body.data()+row,&image.pixels[static_cast<size_t>(y)*w],static_cast<size_t>(w)*4);
        for(int x=0;x<w;x++) if(alphaAt(image,x,y)==0)
            body[40+static_cast<size_t>(w)*h*4+static_cast<size_t>(h-1-y)*maskRow+x/8]|=static_cast<uint8_t>(0x80>>(x%8));
    }
    std::ofstream file(path,std::ios::binary);
    const uint16_t dir[]={0,2,1};
    file.write(reinterpret_cast<const char*>(dir),sizeof(dir));
    const uint8_t size[]={static_cast<uint8_t>(w),static_cast<uint8_t>(h),0,0};
    file.write(reinterpret_cast<const char*>(size),sizeof(size));
    const uint16_t hot[]={static_cast<uint16_t>(hotX),static_cast<uint16_t>(hotY)};
    file.write(reinterpret_cast<const char*>(hot),sizeof(hot));
    const uint32_t layout[]={static_cast<uint32_t>(body.size()),22};
    file.write(reinterpret_cast<const char*>(layout),sizeof(layout));
    file.write(reinterpret_cast<const char*>(body.data()),static_cast<std::streamsize>(body.size()));
}
// Zoomed sheet of the built-in pointer over dark and light backdrops, for looking at by eye.
void writePreview(const std::wstring& path) {
    const int zoom=4,cell=64*zoom;
    Gdiplus::Bitmap sheet(cell*6,cell*2,PixelFormat32bppARGB);
    Gdiplus::Graphics g(&sheet);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    for(int row=0;row<2;row++) for(int column=0;column<6;column++) {
        Gdiplus::SolidBrush back(row?Gdiplus::Color(255,226,232,240):Gdiplus::Color(255,12,15,20));
        g.FillRectangle(&back,column*cell,row*cell,cell,cell);
        const int size=column<3?CursorArt::baseSize(column):64;
        auto image=CursorArt::orbit(size,CursorArt::tint(column<3?1:column-3));
        Gdiplus::Bitmap art(size,size,size*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(image.pixels.data()));
        const int drawn=size*zoom*(column<3?2:1);
        g.DrawImage(&art,column*cell+(cell-drawn)/2,row*cell+(cell-drawn)/2,drawn,drawn);
    }
    CLSID png{};
    UINT count=0,bytes=0; Gdiplus::GetImageEncodersSize(&count,&bytes);
    std::vector<BYTE> buffer(bytes);
    auto* codecs=reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    Gdiplus::GetImageEncoders(count,bytes,codecs);
    for(UINT i=0;i<count;i++) if(!wcscmp(codecs[i].MimeType,L"image/png")) png=codecs[i].Clsid;
    sheet.Save(path.c_str(),&png);
}
int wmain(int argc,wchar_t** argv) {
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token{};
    check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"GDI+ starts");
    {
        // The built-in ring: clear corners, solid centre dot, tinted ring, centred click point.
        auto ring=CursorArt::orbit(32,CursorArt::tint(1));
        check(ring.width==32 && ring.height==32 && ring.pixels.size()==32*32,"orbit size");
        check(ring.hotX==16 && ring.hotY==16,"orbit clicks in the centre");
        check(alphaAt(ring,0,0)==0 && alphaAt(ring,31,0)==0 && alphaAt(ring,0,31)==0 && alphaAt(ring,31,31)==0,"orbit corners are clear");
        check(alphaAt(ring,16,16)==255,"orbit centre dot is solid");
        const uint32_t onRing=ring.pixels[16*32+16+9];   // ring radius is about 9 px at this size
        check((onRing>>24)>=230,"orbit ring is solid");
        check(((onRing>>8)&255)>((onRing>>16)&255),"orbit ring carries the mint tint");
        check(alphaAt(ring,16,16+5)<200,"orbit keeps a gap between dot and ring");
        check(CursorArt::orbit(64,CursorArt::tint(3)).hotX==32,"orbit scales");
        check(CursorArt::hex(0x97F2CF)=="#97f2cf","hex colour");
        check(CursorArt::baseSize(0)==24 && CursorArt::baseSize(2)==32 && CursorArt::baseSize(9)==32,"size steps");

        // Native cursor with the same click point.
        HCURSOR cursor=CursorArt::toCursor(ring);
        check(cursor!=nullptr,"native cursor created");
        ICONINFO info{};
        check(GetIconInfo(cursor,&info)!=FALSE,"native cursor readable");
        check(!info.fIcon && info.xHotspot==16 && info.yHotspot==16,"native cursor hotspot");
        if(info.hbmColor) DeleteObject(info.hbmColor);
        if(info.hbmMask) DeleteObject(info.hbmMask);
        DestroyCursor(cursor);

        // Data URL for the HTML windows.
        const std::string url=CursorArt::pngDataUrl(ring);
        check(url.rfind("data:image/png;base64,iVBOR",0)==0,"png data url");

        // A user's PNG and .cur go through the same path.
        wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH,temp);
        const std::wstring png=std::wstring(temp)+L"hibernation-cursor-test.png";
        const std::wstring cur=std::wstring(temp)+L"hibernation-cursor-test.cur";
        {
            auto big=CursorArt::orbit(96,CursorArt::tint(4));
            Gdiplus::Bitmap bitmap(96,96,96*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(big.pixels.data()));
            CLSID clsid{};
            UINT count=0,bytes=0; Gdiplus::GetImageEncodersSize(&count,&bytes);
            std::vector<BYTE> buffer(bytes);
            auto* codecs=reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
            Gdiplus::GetImageEncoders(count,bytes,codecs);
            for(UINT i=0;i<count;i++) if(!wcscmp(codecs[i].MimeType,L"image/png")) clsid=codecs[i].Clsid;
            check(bitmap.Save(png.c_str(),&clsid)==Gdiplus::Ok,"test png written");
        }
        CursorArt::Image loaded;
        check(CursorArt::loadFile(png,28,true,loaded),"png loads");
        check(loaded.width==28 && loaded.hotX==14 && loaded.hotY==14,"png centred tip");
        check(alphaAt(loaded,14,14)>200 && alphaAt(loaded,0,0)==0,"png keeps its transparency");
        check(CursorArt::loadFile(png,28,false,loaded) && loaded.hotX==0 && loaded.hotY==0,"png corner tip");
        HCURSOR fromPng=CursorArt::loadNative(png,32,true);
        check(fromPng!=nullptr,"png native cursor");
        DestroyCursor(fromPng);

        writeCur(cur,ring,5,7);
        check(CursorArt::loadFile(cur,32,true,loaded),"cur loads");
        check(loaded.hotX==5 && loaded.hotY==7,"cur keeps its own click point");
        check(alphaAt(loaded,16,16)>200 && alphaAt(loaded,0,0)==0,"cur alpha recovered");
        const uint32_t curRing=loaded.pixels[16*32+16+9];
        check(((curRing>>8)&255)>((curRing>>16)&255),"cur colours recovered");
        HCURSOR fromCur=CursorArt::loadNative(cur,32,true);
        check(fromCur!=nullptr,"cur native cursor");
        DestroyCursor(fromCur);

        check(!CursorArt::supportedFile(L"x.exe") && CursorArt::supportedFile(L"X.PNG") && CursorArt::supportedFile(L"a.Ani"),"file types");
        check(!CursorArt::loadFile(L"C:\\definitely\\missing.cur",32,true,loaded),"missing file rejected");
        DeleteFileW(png.c_str()); DeleteFileW(cur.c_str());

        if(argc>2 && std::wstring(argv[1])==L"--preview") writePreview(argv[2]);
    }
    Gdiplus::GdiplusShutdown(token);
    std::cout<<"cursor checks passed\n";
    return 0;
}
