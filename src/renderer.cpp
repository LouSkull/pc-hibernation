#include "renderer.h"
#include <d3dcompiler.h>
#include <cstring>
#include <objidl.h>
#include <gdiplus.h>

// A continuous domain-warped field, shaded as folded translucent liquid ribbons.
static const char* shader = R"(
Texture2D clockImage : register(t0);
SamplerState clockSampling : register(s0);
cbuffer Frame : register(b0) {
    float2 resolution; float time; float palette;
    float brightness; float fade; float effect; float zoom;
    float turbulence; float glow; float grainAmount; float vignetteAmount;
    float clockEnabled; float hour; float minute; float second;
    float clockStyle; float clockScale; float clockOpacity; float clockPosition;
    float clockFormat24; float clockSeconds; float clockWeight; float clockColor;
};
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex vs(uint id : SV_VertexID) {
    Vertex o; o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2,-2) + float2(-1,1),0,1); return o;
}
float3 colors(float v) {
    float3 a,b,c;
    if (palette < .5) { a=float3(.12,.94,.71); b=float3(.26,.29,1.); c=float3(.97,.27,.64); }
    else if (palette < 1.5) { a=float3(1.,.32,.10); b=float3(.98,.64,.26); c=float3(.52,.12,.57); }
    else if (palette < 2.5) { a=float3(.12,.48,1.); b=float3(.14,.91,.93); c=float3(.59,.65,.99); }
    else if (palette < 3.5) { a=float3(.67,.12,1.); b=float3(1.,.18,.50); c=float3(.23,.24,.95); }
    else if (palette < 4.5) { a=float3(.15,.70,.40); b=float3(.69,.95,.32); c=float3(.07,.32,.24); }
    else if (palette < 5.5) { a=float3(.45,.55,.67); b=float3(.86,.91,1.); c=float3(.30,.38,.55); }
    else if (palette < 6.5) { a=float3(1.,.66,.19); b=float3(.92,.32,.10); c=float3(1.,.87,.55); }
    else { a=float3(.99,.62,.74); b=float3(.63,.57,.99); c=float3(.45,.81,.91); }
    return lerp(lerp(a,b,smoothstep(-.9,.1,v)),c,smoothstep(.15,1.1,v));
}
float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float segmentDistance(float2 p,float2 a,float2 b) {
    float2 pa=p-a,ba=b-a; return length(pa-ba*saturate(dot(pa,ba)/dot(ba,ba)));
}
float digit(float2 p,int n,float thick) {
    // Seven segment display; bit order: top, upper right, lower right, bottom, lower left, upper left, middle.
    int masks[10]={63,6,91,79,102,109,125,7,127,111};
    int mask=masks[clamp(n,0,9)]; float d=10.;
    if(mask&1) d=min(d,segmentDistance(p,float2(.12,.02),float2(.40,.02)));
    if(mask&2) d=min(d,segmentDistance(p,float2(.43,.06),float2(.43,.44)));
    if(mask&4) d=min(d,segmentDistance(p,float2(.43,.54),float2(.43,.92)));
    if(mask&8) d=min(d,segmentDistance(p,float2(.12,.96),float2(.40,.96)));
    if(mask&16) d=min(d,segmentDistance(p,float2(.09,.54),float2(.09,.92)));
    if(mask&32) d=min(d,segmentDistance(p,float2(.09,.06),float2(.09,.44)));
    if(mask&64) d=min(d,segmentDistance(p,float2(.12,.49),float2(.40,.49)));
    float aa=max(fwidth(d),.008);
    return 1.-smoothstep(thick,thick+aa*1.2,d);
}
float colonMark(float2 p,float thick) {
    float d=min(length(p-float2(0,.30)),length(p-float2(0,.70)));
    float aa=max(fwidth(d),.006);
    return 1.-smoothstep(thick*.95,thick*.95+aa*1.2,d);
}
float3 clockTint() {
    if(clockColor<.5) return float3(.86,.91,.97);
    if(clockColor<1.5) return float3(.59,.95,.81);
    if(clockColor<2.5) return float3(.50,.82,1.);
    if(clockColor<3.5) return float3(.73,.62,1.);
    if(clockColor<4.5) return float3(.98,.64,.79);
    return float3(.96,.80,.50);
}
bool clockHidesTens() { return clockFormat24<.5 && int(hour)<10; }
// Advance width of the whole time, used to centre it on the anchor.
float timeWidth() {
    float w=(clockHidesTens()?.62:1.24)+.31+1.24;
    if(clockSeconds>.5) w+=.31+1.24;
    return w-.10;
}
// One pass over the digits; a thicker stroke is reused to build the glow halo.
float drawTime(float2 c,float thick) {
    int h=int(hour),m=int(minute),s=int(second);
    float mask=0.,x=0.;
    if(!clockHidesTens()) { mask=max(mask,digit(c-float2(x,0),h/10,thick)); x+=.62; }
    mask=max(mask,digit(c-float2(x,0),h%10,thick)); x+=.62;
    mask=max(mask,colonMark(c-float2(x+.105,0),thick)); x+=.31;
    mask=max(mask,digit(c-float2(x,0),m/10,thick)); x+=.62;
    mask=max(mask,digit(c-float2(x,0),m%10,thick)); x+=.62;
    if(clockSeconds>.5) {
        mask=max(mask,colonMark(c-float2(x+.105,0),thick)); x+=.31;
        mask=max(mask,digit(c-float2(x,0),s/10,thick)); x+=.62;
        mask=max(mask,digit(c-float2(x,0),s%10,thick));
    }
    return saturate(mask);
}
float4 ps(Vertex input) : SV_TARGET {
    float2 uv = input.uv;
    float2 p = (uv-.5)*float2(resolution.x/resolution.y,1.)*2.7/zoom;
    float t=time*.20;
    float2 q=p;
    q += turbulence*.82*float2(sin(p.y*1.6+t),cos(p.x*1.3-t*.8));
    q += turbulence*.36*float2(sin(q.y*3.-t*.7),cos(q.x*2.4+t*.6));
    float f=q.y*.95+q.x*.45 + .60*sin(q.x*1.5+t) + .32*cos(q.y*2.0-q.x-t*.8);
    float ribbons=sin(f*4.2+t*.7);
    float crest=pow(saturate(.5+.5*ribbons),3.);
    float edge=pow(saturate(.5+.5*sin(f*4.2+t*.7+.48)),22.);
    float3 ink=colors(sin(f*1.25-t*.25));
    float3 col=float3(.012,.024,.045) + ink*(crest*.78 + edge*.52);
    col += colors(cos(q.x*.7+t))*.07*exp(-abs(f+.9));
    [branch] if(effect>.5 && effect<1.5) {
        col=float3(.008,.017,.03);
        for(int i=0;i<5;i++) {
            float v=float(i); float curtain=sin(q.x*(1.4+v*.2)+t*.7+v)*.25;
            float band=exp(-abs(p.y+curtain+.25-v*.16)* (8.+v));
            float rays=.55+.45*sin(q.x*16.+sin(q.x*7.+t)*2.+v);
            col+=colors(v*.45-.9)*band*rays*.29;
        }
    } else if(effect>1.5 && effect<2.5) {
        float field=0.;
        for(int i=0;i<7;i++) {
            float v=float(i); float2 center=float2(sin(t*(.4+v*.06)+v*2.),cos(t*.5+v*1.4))*.9;
            field+=.15/max(dot(q-center,q-center),.025);
        }
        float body=smoothstep(.8,1.8,field); float rim=exp(-abs(field-1.35)*7.);
        col=float3(.016,.008,.024)+colors(sin(field*.7+t*.2))*body*.48+colors(cos(field))*rim*.62;
    } else if(effect>2.5 && effect<3.5) {
        float r=length(q),a=atan2(q.y,q.x); float rings=sin(r*12.-t*2.+sin(a*3.+t)*turbulence*2.);
        float light=pow(saturate(rings),14.)/(1.+r*r);
        col=float3(.012,.012,.025)+colors(sin(a+t*.2))*light*1.4;
        col+=colors(cos(r*3.-t))*.14*exp(-r*1.4);
    } else if(effect>3.5 && effect<4.5) {
        col=float3(.008,.016,.035);
        for(int i=0;i<7;i++) {
            float v=float(i); float wave=sin(q.x*(1.2+v*.15)+t+v*.5)*(.22+turbulence*.25);
            float band=exp(-abs(q.y-wave+(v-3.)*.22)*16.);
            col+=colors(v*.3-1.)*band*.42;
        }
    } else if(effect>4.5 && effect<5.5) {
        float2 water=q*2.; float caustic=0.;
        for(int i=0;i<4;i++) {
            water+=float2(sin(water.y+t),cos(water.x-t))*.5;
            caustic+=1./(.12+abs(sin(water.x)*cos(water.y)));
        }
        col=colors(sin(q.x*.6+q.y))*pow(saturate(caustic*.035),2.)*.75+float3(.01,.026,.04);
    } else if(effect>5.5 && effect<6.5) {
        float density=0.,amp=.5; float2 smoke=q;
        for(int i=0;i<4;i++) { density+=amp*sin(smoke.x+sin(smoke.y+t*.4))*cos(smoke.y-t*.2); smoke=smoke*1.9+float2(.7,.3); amp*=.5; }
        col=colors(density*2.)*smoothstep(-.55,.65,density)*.7+float3(.015,.012,.025);
    } else if(effect>6.5 && effect<7.5) {
        float2 grid=q*3.+float2(0,t*.5);
        float2 cells=abs(frac(grid)-.5); float beam=pow(saturate(1.-min(cells.x,cells.y)*2.),25.);
        col=colors(sin(q.y+t*.3))*beam*.72+colors(cos(q.x))*.06;
    } else if(effect>7.5 && effect<8.5) {
        float glass=sin(q.x*2.1+t*.6)+cos(q.y*1.9-t*.4)+sin((q.x+q.y)*2.7);
        float ridge=pow(saturate(1.-abs(sin(glass*2.))),15.);
        col=colors(sin(glass))* (.14+.35*smoothstep(-1.,2.,glass)) + ridge*.65;
        col*=.7+.3*sin(q.y+q.x+t);
    } else if(effect>8.5 && effect<9.5) {
        col=float3(.018,.025,.035);
        for(int i=0;i<6;i++) {
            float n=float(i); float2 center=float2(sin(t*.35+n*2.3),cos(t*.27+n*1.7));
            float radius=.20+.07*sin(n*2.),d=length(p-center);
            float rim=exp(-abs(d-radius)*65.); float fill=smoothstep(radius,radius-.08,d);
            float highlight=exp(-length(p-center+float2(radius*.3,radius*.4))*40.);
            col+=colors(sin(n))*rim*.5+colors(cos(n))*fill*.09+highlight*.7;
        }
    } else if(effect>9.5 && effect<10.5) {
        float terrain=sin(q.x*1.5+t*.2)*cos(q.y*1.4)+.3*sin(q.x*3.+q.y*2.-t*.3);
        float contour=pow(saturate(.5+.5*cos(terrain*30.)),22.);
        col=colors(terrain)*contour*.66+colors(terrain)*.07;
    } else if(effect>10.5) {
        float r=length(q),a=abs(frac(atan2(q.y,q.x)/6.2831853*6.)-.5)*1.0472;
        float2 mirrored=float2(cos(a),sin(a))*r;
        float petals=sin(mirrored.x*5.+t)+cos(mirrored.y*9.-t*.5);
        col=colors(sin(petals+t*.3))*pow(saturate(.5+.5*sin(petals*3.)),4.)*.8;
    }
    col *= .65+glow*.9;
    col *= lerp(1.,.45+.55*pow(saturate(1.-length((uv-.5)*1.2)),.7),vignetteAmount);
    float grain=frac(sin(dot(input.position.xy,float2(12.9898,78.233)))*43758.5453)-.5;
    col += grain*grainAmount/255.;
    col=pow(max(col,0),.88)*brightness;
    [branch] if(clockEnabled>.5) {
        float2 anchor=clockPosition<.5?float2(.5,.82):(clockPosition<1.5?float2(.5,.5):float2(.18,.18));
        float2 centered=(uv-anchor)*float2(resolution.x/resolution.y,1.)*18./clockScale;
        float2 c=centered+float2(timeWidth()*.5,.49);
        float thick=.020+clockWeight*.009;          // thin / regular / bold
        float mask=drawTime(c,thick);
        float3 tint=clockTint();
        if(clockStyle>.5) {
            float2 label=centered/float2(5.,1.25)+.5;
            mask=clockImage.Sample(clockSampling,label).a;
            if(clockStyle>1.5) {
                float2 panel=abs(centered)-float2(2.55,.68);
                float plate=1.-smoothstep(0.,.05,max(panel.x,panel.y));
                col=lerp(col,float3(.018,.025,.034),plate*clockOpacity*.8);
                float underline=(1.-smoothstep(.006,.025,abs(centered.y-.64)))*step(abs(centered.x),2.3);
                col=lerp(col,tint,underline*clockOpacity*.5);
            }
        }
        col=lerp(col,tint,mask*clockOpacity);
    }
    return float4(col*fade,1);
}
)";

