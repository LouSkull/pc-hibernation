#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "core.h"
class Renderer {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext> context;
    Ptr<IDXGISwapChain> swap;
    Ptr<ID3D11RenderTargetView> target;
    Ptr<ID3D11VertexShader> vertex;
    Ptr<ID3D11PixelShader> pixel;
    Ptr<ID3D11Buffer> constants;
    Ptr<ID3D11Texture2D> staging;
    Ptr<ID3D11Query> gpuDisjoint,gpuStart,gpuEnd;
    bool queryPending=false, measuredFrame=false, software=false;
    double gpuMilliseconds=-1;
    Ptr<ID3D11Texture2D> clockTexture;
    Ptr<ID3D11ShaderResourceView> clockResource;
    Ptr<ID3D11SamplerState> clockSampler;
    std::wstring clockCache;
    bool updateClock(const Settings& settings,const SYSTEMTIME& now);
    UINT width = 0, height = 0;
public:
    bool init(HWND window, std::wstring& error, int quality = 100, bool forceSoftware = false);
    bool resize(UINT w, UINT h);
    bool isSoftware() const { return software; }
    double gpuMs() const { return gpuMilliseconds; }
    UINT renderWidth() const { return width; }
    UINT renderHeight() const { return height; }
    bool draw(float time, const Settings& settings, float fade = 1.f, bool present = true);
    bool readPixels(std::vector<unsigned>& pixels, UINT& w, UINT& h);
};
