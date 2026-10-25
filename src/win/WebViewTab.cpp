#ifdef GEODE_IS_WINDOWS

#include "WebViewTab.hpp"
#include <Geode/Geode.hpp>
#include <WebView2EnvironmentOptions.h>
#include <wrl/event.h>
#include <filesystem>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;
using Microsoft::WRL::Callback;

namespace browser::win {

namespace {
    const wchar_t* kChildClassName = L"ClaudeBrowserWebViewHost";

    LRESULT CALLBACK childWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void ensureChildClassRegistered(HINSTANCE hInst) {
        static bool registered = false;
        if (registered) return;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = childWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = kChildClassName;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        RegisterClassExW(&wc);
        registered = true;
    }
}

// WebViewEnvironment

bool WebViewEnvironment::checkRuntimeAvailable(std::string* outVersion) {
    LPWSTR versionStr = nullptr;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &versionStr);

    bool available = SUCCEEDED(hr) && versionStr != nullptr;
    if (available && outVersion) {
        int len = WideCharToMultiByte(CP_UTF8, 0, versionStr, -1, nullptr, 0, nullptr, nullptr);
        outVersion->resize(len > 0 ? len - 1 : 0);
        if (len > 0) {
            WideCharToMultiByte(CP_UTF8, 0, versionStr, -1, outVersion->data(), len, nullptr, nullptr);
        }
    }
    if (versionStr) CoTaskMemFree(versionStr);
    return available;
}

void WebViewEnvironment::initialize(std::function<void()> onReady, std::function<void(HRESULT)> onFailed) {
    auto userDataDir = Mod::get()->getSaveDir() / "webview2-profile";
    std::error_code ec;
    std::filesystem::create_directories(userDataDir, ec);

    ComPtr<ICoreWebView2EnvironmentOptions> options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, // use the installed Evergreen WebView2 Runtime
        userDataDir.wstring().c_str(),
        options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, onReady, onFailed](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    log::error("[Browser] CreateCoreWebView2Environment failed: 0x{:08x}", static_cast<unsigned>(result));
                    if (onFailed) onFailed(result);
                    return S_OK;
                }
                m_env = env;
                log::info("[Browser] WebView2 environment ready");
                if (onReady) onReady();
                return S_OK;
            }
        ).Get()
    );

    if (FAILED(hr)) {
        log::error("[Browser] CreateCoreWebView2EnvironmentWithOptions call itself failed: 0x{:08x}", static_cast<unsigned>(hr));
        if (onFailed) onFailed(hr);
    }
}

void WebViewEnvironment::shutdown() {
    m_env.Reset();
}

// WebViewTab

WebViewTab::~WebViewTab() {
    destroy();
}

void WebViewTab::create(HWND parent, ICoreWebView2Environment* env, const std::string& initialUrl) {
    if (m_hwnd) return; // already created

    m_parent = parent;
    m_pendingInitialUrl = initialUrl;

    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    ensureChildClassRegistered(hInst);

    m_hwnd = CreateWindowExW(
        0,
        kChildClassName,
        L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0,
        parent,
        nullptr,
        hInst,
        nullptr
    );

    if (!m_hwnd) {
        log::error("[Browser] Failed to create child HWND for tab (GetLastError={})", GetLastError());
        return;
    }

    env->CreateCoreWebView2Controller(
        m_hwnd,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                if (FAILED(result) || !controller) {
                    log::error("[Browser] CreateCoreWebView2Controller failed: 0x{:08x}", static_cast<unsigned>(result));
                    return S_OK;
                }
                this->attachController(controller);
                return S_OK;
            }
        ).Get()
    );
}

