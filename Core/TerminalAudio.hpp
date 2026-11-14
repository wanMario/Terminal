//
// Terminal / Core / TerminalAudio.hpp
// 轻量音频播放器：
//   * 首选 Media Foundation（mfplay.dll）——支持 WAV/MP3 解码、暂停、进度和真正的倍速；
//   * 兜底 MCI（winmm.dll）——MF 不可用或文件格式不被 MF 接受时仍能播放。
// 两者都运行时 LoadLibrary，使用方不需要额外 -l 参数。
//

#pragma once

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <windows.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfplay.h>

#include "../Utils/Utf8.hpp"
#include "TerminalMessage.hpp"

namespace Terminal {

    class AudioPlayer {
    public:
        enum class State { Closed, Stopped, Playing, Paused };

        AudioPlayer() = default;
        ~AudioPlayer() { Close(); }

        AudioPlayer(const AudioPlayer&) = delete;
        AudioPlayer& operator=(const AudioPlayer&) = delete;

        bool Open(const std::string& path) {
            Close();
            if (path.empty()) { m_error = "empty path"; return false; }
            const std::wstring wpath = Utf8::ToWide(path);

            if (OpenMf(wpath)) return true;
            if (OpenMci(wpath)) return true;
            m_error = "Media Foundation / MCI 都无法打开该文件";
            return false;
        }

        void Close() {
            CloseMf();
            if (m_backend == Backend::Mci && m_open && !m_alias.empty())
                Send(L"close " + m_alias);
            m_backend = Backend::None;
            m_open = false;
            m_state = State::Closed;
            m_alias.clear();
        }

        bool Play() {
            if (!m_open) return false;
            if (m_backend == Backend::Mf) {
                if (m_state == State::Paused) m_state = State::Playing;
                if (FAILED(m_mfPlayer->Play())) { m_error = "MF Play failed"; return false; }
                m_state = State::Playing;
                return true;
            }
            if (m_state == State::Paused) return Resume();
            if (!Send(L"play " + m_alias)) { m_error = LastMciError(); return false; }
            m_state = State::Playing;
            return true;
        }

        bool Pause() {
            if (!m_open) return false;
            if (m_backend == Backend::Mf) {
                if (FAILED(m_mfPlayer->Pause())) { m_error = "MF Pause failed"; return false; }
                m_state = State::Paused;
                return true;
            }
            if (m_state != State::Playing) return false;
            if (!Send(L"pause " + m_alias)) { m_error = LastMciError(); return false; }
            m_state = State::Paused;
            return true;
        }

        bool Resume() {
            if (!m_open) return false;
            if (m_backend == Backend::Mf) return Play();
            if (!Send(L"resume " + m_alias)) {
                if (!Send(L"play " + m_alias)) { m_error = LastMciError(); return false; }
            }
            m_state = State::Playing;
            return true;
        }

        bool Stop() {
            if (!m_open) return false;
            if (m_backend == Backend::Mf) {
                if (FAILED(m_mfPlayer->Stop())) { m_error = "MF Stop failed"; return false; }
                m_state = State::Stopped;
                return true;
            }
            Send(L"stop " + m_alias);
            Send(L"seek " + m_alias + L" to start");
            m_state = State::Stopped;
            return true;
        }

        bool TogglePause() {
            return m_state == State::Playing ? Pause() : Resume();
        }

        bool Seek(const double seconds) {
            if (!m_open) return false;
            if (m_backend == Backend::Mf) {
                PROPVARIANT pv{};
                pv.vt = VT_I8;
                pv.hVal.QuadPart = static_cast<LONGLONG>(std::max(0.0, seconds) * 10000000.0);
                if (FAILED(m_mfPlayer->SetPosition(kPosition100ns, &pv))) {
                    m_error = "MF Seek failed";
                    return false;
                }
                return true;
            }
            const int ms = std::max(0, static_cast<int>(seconds * 1000.0));
            const bool wasPlaying = m_state == State::Playing;
            if (!Send(L"seek " + m_alias + L" to " + std::to_wstring(ms))) {
                m_error = LastMciError();
                return false;
            }
            if (wasPlaying && !Send(L"play " + m_alias + L" from " + std::to_wstring(ms))) {
                m_error = LastMciError();
                return false;
            }
            m_state = wasPlaying ? State::Playing : State::Paused;
            return true;
        }