bool Renderer::init(HWND window, std::wstring& error, int quality, bool forceSoftware) {
    RECT r{}; GetClientRect(window,&r);
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount=2; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.Width=static_cast<UINT>(r.right*quality/100); desc.BufferDesc.Height=static_cast<UINT>(r.bottom*quality/100);
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow=window;
    desc.SampleDesc.Count=1; desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    auto hr=D3D11CreateDeviceAndSwapChain(nullptr,forceSoftware?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);
    software=forceSoftware || FAILED(hr);
    if (FAILED(hr)) hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);
    if (FAILED(hr)) { error=L"Не удалось создать устройство Direct3D 11."; return false; }
    D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
    device->CreateQuery(&query,&gpuDisjoint); query.Query=D3D11_QUERY_TIMESTAMP;
    device->CreateQuery(&query,&gpuStart); device->CreateQuery(&query,&gpuEnd);
    // Bytecode is identical on every monitor; compile once per process.
    static Ptr<ID3DBlob> vsCode,psCode;
    Ptr<ID3DBlob> errors;
    hr=S_OK;
    if(!vsCode) hr=D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"vs","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vsCode,&errors);
    if(SUCCEEDED(hr) && !psCode) hr=D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"ps","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&psCode,&errors);
    if (FAILED(hr)) {
        error=L"Не удалось скомпилировать шейдер.";
        if(errors) {
            const char* details=static_cast<const char*>(errors->GetBufferPointer());
            OutputDebugStringA(details);
            error+=L"\n";
            for(size_t i=0;i<errors->GetBufferSize() && details[i];i++) error+=static_cast<unsigned char>(details[i]);
        }
        return false;
    }
    if (FAILED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vertex)) ||
        FAILED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&pixel))) {
        error=L"Не удалось создать шейдеры."; return false;
    }
    D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=96; buffer.Usage=D3D11_USAGE_DEFAULT;
    buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(FAILED(device->CreateBuffer(&buffer,nullptr,&constants))) { error=L"Не удалось создать буфер кадра."; return false; }
    if(!resize(desc.BufferDesc.Width,desc.BufferDesc.Height)) { error=L"Не удалось создать поверхность отрисовки."; return false; }
    return true;
}
bool Renderer::resize(UINT w, UINT h) {
    if (!swap || !w || !h) return false;
    if(target && width==w && height==h) return true;
    context->OMSetRenderTargets(0,nullptr,nullptr); target.Reset(); staging.Reset();
    if(FAILED(swap->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0))) return false;
    Ptr<ID3D11Texture2D> back;
    if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&back)))) return false;
    if(FAILED(device->CreateRenderTargetView(back.Get(),nullptr,&target))) return false;
    width=w; height=h; return true;
}
bool Renderer::draw(float time, const Settings& s, float fade, bool present) {
    if (!target) return false;
    if(queryPending) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{}; UINT64 begin=0,end=0;
        if(context->GetData(gpuDisjoint.Get(),&timing,sizeof(timing),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK &&
           context->GetData(gpuStart.Get(),&begin,sizeof(begin),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK &&
           context->GetData(gpuEnd.Get(),&end,sizeof(end),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK) {
            if(!timing.Disjoint && timing.Frequency) gpuMilliseconds=1000.0*(end-begin)/timing.Frequency;
            queryPending=false;
        }
    }
    measuredFrame=present && !queryPending && gpuDisjoint && gpuStart && gpuEnd;
    SYSTEMTIME clock{}; GetLocalTime(&clock);
    if(s.clock && s.clockStyle>0 && !updateClock(s,clock)) return false;
    int shownHour=clock.wHour;
    if(!s.clockFormat24) { shownHour%=12; if(shownHour==0) shownHour=12; }
    float data[24]={static_cast<float>(width),static_cast<float>(height),time,static_cast<float>(s.palette),
        s.brightness/100.f,fade,static_cast<float>(s.effect),s.scale/100.f,
        s.turbulence/100.f,s.glow/100.f,s.grain?1.f:0.f,s.vignette?1.f:0.f,
        s.clock?1.f:0.f,static_cast<float>(shownHour),static_cast<float>(clock.wMinute),static_cast<float>(clock.wSecond),
        static_cast<float>(s.clockStyle),s.clockSize/100.f,s.clockOpacity/100.f,static_cast<float>(s.clockPosition),
        s.clockFormat24?1.f:0.f,s.clockSeconds?1.f:0.f,static_cast<float>(s.clockWeight),static_cast<float>(s.clockColor)};
    if(measuredFrame) { context->Begin(gpuDisjoint.Get()); context->End(gpuStart.Get()); }
    context->UpdateSubresource(constants.Get(),0,nullptr,data,0,0);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
    context->RSSetViewports(1,&viewport);
    auto rt=target.Get(); context->OMSetRenderTargets(1,&rt,nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(pixel.Get(),nullptr,0);
    auto cb=constants.Get(); context->PSSetConstantBuffers(0,1,&cb);
    auto clockView=clockResource.Get(); auto sampling=clockSampler.Get();
    context->PSSetShaderResources(0,1,&clockView); context->PSSetSamplers(0,1,&sampling);
    context->Draw(3,0);
    if(measuredFrame) { context->End(gpuEnd.Get()); context->End(gpuDisjoint.Get()); queryPending=true; }
    return !present || SUCCEEDED(swap->Present(0,0));
}
bool Renderer::updateClock(const Settings& s,const SYSTEMTIME& now) {
    int h=now.wHour; if(!s.clockFormat24) { h%=12; if(!h) h=12; }
    wchar_t label[32]{};
    if(s.clockSeconds) swprintf_s(label,L"%02d:%02d:%02d",h,now.wMinute,now.wSecond);
    else swprintf_s(label,L"%02d:%02d",h,now.wMinute);
    std::wstring cache=std::wstring(label)+L"/"+std::to_wstring(s.clockStyle)+L"/"+std::to_wstring(s.clockWeight)+L"/"+std::to_wstring(s.clockFormat24)+L"/"+std::to_wstring(now.wHour);
    if(cache==clockCache && clockResource) return true;
    if(!clockTexture) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=1024;desc.Height=256;desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&clockTexture)) || FAILED(device->CreateShaderResourceView(clockTexture.Get(),nullptr,&clockResource))) return false;
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        if(FAILED(device->CreateSamplerState(&sampler,&clockSampler))) return false;
    }
    Gdiplus::Bitmap bitmap(1024,256,PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bitmap);g.Clear(Gdiplus::Color(0,0,0,0));g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        const wchar_t* face=s.clockStyle==1?(s.clockWeight==0?L"Segoe UI Light":L"Segoe UI"):L"Bahnschrift";
        Gdiplus::Font font(face,s.clockSeconds?142.f:166.f,s.clockWeight==2?Gdiplus::FontStyleBold:Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
        Gdiplus::SolidBrush white(Gdiplus::Color(255,255,255,255));Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(label,-1,&font,Gdiplus::RectF(0,0,1024,220),&format,&white);
        if(!s.clockFormat24) {
            Gdiplus::Font suffixFont(L"Segoe UI",48,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
            g.DrawString(now.wHour<12?L"AM":L"PM",-1,&suffixFont,Gdiplus::RectF(0,206,1024,45),&format,&white);
        }
    }
    Gdiplus::BitmapData data{};Gdiplus::Rect bounds(0,0,1024,256);
    if(bitmap.LockBits(&bounds,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)!=Gdiplus::Ok) return false;
    context->UpdateSubresource(clockTexture.Get(),0,nullptr,data.Scan0,data.Stride,0);bitmap.UnlockBits(&data);clockCache=cache;return true;
}
bool Renderer::readPixels(std::vector<unsigned>& pixels, UINT& w, UINT& h) {
    Ptr<ID3D11Texture2D> back;
    if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&back)))) return false;
    D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc); desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    if(!staging && FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) return false;
    context->CopyResource(staging.Get(),back.Get()); D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;
    w=width; h=height; pixels.resize(static_cast<size_t>(w)*h);
    for(UINT y=0;y<h;y++) for(UINT x=0;x<w;x++) {
        const auto* row=reinterpret_cast<const unsigned*>(static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch);
        unsigned c=row[x]; pixels[static_cast<size_t>(y)*w+x]=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);
    }
    context->Unmap(staging.Get(),0); return true;
}
