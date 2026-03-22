// GDXIBLBaker.cpp — HDR laden + IBL-Cubemaps auf CPU backen.
//
// Keine DX11/OpenGL-Abhaengigkeiten — nur stb_image + C-Math.
// Das Backend laed die Ergebnispuffer selbst als Texturen hoch.

#include "GDXIBLBaker.h"
#include "Debug.h"

// stb_image: STB_IMAGE_IMPLEMENTATION wird von GDXTextureLoader.cpp gesetzt.
// Hier nur includen — stbi_loadf ist dann sichtbar.
#include "..//third_party/stb_image.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <thread>
#include <future>
#include <atomic>

// Windows definiert min/max als Makros — mit (std::max) umgehen.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// ---------------------------------------------------------------------------
// Konstanten
// ---------------------------------------------------------------------------
static const float PI      = 3.14159265359f;
static const float TWO_PI  = 6.28318530718f;

// ---------------------------------------------------------------------------
// Equirectangular Sampling (bilinear, mit Firefly-Clamp)
// ---------------------------------------------------------------------------
static void SampleEquirect(const float* hdr, int w, int h,
                            float dx, float dy, float dz,
                            float& r, float& g, float& b)
{
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f) { r = g = b = 0.0f; return; }
    dx /= len; dy /= len; dz /= len;

    float u = atan2f(dz, dx) / TWO_PI + 0.5f;
    float v = acosf((std::max)(-1.0f, (std::min)(1.0f, dy))) / PI;

    float px = u * float(w - 1);
    float py = v * float(h - 1);
    int x0 = (int)px,              y0 = (int)py;
    int x1 = (std::min)(x0+1, w-1), y1 = (std::min)(y0+1, h-1);
    float fx = px - float(x0),     fy = py - float(y0);

    auto s = [&](int x, int y, int c) { return hdr[(y*w+x)*4+c]; };

    r = (1-fx)*(1-fy)*s(x0,y0,0) + fx*(1-fy)*s(x1,y0,0)
      + (1-fx)*fy   *s(x0,y1,0) + fx*fy   *s(x1,y1,0);
    g = (1-fx)*(1-fy)*s(x0,y0,1) + fx*(1-fy)*s(x1,y0,1)
      + (1-fx)*fy   *s(x0,y1,1) + fx*fy   *s(x1,y1,1);
    b = (1-fx)*(1-fy)*s(x0,y0,2) + fx*(1-fy)*s(x1,y0,2)
      + (1-fx)*fy   *s(x0,y1,2) + fx*fy   *s(x1,y1,2);

    // Firefly-Clamp: HDR-Hotspots (Studioleuchten etc.) wuerden bei
    // Monte-Carlo einzelne Samples dominieren und helle Flecken erzeugen.
    const float MAX_LUM = 8.0f;
    float lum = 0.2126f*r + 0.7152f*g + 0.0722f*b;
    if (lum > MAX_LUM)
    {
        float sc = MAX_LUM / lum;
        r *= sc; g *= sc; b *= sc;
    }
}

// ---------------------------------------------------------------------------
// Cubemap Face + UV -> Richtungsvektor (D3D-Reihenfolge: +X -X +Y -Y +Z -Z)
// ---------------------------------------------------------------------------
static void FaceUVToDir(int face, float u, float v,
                        float& dx, float& dy, float& dz)
{
    float uc = 2.0f*u - 1.0f, vc = 2.0f*v - 1.0f;
    switch (face)
    {
    case 0: dx= 1; dy=-vc; dz=-uc; break;
    case 1: dx=-1; dy=-vc; dz= uc; break;
    case 2: dx= uc; dy= 1; dz= vc; break;
    case 3: dx= uc; dy=-1; dz=-vc; break;
    case 4: dx= uc; dy=-vc; dz= 1; break;
    default:dx=-uc; dy=-vc; dz=-1; break;
    }
}

// ---------------------------------------------------------------------------
// Hammersley-Sequenz (Low-Discrepancy)
// ---------------------------------------------------------------------------
static float RadicalInverse(uint32_t bits)
{
    bits = (bits<<16u)|(bits>>16u);
    bits = ((bits&0x55555555u)<<1u)|((bits&0xAAAAAAAAu)>>1u);
    bits = ((bits&0x33333333u)<<2u)|((bits&0xCCCCCCCCu)>>2u);
    bits = ((bits&0x0F0F0F0Fu)<<4u)|((bits&0xF0F0F0F0u)>>4u);
    bits = ((bits&0x00FF00FFu)<<8u)|((bits&0xFF00FF00u)>>8u);
    return float(bits) * 2.3283064365386963e-10f;
}
static void Hammersley(uint32_t i, uint32_t n, float& xi1, float& xi2)
{
    xi1 = float(i) / float(n);
    xi2 = RadicalInverse(i);
}

