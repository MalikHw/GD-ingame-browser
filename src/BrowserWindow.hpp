#pragma once

#ifdef GEODE_IS_WINDOWS

#include <vector>
#include <memory>
#include <string>
#include "win/WebViewTab.hpp"

namespace browser {

struct Tab {
    std::unique_ptr<win::WebViewTab> webview;
    std::string displayTitle = "New Tab"; // falls back to URL
    char urlBuf[2048] = "";
    int id = 0;
};

class BrowserWindow {
public:
    static BrowserWindow& get();

    void init();

    void shutdown();

    void draw();

    void toggleVisible();
    [[nodiscard]] bool isVisible() const { return m_visible; }

    void focusChrome();
    void clearBrowserData(bool siteData, const std::string& action);

private:
    BrowserWindow() = default;

    void drawRuntimeMissingWindow();
    void drawBrowserWindow();
    void drawTabBar();
    void drawToolbar();

    Tab& activeTab();
    void addTab(const std::string& url = "");
    void closeTab(int index);
    void switchToTab(int index);

    void navigateFromUrlBar(Tab& tab);
    void updateDisplayTitle(Tab& tab);

    win::WebViewEnvironment m_env;
    std::vector<Tab> m_tabs;
    int m_activeIndex = -1;
    int m_nextTabId = 1;

    bool m_visible = false;
    bool m_runtimeAvailable = false;
    bool m_runtimeChecked = false;
    bool m_envReady = false;
    bool m_envFailed = false;
    HRESULT m_envFailureCode = S_OK;

    bool m_wantsChromeFocus = false;
};

bool looksLikeUrl(const std::string& text);

std::string resolveNavigationTarget(const std::string& input);

} // namespace browser

#endif // GEODE_IS_WINDOWS
