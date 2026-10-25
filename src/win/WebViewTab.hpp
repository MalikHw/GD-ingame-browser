#pragma once

#ifdef GEODE_IS_WINDOWS

#include <Windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <functional>
#include "GDWindow.hpp"

namespace browser::win {

using Microsoft::WRL::ComPtr;

class WebViewTab {
public:
    using TitleChangedCallback = std::function<void(WebViewTab&)>;
    using NavStateChangedCallback = std::function<void(WebViewTab&)>;
    using NewWindowRequestedCallback = std::function<void(const std::string&)>;

    WebViewTab() = default;
    ~WebViewTab();

    WebViewTab(const WebViewTab&) = delete;
    WebViewTab& operator=(const WebViewTab&) = delete;

    void create(HWND parent, ICoreWebView2Environment* env, const std::string& initialUrl);

    void destroy();

    [[nodiscard]] bool isReady() const { return m_webview != nullptr; }
    [[nodiscard]] HWND hwnd() const { return m_hwnd; }

    // navigation
    void navigate(const std::string& url);
    void goBack();
    void goForward();
    void reload();
    void stop();

    [[nodiscard]] bool canGoBack() const { return m_canGoBack; }
    [[nodiscard]] bool canGoForward() const { return m_canGoForward; }
    [[nodiscard]] bool isLoading() const { return m_isLoading; }

    // display state
    [[nodiscard]] const std::string& title() const { return m_title; }
    [[nodiscard]] const std::string& url() const { return m_url; }

    void syncRect(const Rect& rect);
    void setActive(bool active);

    TitleChangedCallback onTitleChanged;
    NavStateChangedCallback onNavStateChanged;
    NewWindowRequestedCallback onNewWindowRequested;

    void focusWebview();

private:
    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;
    ComPtr<ICoreWebView2Controller> m_controller;
    ComPtr<ICoreWebView2> m_webview;

    std::string m_title;
    std::string m_url;
    std::string m_pendingInitialUrl;
    bool m_canGoBack = false;
    bool m_canGoForward = false;
    bool m_isLoading = false;
    bool m_active = false;

    EventRegistrationToken m_titleToken{};
    EventRegistrationToken m_sourceToken{};
    EventRegistrationToken m_navStartToken{};
    EventRegistrationToken m_navCompletedToken{};
    EventRegistrationToken m_historyToken{};
    EventRegistrationToken m_newWindowToken{};

    void attachController(ICoreWebView2Controller* controller);
    void refreshNavState();
    void applySiteVolume();
};

class WebViewEnvironment {
public:
    void initialize(std::function<void()> onReady, std::function<void(HRESULT)> onFailed);

    void shutdown();

    [[nodiscard]] bool isReady() const { return m_env != nullptr; }
    [[nodiscard]] ICoreWebView2Environment* get() const { return m_env.Get(); }
    
    static bool checkRuntimeAvailable(std::string* outVersion = nullptr);

private:
    ComPtr<ICoreWebView2Environment> m_env;
};

} // namespace browser::win

#endif // GEODE_IS_WINDOWS