// ---------------------------------------------------------------------------
// Orthonormalbasis um N
// ---------------------------------------------------------------------------
static void MakeONB(float nx, float ny, float nz,
                    float& tx, float& ty, float& tz,
                    float& bx, float& by, float& bz)
{
    float ax = (fabsf(nx) > 0.9f) ? 0.0f : 1.0f;
    float ay = (fabsf(nx) > 0.9f) ? 1.0f : 0.0f;
    tx = ay*nz; ty = -ax*nz; tz = ax*ny - ay*nx;
    float tl = sqrtf(tx*tx+ty*ty+tz*tz);
    if (tl > 1e-6f) { tx/=tl; ty/=tl; tz/=tl; }
    bx = ny*tz-nz*ty; by = nz*tx-nx*tz; bz = nx*ty-ny*tx;
}

// ---------------------------------------------------------------------------
// GGX Importance Sampling
// ---------------------------------------------------------------------------
static void SampleGGX(float xi1, float xi2, float roughness,
                      float& hx, float& hy, float& hz)
{
    float a   = roughness * roughness;
    float phi = TWO_PI * xi1;
    float cosT= sqrtf((1.0f-xi2) / (1.0f+(a*a-1.0f)*xi2));
    float sinT= sqrtf(1.0f - cosT*cosT);
    hx = sinT*cosf(phi); hy = cosT; hz = sinT*sinf(phi);
}

// ---------------------------------------------------------------------------
// Irradiance-Cubemap (Lambertian Convolution)
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Minimaler Thread-Pool — Tasks per Work-Item ohne Overhead.
// Jede Row (face,y) ist ein unabhängiger Task.
// ---------------------------------------------------------------------------
static void RunParallel(uint32_t totalTasks, std::function<void(uint32_t)> fn)
{
    const uint32_t nCores = (std::max)(1u, (uint32_t)std::thread::hardware_concurrency());
    std::atomic<uint32_t> next{0u};
    std::vector<std::thread> threads;
    threads.reserve(nCores);
    for (uint32_t t = 0; t < nCores; ++t)
    {
        threads.emplace_back([&]()
        {
            for (;;)
            {
                uint32_t i = next.fetch_add(1u, std::memory_order_relaxed);
                if (i >= totalTasks) break;
                fn(i);
            }
        });
    }
    for (auto& t : threads) t.join();
}

static void BakeIrradiance(const float* hdr, int hw, int hh,
                           uint32_t size, std::vector<float>& out)
{
    const uint32_t SAMPLES = 2048u;
    out.assign(6u * size * size * 4u, 0.0f);

    // Task = eine Row (face*size + y) — feingranular, alle Kerne ausgelastet.
    const uint32_t totalRows = 6u * size;
    RunParallel(totalRows, [&](uint32_t taskIdx)
    {
        const uint32_t face = taskIdx / size;
        const uint32_t y    = taskIdx % size;
        for (uint32_t x = 0; x < size; ++x)
        {
            float u = (x+0.5f)/float(size), v = (y+0.5f)/float(size);
            float nx,ny,nz; FaceUVToDir((int)face, u, v, nx,ny,nz);
            float len = sqrtf(nx*nx+ny*ny+nz*nz);
            nx/=len; ny/=len; nz/=len;

            float tx,ty,tz,bx,by,bz;
            MakeONB(nx,ny,nz, tx,ty,tz, bx,by,bz);

            float aR=0,aG=0,aB=0;
            for (uint32_t i = 0; i < SAMPLES; ++i)
            {
                float xi1,xi2; Hammersley(i,SAMPLES,xi1,xi2);
                float cosT = sqrtf(xi2), sinT = sqrtf(1.0f-xi2);
                float phi  = TWO_PI*xi1;
                float lx = sinT*cosf(phi)*tx + cosT*nx + sinT*sinf(phi)*bx;
                float ly = sinT*cosf(phi)*ty + cosT*ny + sinT*sinf(phi)*by;
                float lz = sinT*cosf(phi)*tz + cosT*nz + sinT*sinf(phi)*bz;
                float r,g,b; SampleEquirect(hdr,hw,hh,lx,ly,lz,r,g,b);
                aR+=r; aG+=g; aB+=b;
            }
            float inv = 1.0f/float(SAMPLES);
            uint32_t idx = (face*size*size + y*size + x)*4;
            out[idx+0]=aR*inv; out[idx+1]=aG*inv;
            out[idx+2]=aB*inv; out[idx+3]=1.0f;
        }
    });
}