void WebViewTab::attachController(ICoreWebView2Controller* controller) {
    m_controller = controller;
    m_controller->get_CoreWebView2(&m_webview);
    if (!m_webview) {
        log::error("[Browser] Controller had no CoreWebView2 attached");
        return;
    }

    m_controller->put_IsVisible(FALSE);

    // title changes
    m_webview->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                LPWSTR title = nullptr;
                if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, title, -1, nullptr, 0, nullptr, nullptr);
                    std::string utf8(len > 0 ? len - 1 : 0, '\0');
                    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, title, -1, utf8.data(), len, nullptr, nullptr);
                    CoTaskMemFree(title);
                    m_title = utf8;
                }
                if (onTitleChanged) onTitleChanged(*this);
                return S_OK;
            }
        ).Get(),
        &m_titleToken
    );

    // URL / navigation
    m_webview->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                LPWSTR src = nullptr;
                if (SUCCEEDED(sender->get_Source(&src)) && src) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
                    std::string utf8(len > 0 ? len - 1 : 0, '\0');
                    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, src, -1, utf8.data(), len, nullptr, nullptr);
                    CoTaskMemFree(src);
                    m_url = utf8;
                }
                refreshNavState();
                if (onNavStateChanged) onNavStateChanged(*this);
                return S_OK;
            }
        ).Get(),
        &m_sourceToken
    );

    m_webview->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) -> HRESULT {
                m_isLoading = true;
                if (onNavStateChanged) onNavStateChanged(*this);
                return S_OK;
            }
        ).Get(),
        &m_navStartToken
    );

    m_webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                m_isLoading = false;
                refreshNavState();
                if (onNavStateChanged) onNavStateChanged(*this);
                return S_OK;
            }
        ).Get(),
        &m_navCompletedToken
    );

    m_webview->add_HistoryChanged(
        Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this](ICoreWebView2*, IUnknown*) -> HRESULT {
                refreshNavState();
                if (onNavStateChanged) onNavStateChanged(*this);
                return S_OK;
            }
        ).Get(),
        &m_historyToken
    );

    m_webview->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (args && SUCCEEDED(args->get_Uri(&uri)) && uri) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, uri, -1, nullptr, 0, nullptr, nullptr);
                    std::string utf8(len > 0 ? len - 1 : 0, '\0');
                    if (len > 0) {
                        WideCharToMultiByte(CP_UTF8, 0, uri, -1, utf8.data(), len, nullptr, nullptr);
                    }
                    if (onNewWindowRequested) {
                        onNewWindowRequested(utf8);
                    }
                    CoTaskMemFree(uri);
                }
                if (args) args->put_Handled(TRUE);
                return S_OK;
            }
        ).Get(),
        &m_newWindowToken
    );

    if (!m_pendingInitialUrl.empty()) {
        navigate(m_pendingInitialUrl);
    }
}

void WebViewTab::applySiteVolume() {
    if (!m_webview) return;
    auto volume = Mod::get()->getSettingValue<double>("site-volume");
    auto script = std::wstring(L"document.querySelectorAll('audio,video').forEach(function(e){e.volume=")
        + std::to_wstring(volume) + L";});";
    m_webview->ExecuteScript(script.c_str(), nullptr);
}

void WebViewTab::refreshNavState() {
    if (!m_webview) return;
    BOOL back = FALSE, fwd = FALSE;
    m_webview->get_CanGoBack(&back);
    m_webview->get_CanGoForward(&fwd);
    m_canGoBack = back;
    m_canGoForward = fwd;
}

void WebViewTab::destroy() {
    if (m_webview) {
        m_webview->remove_DocumentTitleChanged(m_titleToken);
        m_webview->remove_SourceChanged(m_sourceToken);
        m_webview->remove_NavigationStarting(m_navStartToken);
        m_webview->remove_NavigationCompleted(m_navCompletedToken);
        m_webview->remove_HistoryChanged(m_historyToken);
        m_webview->remove_NewWindowRequested(m_newWindowToken);
        m_webview.Reset();
    }

    if (m_controller) {
        m_controller->Close();
        m_controller.Reset();
    }

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void WebViewTab::navigate(const std::string& url) {
    if (!m_webview) {
        m_pendingInitialUrl = url; // not ready yet, apply once attached
        return;
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wurl(wlen > 0 ? wlen - 1 : 0, L'\0');
    if (wlen > 0) MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl.data(), wlen);
    m_webview->Navigate(wurl.c_str());
    applySiteVolume();
}

void WebViewTab::goBack() { if (m_webview) m_webview->GoBack(); }
void WebViewTab::goForward() { if (m_webview) m_webview->GoForward(); }
void WebViewTab::reload() { if (m_webview) m_webview->Reload(); }
void WebViewTab::stop() { if (m_webview) m_webview->Stop(); }

void WebViewTab::setActive(bool active) {
    m_active = active;
    if (!active) {
        if (m_controller) m_controller->put_IsVisible(FALSE);
        hideChild(m_hwnd);
    }
}

void WebViewTab::syncRect(const Rect& rect) {
    if (!m_hwnd) return;

    syncChildToRect(m_hwnd, rect);

    if (!m_controller) return;

    bool visible = rect.w > 0.f && rect.h > 0.f && m_active;
    m_controller->put_IsVisible(visible ? TRUE : FALSE);
    if (!visible) return;

    RECT bounds{0, 0, static_cast<LONG>(rect.w), static_cast<LONG>(rect.h)};
    m_controller->put_Bounds(bounds);
}

void WebViewTab::focusWebview() {
    if (m_controller) {
        m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
}

} // namespace browser::win

#endif // GEODE_IS_WINDOWS
