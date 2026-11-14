//
// Terminal / Components / TerminalImage.hpp
// 半格像素图片元件：每个字符格用上下两个像素（▀ + 前景/背景真彩），
// 因此 1 列 = 1 像素宽、1 行 = 2 像素高，能跟着 DOM 布局 / 裁剪 / 真彩刷新走。
//
// PNG / JPG 解码走 Windows 自带的 WIC（windowscodecs），不引第三方库。
//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>
#include <wincodec.h>

#include "../Core/UI.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Core/TerminalMessage.hpp"

namespace Terminal {

    enum class ImageFit { Contain, Cover, Stretch };

    namespace Detail {
        // 运行时加载 ole32 / windowscodecs，避免使用方额外链接 -lole32 -lwindowscodecs
        using CoInitializeExFn = HRESULT(WINAPI*)(void*, DWORD);
        using CoCreateInstanceFn = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, void**);

        inline const GUID kClsidWicImagingFactory = {
            0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a }
        };
        inline const GUID kIidWicImagingFactory = {
            0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 }
        };
        inline const GUID kGuidWicPixelFormat32bppRGBA = {
            0xf5c7ad2d, 0x6a8d, 0x43dd, { 0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9 }
        };

        struct WicState {
            IWICImagingFactory* factory{ nullptr };
            std::once_flag once;
            bool tried{ false };
        };

        template <class Fn>
        inline Fn ProcAddress(HMODULE module, const char* name) {
            FARPROC raw = GetProcAddress(module, name);
            Fn fn = nullptr;
            static_assert(sizeof(fn) == sizeof(raw), "function pointer size mismatch");
            std::memcpy(&fn, &raw, sizeof(fn));
            return fn;
        }

        inline void InitWic(WicState& state) {
            state.tried = true;
            HMODULE ole32 = LoadLibraryW(L"ole32.dll");
            HMODULE wic = LoadLibraryW(L"windowscodecs.dll");
            (void)wic;   // 保持模块引用，防止工厂创建后被卸载

            if (ole32 == nullptr) return;
            auto coInit = ProcAddress<CoInitializeExFn>(ole32, "CoInitializeEx");
            auto coCreate = ProcAddress<CoCreateInstanceFn>(ole32, "CoCreateInstance");
            if (coInit != nullptr) {
                // 已初始化成 MTA 时这里会返回 RPC_E_CHANGED_MODE，WIC 仍可继续用
                coInit(nullptr, COINIT_APARTMENTTHREADED);
            }
            if (coCreate != nullptr) {
                coCreate(kClsidWicImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         kIidWicImagingFactory, reinterpret_cast<void**>(&state.factory));
            }
        }

        inline IWICImagingFactory* WicFactory() {
            static WicState state;
            std::call_once(state.once, InitWic, state);
            return state.factory;
        }

        template <class T>
        inline void ComRelease(T*& p) {
            if (p) { p->Release(); p = nullptr; }
        }
    }

    class UIImage : public UI {
    public:
        bool showPlaceholder{ false };
        std::string placeholder{"[图片]"};
        ImageFit fit{ ImageFit::Contain };

        UIImage() {
            signature = "UIImage";
            style = Style(Theme::Current().text, Theme::Current().background);
        }

        UIImage(const std::string& path) : UIImage() { SetImage(path); }

        //---------- 配置 ----------
        UIImage& SetFit(const ImageFit f) { fit = f; Invalidate(); return *this; }
        UIImage& SetFit(const std::string& name) {
            if (name == "cover") fit = ImageFit::Cover;
            else if (name == "stretch" || name == "fill") fit = ImageFit::Stretch;
            else fit = ImageFit::Contain;
            Invalidate();
            return *this;
        }
        UIImage& SetPlaceholder(const std::string& text) { placeholder = text; showPlaceholder = true; Invalidate(); return *this; }

        bool SetImage(const std::string& utf8Path) {
            clearPixels();
            if (!loadWic(utf8Path)) { InvalidateLayout(); return false; }
            InvalidateLayout();
            return true;
        }

        bool SetPixels(const int w, const int h, std::vector<std::uint8_t> rgba) {
            if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) * h * 4) return false;
            m_width = w;
            m_height = h;
            m_rgba = std::move(rgba);
            InvalidateLayout();
            return true;
        }

        void Clear() { clearPixels(); InvalidateLayout(); }

        [[nodiscard]] bool HasImage() const { return m_width > 0 && m_height > 0 && !m_rgba.empty(); }
        [[nodiscard]] int PixelWidth() const { return m_width; }
        [[nodiscard]] int PixelHeight() const { return m_height; }

        //---------- 度量 ----------
        int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            if (layout.grow > 0) return 1;
            if (HasImage()) return m_width;
            return placeholder.empty() ? 0 : Utf8::DisplayWidth(placeholder);
        }

        int MeasureHeight() const override {
            if (layout.height >= 0) return layout.height;
            if (layout.grow > 0) return 1;
            if (HasImage()) return (m_height + 1) / 2;
            return 1;
        }

        //---------- 绘制 ----------
        void OnRender(ConsoleBackBuffer& buf) override {
            const Color bg = EffectiveBackground();
            if (!HasImage()) {
                if (showPlaceholder) buf.WriteText(Left(), Top(), placeholder, Style(style.fg, bg), Width());
                return;
            }

            const Bounds area = ContentBounds();
            if (area.IsEmpty()) return;

            // 目标矩形（单位：字符格）；纵向每格 2 像素
            int outW = area.width();
            int outH = area.height();
            int dstX = area.Left();
            int dstY = area.Top();

            // 源矩形（Cover 时裁掉溢出部分）
            double srcX = 0.0, srcY = 0.0;
            double srcW = static_cast<double>(m_width);
            double srcH = static_cast<double>(m_height);

            if (fit != ImageFit::Stretch) {
                const double imgAspect = static_cast<double>(m_width) / static_cast<double>(m_height);
                if (fit == ImageFit::Contain) {
                    const double areaAspect = static_cast<double>(area.width()) /
                                              (static_cast<double>(area.height()) * 2.0);
                    if (imgAspect >= areaAspect) {
                        outW = area.width();
                        outH = std::max(1, static_cast<int>(std::lround(outW / imgAspect / 2.0)));
                        if (outH > area.height()) { outH = area.height(); outW = std::max(1, static_cast<int>(std::lround(outH * 2.0 * imgAspect))); }
                    } else {
                        outH = area.height();
                        outW = std::max(1, static_cast<int>(std::lround(outH * 2.0 * imgAspect)));
                        if (outW > area.width()) { outW = area.width(); outH = std::max(1, static_cast<int>(std::lround(outW / imgAspect / 2.0))); }
                    }
                    outW = std::min(outW, area.width());
                    outH = std::min(outH, area.height());
                    dstX = area.Left() + (area.width() - outW) / 2;
                    dstY = area.Top() + (area.height() - outH) / 2;
                } else { // Cover：铺满，按比例裁源
                    const double areaAspect = static_cast<double>(outW) / (static_cast<double>(outH) * 2.0);
                    if (imgAspect > areaAspect) {
                        srcW = static_cast<double>(m_height) * areaAspect;
                        srcX = (static_cast<double>(m_width) - srcW) / 2.0;
                    } else {
                        srcH = static_cast<double>(m_width) / areaAspect;
                        srcY = (static_cast<double>(m_height) - srcH) / 2.0;
                    }
                }
            }

            for (int cy = 0; cy < outH; ++cy) {
                for (int cx = 0; cx < outW; ++cx) {
                    const double tx = (static_cast<double>(cx) + 0.5) / static_cast<double>(outW);
                    // 上半像素：第 2cy 个采样点；下半像素：第 2cy+1 个
                    const double tyTop = (2.0 * cy + 0.5) / (2.0 * outH);
                    const double tyBottom = (2.0 * cy + 1.5) / (2.0 * outH);
                    const Color top = Sample(srcX, srcY, srcW, srcH, tx, tyTop, bg);
                    const Color bottom = Sample(srcX, srcY, srcW, srcH, tx, tyBottom, bg);
                    buf.SetCell(dstX + cx, dstY + cy, L'▀', Style(top, bottom));
                }
            }
        }

        bool OnMessage(const Message& msg) override {
            if (msg.type == "setImage") { SetImage(msg.text); return true; }
            if (msg.type == "clear") { Clear(); return true; }
            return UI::OnMessage(msg);
        }

    private:
        void clearPixels() {
            m_rgba.clear();
            m_width = 0;
            m_height = 0;
        }

        [[nodiscard]] Color Sample(const double sx0, const double sy0,
                                   const double sw, const double sh,
                                   const double u, const double v, const Color& bg) const {
            int sx = static_cast<int>(sx0 + u * sw);
            int sy = static_cast<int>(sy0 + v * sh);
            sx = std::clamp(sx, 0, m_width - 1);
            sy = std::clamp(sy, 0, m_height - 1);
            const size_t idx = (static_cast<size_t>(sy) * m_width + sx) * 4;
            const std::uint8_t a = m_rgba[idx + 3];
            if (a == 0) return bg;
            const std::uint8_t r = m_rgba[idx + 0];
            const std::uint8_t g = m_rgba[idx + 1];
            const std::uint8_t b = m_rgba[idx + 2];
            if (a == 255) return Color::OfRGB(r, g, b);
            const int inv = 255 - a;
            return Color::OfRGB(static_cast<std::uint8_t>((r * a + bg.r * inv) / 255),
                                static_cast<std::uint8_t>((g * a + bg.g * inv) / 255),
                                static_cast<std::uint8_t>((b * a + bg.b * inv) / 255));
        }

        bool loadWic(const std::string& utf8Path) {
            IWICImagingFactory* factory = Detail::WicFactory();
            if (factory == nullptr || utf8Path.empty()) return false;

            // UTF-8 -> UTF-16
            const int need = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
            if (need <= 1) return false;
            std::wstring wide(static_cast<size_t>(need), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, wide.data(), need);

            IWICBitmapDecoder* decoder = nullptr;
            IWICBitmapFrameDecode* frame = nullptr;
            IWICFormatConverter* converter = nullptr;
            bool ok = false;

            if (SUCCEEDED(factory->CreateDecoderFromFilename(wide.c_str(), nullptr, GENERIC_READ,
                                                             WICDecodeMetadataCacheOnLoad, &decoder)) &&
                SUCCEEDED(decoder->GetFrame(0, &frame)) &&
                SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                SUCCEEDED(converter->Initialize(frame, Detail::kGuidWicPixelFormat32bppRGBA,
                                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                                WICBitmapPaletteTypeCustom))) {
                UINT w = 0, h = 0;
                if (SUCCEEDED(converter->GetSize(&w, &h)) && w > 0 && h > 0) {
                    std::vector<std::uint8_t> pixels(static_cast<size_t>(w) * h * 4);
                    if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4,
                                                        static_cast<UINT>(pixels.size()), pixels.data()))) {
                        m_width = static_cast<int>(w);
                        m_height = static_cast<int>(h);
                        m_rgba = std::move(pixels);
                        ok = true;
                    }
                }
            }

            Detail::ComRelease(converter);
            Detail::ComRelease(frame);
            Detail::ComRelease(decoder);
            return ok;
        }

        int m_width{ 0 };
        int m_height{ 0 };
        std::vector<std::uint8_t> m_rgba{};   // RGBA8888
    };

    using UIImageView = UIImage;
}