        // 0.25x ~ 4x；MF 后端通常完整支持，MCI 后端是 best-effort
        bool SetSpeed(const double speed) {
            m_speed = std::clamp(speed, 0.25, 4.0);
            if (!m_open) return false;
            if (m_backend == Backend::Mf) {
                if (FAILED(m_mfPlayer->SetRate(static_cast<float>(m_speed)))) {
                    m_speedSupported = false;
                    m_error = "MF SetRate failed";
                    return false;
                }
                m_speedSupported = true;
                return true;
            }
            if (m_alias.empty()) return false;
            const int value = std::max(1, static_cast<int>(1000.0 / m_speed));
            m_speedSupported = Send(L"set " + m_alias + L" speed " + std::to_wstring(value));
            if (!m_speedSupported) m_error = LastMciError();
            return m_speedSupported;
        }

        bool SetVolume(const int percent) {
            if (!m_open) return false;
            m_volume = std::clamp(percent, 0, 100);
            if (m_backend == Backend::Mf) {
                if (FAILED(m_mfPlayer->SetVolume(static_cast<float>(m_volume) / 100.0f))) {
                    m_error = "MF SetVolume failed";
                    return false;
                }
                return true;
            }
            const int v = m_volume * 10;
            return Send(L"setaudio " + m_alias + L" volume to " + std::to_wstring(v));
        }

        [[nodiscard]] State GetState() const {
            if (!m_open) return State::Closed;
            if (m_backend == Backend::Mf && m_mfPlayer != nullptr) {
                MFP_MEDIAPLAYER_STATE st = MFP_MEDIAPLAYER_STATE_STOPPED;
                if (SUCCEEDED(m_mfPlayer->GetState(&st))) {
                    switch (st) {
                    case MFP_MEDIAPLAYER_STATE_PLAYING: return State::Playing;
                    case MFP_MEDIAPLAYER_STATE_PAUSED: return State::Paused;
                    case MFP_MEDIAPLAYER_STATE_EMPTY:
                    case MFP_MEDIAPLAYER_STATE_STOPPED: return State::Stopped;
                    default: return State::Closed;
                    }
                }
            }
            return m_state;
        }

        [[nodiscard]] bool IsOpen() const noexcept { return m_open; }
        [[nodiscard]] bool IsPlaying() const { return GetState() == State::Playing; }
        [[nodiscard]] bool IsPaused() const { return GetState() == State::Paused; }
        [[nodiscard]] double Speed() const noexcept { return m_speed; }
        [[nodiscard]] bool SpeedSupported() const noexcept { return m_speedSupported; }
        [[nodiscard]] const std::string& LastError() const noexcept { return m_error; }
        [[nodiscard]] const char* BackendName() const noexcept {
            return m_backend == Backend::Mf ? "MediaFoundation" : (m_backend == Backend::Mci ? "MCI" : "none");
        }