// ---------------------------------------------------------------------------
// Prefiltered Environment Map (GGX, mehrere Mips)
// ---------------------------------------------------------------------------
static void BakePrefiltered(const float* hdr, int hw, int hh,
                            uint32_t size, uint32_t mipLevels,
                            std::vector<float>& out)
{
    const uint32_t SAMPLES = 1024u;
    uint32_t total = 0;
    for (uint32_t m = 0; m < mipLevels; ++m)
    { uint32_t ms=(std::max)(size>>m,1u); total += 6u*ms*ms*4u; }
    out.assign(total, 0.0f);

    // Offset-Array vorab berechnen.
    std::vector<uint32_t> mipOffsets(mipLevels);
    { uint32_t off=0; for (uint32_t m=0;m<mipLevels;++m)
      { mipOffsets[m]=off; uint32_t ms=(std::max)(size>>m,1u); off+=6u*ms*ms*4u; } }

    for (uint32_t mip = 0; mip < mipLevels; ++mip)
    {
        uint32_t ms = (std::max)(size>>mip, 1u);
        float roughness = (mipLevels<=1) ? 0.0f : float(mip)/float(mipLevels-1);
        uint32_t offset = mipOffsets[mip];

        // Task = eine Row (face*ms + y) — alle Kerne pro Mip ausgelastet.
        const uint32_t totalRows = 6u * ms;
        RunParallel(totalRows, [&](uint32_t taskIdx)
        {
            const uint32_t face = taskIdx / ms;
            const uint32_t y    = taskIdx % ms;
            for (uint32_t x = 0; x < ms; ++x)
            {
                float u=(x+0.5f)/float(ms), v=(y+0.5f)/float(ms);
                float nx,ny,nz; FaceUVToDir((int)face,u,v,nx,ny,nz);
                float len=sqrtf(nx*nx+ny*ny+nz*nz);
                nx/=len; ny/=len; nz/=len;
                float vx=nx,vy=ny,vz=nz;
                float tx,ty,tz,bx,by,bz;
                MakeONB(nx,ny,nz,tx,ty,tz,bx,by,bz);

                float aR=0,aG=0,aB=0,aW=0;
                for (uint32_t i = 0; i < SAMPLES; ++i)
                {
                    float xi1,xi2; Hammersley(i,SAMPLES,xi1,xi2);
                    float hx,hy,hz;
                    SampleGGX(xi1,xi2,(std::max)(roughness,0.001f),hx,hy,hz);
                    float hwx=hx*tx+hy*nx+hz*bx;
                    float hwy=hx*ty+hy*ny+hz*by;
                    float hwz=hx*tz+hy*nz+hz*bz;
                    float VdotH=vx*hwx+vy*hwy+vz*hwz;
                    float lx=2.0f*VdotH*hwx-vx;
                    float ly=2.0f*VdotH*hwy-vy;
                    float lz=2.0f*VdotH*hwz-vz;
                    float NdotL=nx*lx+ny*ly+nz*lz;
                    if (NdotL > 0.0f)
                    {
                        float r,g,b; SampleEquirect(hdr,hw,hh,lx,ly,lz,r,g,b);
                        aR+=r*NdotL; aG+=g*NdotL; aB+=b*NdotL; aW+=NdotL;
                    }
                }
                if (aW > 0.0f)
                {
                    float inv=1.0f/aW;
                    uint32_t idx = offset + (face*ms*ms + y*ms + x)*4;
                    out[idx+0]=aR*inv; out[idx+1]=aG*inv;
                    out[idx+2]=aB*inv; out[idx+3]=1.0f;
                }
            }
        });
    }
}


// ---------------------------------------------------------------------------
// BRDF Split-Sum LUT (Karis, RG32F)
// ---------------------------------------------------------------------------
static float GSchlick(float NdotV, float r) {
    float k=r*r*0.5f; return NdotV/(NdotV*(1.0f-k)+k);
}
static void BakeBrdfLut(uint32_t size, std::vector<float>& out)
{
    const uint32_t SAMPLES = 1024u;
    out.assign(size*size*2u, 0.0f);

    for (uint32_t j = 0; j < size; ++j)
    for (uint32_t i = 0; i < size; ++i)
    {
        float NdotV    = (i+0.5f)/float(size);
        float roughness= (j+0.5f)/float(size);
        float vx=sqrtf(1.0f-NdotV*NdotV), vy=0.0f, vz=NdotV;
        float A=0,B=0;
        for (uint32_t k = 0; k < SAMPLES; ++k)
        {
            float xi1,xi2; Hammersley(k,SAMPLES,xi1,xi2);
            float hx,hy,hz; SampleGGX(xi1,xi2,roughness,hx,hy,hz);
            float VdotH=vx*hx+vy*hy+vz*hz;
            float lz=2.0f*VdotH*hz-vz;
            float NdotL=(std::max)(lz,0.0f);
            if (NdotL > 0.0f)
            {
                float G   = GSchlick(NdotV,roughness)*GSchlick(NdotL,roughness);
                float GV  = G*(std::max)(VdotH,0.0f)/((std::max)(hz,1e-6f)*NdotV);
                float Fc  = powf(1.0f-(std::max)(VdotH,0.0f),5.0f);
                A += (1.0f-Fc)*GV;
                B += Fc*GV;
            }
        }
        uint32_t idx=(j*size+i)*2;
        out[idx+0]=A/float(SAMPLES);
        out[idx+1]=B/float(SAMPLES);
    }
}

// ---------------------------------------------------------------------------
// Oeffentliche API
// ---------------------------------------------------------------------------
GDXIBLData GDXIBLBaker::Bake(const wchar_t* hdrPath,
                               uint32_t irrSize, uint32_t envSize,
                               uint32_t lutSize, uint32_t envMipLevels)
{
    GDXIBLData result;
    if (!hdrPath || hdrPath[0]==L'\0') return result;

    char narrow[4096]={};
    size_t n=0;
    wcstombs_s(&n, narrow, sizeof(narrow), hdrPath, _TRUNCATE);

    int w=0,h=0,ch=0;
    stbi_set_flip_vertically_on_load(0);
    float* hdr = stbi_loadf(narrow, &w, &h, &ch, 4);
    if (!hdr)
    {
        Debug::LogError(GDX_SRC_LOC, L"GDXIBLBaker::Bake: HDR nicht geladen: ", hdrPath);
        return result;
    }

    Debug::Log(L"GDXIBLBaker: Bake ", w, L"x", h);

    BakeIrradiance (hdr, w, h, irrSize,      result.irradiance);
    BakePrefiltered(hdr, w, h, envSize, envMipLevels, result.prefiltered);
    BakeBrdfLut    (lutSize, result.brdfLut);

    stbi_image_free(hdr);

    result.irrSize = irrSize;
    result.envSize = envSize;
    result.envMips = envMipLevels;
    result.lutSize = lutSize;
    result.valid   = true;

    Debug::Log(L"GDXIBLBaker: Bake fertig");
    return result;
}

// Proceduraler Himmel-Gradient fuer Fallback-IBL.
// dir.y = +1 (oben) → Himmelblau, dir.y = -1 (unten) → Erdgrau.
// Gibt ein weiches, plausibles Ambient ohne externe HDR-Datei.
static void SkyGradient(float dx, float dy, float dz,
                        float& r, float& g, float& b)
{
    float len = sqrtf(dx*dx+dy*dy+dz*dz);
    if (len < 1e-6f) { r=g=b=0.1f; return; }
    float t = (dy/len)*0.5f + 0.5f;  // 0=unten, 1=oben

    // Oben: helles Himmelblau   (0.45, 0.60, 0.85)
    // Unten: neutrales Erdgrau  (0.12, 0.11, 0.10)
    r = 0.12f + t*(0.45f - 0.12f);
    g = 0.11f + t*(0.60f - 0.11f);
    b = 0.10f + t*(0.85f - 0.10f);
}

static void FillCubemapGradient(uint32_t size, std::vector<float>& out)
{
    out.resize(6u*size*size*4u);
    for (uint32_t face=0;face<6;++face)
    for (uint32_t y=0;y<size;++y)
    for (uint32_t x=0;x<size;++x)
    {
        float u=(x+0.5f)/float(size), v=(y+0.5f)/float(size);
        float dx,dy,dz; FaceUVToDir((int)face,u,v,dx,dy,dz);
        float r,g,b; SkyGradient(dx,dy,dz,r,g,b);
        uint32_t idx=(face*size*size+y*size+x)*4u;
        out[idx+0]=r; out[idx+1]=g; out[idx+2]=b; out[idx+3]=1.0f;
    }
}

GDXIBLData GDXIBLBaker::MakeFallback(uint32_t irrSize, uint32_t envSize,
                                      uint32_t lutSize, uint32_t envMips)
{
    GDXIBLData result;

    // Irradiance: Himmel-Gradient, 32x32 genuegt (wird sowieso integriert)
    FillCubemapGradient(irrSize, result.irradiance);

    // Prefiltered: alle Mips mit Gradient befuellen
    uint32_t total=0;
    for (uint32_t m=0;m<envMips;++m)
    { uint32_t ms=(std::max)(envSize>>m,1u); total+=6u*ms*ms*4u; }
    result.prefiltered.resize(total);
    uint32_t offset=0;
    for (uint32_t m=0;m<envMips;++m)
    {
        uint32_t ms=(std::max)(envSize>>m,1u);
        std::vector<float> mipData;
        FillCubemapGradient(ms, mipData);
        std::copy(mipData.begin(), mipData.end(),
                  result.prefiltered.begin()+offset);
        offset += 6u*ms*ms*4u;
    }

    BakeBrdfLut(lutSize, result.brdfLut);

    result.irrSize=irrSize; result.envSize=envSize;
    result.envMips=envMips; result.lutSize=lutSize;
    result.valid=true;
    return result;
}