        // MFPlay 依赖窗口消息循环；控制台主循环每帧调用一次即可。
        void Update() {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        [[nodiscard]] double Position() const {
            if (!m_open) return 0.0;
            if (m_backend == Backend::Mf) {
                PROPVARIANT pv{};
                if (FAILED(m_mfPlayer->GetPosition(kPosition100ns, &pv))) return 0.0;
                if (pv.vt == VT_UI8) return static_cast<double>(pv.uhVal.QuadPart) / 10000000.0;
                if (pv.vt == VT_I8) return static_cast<double>(pv.hVal.QuadPart) / 10000000.0;
                return 0.0;
            }
            const int ms = QueryNumber(L"position");
            return ms < 0 ? 0.0 : static_cast<double>(ms) / 1000.0;
        }

        [[nodiscard]] double Duration() const {
            if (!m_open) return 0.0;
            if (m_backend == Backend::Mf) {
                PROPVARIANT pv{};
                if (FAILED(m_mfPlayer->GetDuration(kPosition100ns, &pv))) return 0.0;
                if (pv.vt == VT_UI8) return static_cast<double>(pv.uhVal.QuadPart) / 10000000.0;
                if (pv.vt == VT_I8) return static_cast<double>(pv.hVal.QuadPart) / 10000000.0;
                return 0.0;
            }
            const int ms = QueryNumber(L"length");
            return ms < 0 ? 0.0 : static_cast<double>(ms) / 1000.0;
        }

        [[nodiscard]] std::string Mode() const {
            const State s = GetState();
            if (s == State::Playing) return "playing";
            if (s == State::Paused) return "paused";
            if (s == State::Stopped) return "stopped";
            return "closed";
        }

        // 给页面/控件使用的消息入口：play/pause/resume/stop/seek/speed
        bool OnMessage(const Message& msg) {
            if (msg.type == "play") return Play();
            if (msg.type == "pause") return Pause();
            if (msg.type == "resume") return Resume();
            if (msg.type == "stop") return Stop();
            if (msg.type == "speed") {
                double v = m_speed;
                if (msg.payload.IsNumber()) v = msg.payload.GetNumber();
                else if (msg.payload.IsObject() && msg.payload.contains_key("value")) {
                    const Json x = msg.payload.get("value");
                    if (x.IsNumber()) v = x.GetNumber();
                }
                return SetSpeed(v);
            }
            if (msg.type == "seek") {
                double v = 0.0;
                if (msg.payload.IsNumber()) v = msg.payload.GetNumber();
                else if (msg.payload.IsObject() && msg.payload.contains_key("seconds")) {
                    const Json x = msg.payload.get("seconds");
                    if (x.IsNumber()) v = x.GetNumber();
                }
                return Seek(v);
            }
            return false;
        }

    private:
        enum class Backend { None, Mf, Mci };

        using MciSendFn = MCIERROR (WINAPI*)(LPCWSTR, LPWSTR, UINT, HWND);
        using MciErrorFn = BOOL (WINAPI*)(MCIERROR, LPWSTR, UINT);
        using MfStartupFn = HRESULT (WINAPI*)(ULONG, DWORD);
        using MfShutdownFn = HRESULT (WINAPI*)();
        using MfCreateFn = HRESULT (WINAPI*)(LPCWSTR, BOOL, MFP_CREATION_OPTIONS,
                                             IMFPMediaPlayerCallback*, HWND, IMFPMediaPlayer**);
        using CoInitFn = HRESULT (WINAPI*)(void*, DWORD);
        using CoUninitFn = void (WINAPI*)();

        static inline const GUID kPosition100ns{ 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };

        // Media Foundation
        HMODULE m_mfplat{ nullptr };
        HMODULE m_mfplay{ nullptr };
        HMODULE m_ole32{ nullptr };
        MfStartupFn m_mfStartup{ nullptr };
        MfShutdownFn m_mfShutdown{ nullptr };
        MfCreateFn m_mfCreate{ nullptr };
        CoInitFn m_coInit{ nullptr };
        CoUninitFn m_coUninit{ nullptr };
        IMFPMediaPlayer* m_mfPlayer{ nullptr };
        bool m_mfStarted{ false };
        bool m_comInitialized{ false };

        // MCI 兜底
        HMODULE m_winmm{ nullptr };
        MciSendFn m_send{ nullptr };
        MciErrorFn m_errorText{ nullptr };
        std::wstring m_alias{};

        Backend m_backend{ Backend::None };
        bool m_open{ false };
        bool m_speedSupported{ false };
        double m_speed{ 1.0 };
        int m_volume{ 100 };
        State m_state{ State::Closed };
        std::string m_error{};
        mutable MCIERROR m_lastError{ 0 };
        static inline std::atomic<int> s_nextId{ 0 };

        //------------------------------------------------------------------
        // 运行时符号
        //------------------------------------------------------------------
        template <typename Fn>
        static Fn ProcAddress(HMODULE mod, const char* name) {
            FARPROC p = GetProcAddress(mod, name);
            Fn fn = nullptr;
            static_assert(sizeof(fn) == sizeof(p), "function pointer size mismatch");
            std::memcpy(&fn, &p, sizeof(fn));
            return fn;
        }

        bool EnsureMf() {
            if (m_mfCreate != nullptr) return true;
            m_ole32 = LoadLibraryW(L"ole32.dll");
            m_mfplat = LoadLibraryW(L"mfplat.dll");
            m_mfplay = LoadLibraryW(L"mfplay.dll");
            if (m_ole32 == nullptr || m_mfplat == nullptr || m_mfplay == nullptr) {
                m_error = "Media Foundation DLL not found";
                return false;
            }
            m_coInit = ProcAddress<CoInitFn>(m_ole32, "CoInitializeEx");
            m_coUninit = ProcAddress<CoUninitFn>(m_ole32, "CoUninitialize");
            m_mfStartup = ProcAddress<MfStartupFn>(m_mfplat, "MFStartup");
            m_mfShutdown = ProcAddress<MfShutdownFn>(m_mfplat, "MFShutdown");
            m_mfCreate = ProcAddress<MfCreateFn>(m_mfplay, "MFPCreateMediaPlayer");
            if (m_coInit == nullptr || m_mfStartup == nullptr || m_mfCreate == nullptr) {
                m_error = "Media Foundation entry point missing";
                return false;
            }
            if (!m_comInitialized) {
                const HRESULT hr = m_coInit(nullptr, COINIT_APARTMENTTHREADED);
                m_comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
            }
            if (!m_mfStarted) {
                if (FAILED(m_mfStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
                    m_error = "MFStartup failed";
                    return false;
                }
                m_mfStarted = true;
            }
            return true;
        }

        bool OpenMf(const std::wstring& path) {
            if (!EnsureMf()) return false;
            IMFPMediaItem* item = nullptr;
            HRESULT hr = m_mfCreate(path.c_str(), FALSE, MFP_OPTION_NONE, nullptr,
                                    nullptr, &m_mfPlayer);
            if (FAILED(hr) || m_mfPlayer == nullptr) {
                m_error = "MFPCreateMediaPlayer failed";
                return false;
            }
            hr = m_mfPlayer->CreateMediaItemFromURL(path.c_str(), TRUE, 0, &item);
            if (FAILED(hr) || item == nullptr) {
                m_error = "MF CreateMediaItemFromURL failed";
                CloseMf();
                return false;
            }
            hr = m_mfPlayer->SetMediaItem(item);
            item->Release();
            if (FAILED(hr)) {
                m_error = "MF SetMediaItem failed";
                CloseMf();
                return false;
            }
            m_backend = Backend::Mf;
            m_open = true;
            m_state = State::Stopped;
            SetVolume(m_volume);
            SetSpeed(m_speed);
            return true;
        }

        void CloseMf() {
            if (m_mfPlayer != nullptr) {
                m_mfPlayer->Shutdown();
                m_mfPlayer->Release();
                m_mfPlayer = nullptr;
            }
            if (m_mfStarted && m_mfShutdown != nullptr) {
                m_mfShutdown();
                m_mfStarted = false;
            }
            if (m_comInitialized && m_coUninit != nullptr) {
                m_coUninit();
                m_comInitialized = false;
            }
        }

        bool EnsureMci() {
            if (m_send != nullptr) return true;
            m_winmm = LoadLibraryW(L"winmm.dll");
            if (m_winmm == nullptr) { m_error = "winmm.dll not found"; return false; }
            m_send = ProcAddress<MciSendFn>(m_winmm, "mciSendStringW");
            m_errorText = ProcAddress<MciErrorFn>(m_winmm, "mciGetErrorStringW");
            if (m_send == nullptr) { m_error = "mciSendStringW not found"; return false; }
            return true;
        }

        bool OpenMci(const std::wstring& path) {
            if (!EnsureMci()) return false;
            m_alias = L"tui_audio_" + std::to_wstring(++s_nextId);
            const std::wstring quoted = L"\"" + path + L"\"";
            if (!Send(L"open " + quoted + L" alias " + m_alias)) {
                if (!Send(L"open " + quoted + L" type mpegvideo alias " + m_alias)) {
                    m_error = LastMciError();
                    m_alias.clear();
                    return false;
                }
            }
            Send(L"set " + m_alias + L" time format milliseconds");
            m_backend = Backend::Mci;
            m_open = true;
            m_state = State::Stopped;
            SetVolume(m_volume);
            SetSpeed(m_speed);
            return true;
        }

        bool Send(const std::wstring& cmd) const {
            if (m_send == nullptr) return false;
            m_lastError = m_send(cmd.c_str(), nullptr, 0, nullptr);
            return m_lastError == 0;
        }

        int QueryNumber(const std::wstring& what) const {
            if (!m_open || m_alias.empty() || m_send == nullptr) return -1;
            wchar_t ret[128]{};
            if (m_send((L"status " + m_alias + L" " + what).c_str(), ret, 128, nullptr) != 0) return -1;
            return static_cast<int>(std::wcstol(ret, nullptr, 10));
        }

        std::string LastMciError() const {
            if (m_errorText == nullptr || m_lastError == 0) return "MCI command failed";
            wchar_t text[256]{};
            m_errorText(m_lastError, text, 256);
            const int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
            if (need <= 1) return "MCI command failed";
            std::string out(static_cast<size_t>(need - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), need, nullptr, nullptr);
            return out;
        }
    };

} // namespace Terminal
