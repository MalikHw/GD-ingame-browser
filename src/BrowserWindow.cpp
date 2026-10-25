#ifdef GEODE_IS_WINDOWS

#include "BrowserWindow.hpp"
#include "win/GDWindow.hpp"
#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>
#include <imgui.h>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cfloat>
#include <filesystem>

using namespace geode::prelude;

namespace browser {

// ============================================================================
// URL vs. search-query heuristic
// ============================================================================

namespace {
    std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    bool isIPv4(const std::string& s) {
        int parts = 0, dots = 0;
        std::stringstream ss(s);
        std::string seg;
        while (std::getline(ss, seg, '.')) {
            if (seg.empty() || seg.size() > 3) return false;
            for (char c : seg) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            int v = std::stoi(seg);
            if (v < 0 || v > 255) return false;
            parts++;
        }
        return parts == 4;
    }

    std::string urlEncode(const std::string& s) {
        std::ostringstream out;
        out << std::hex << std::uppercase;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out << c;
            } else if (c == ' ') {
                out << '+';
            } else {
                out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
            }
        }
        return out.str();
    }

    std::string newTabUrl() {
        auto engine = toLower(Mod::get()->getSettingValue<std::string>("search-engine"));
        if (engine == "brave")  return "https://search.brave.com";
        if (engine == "google") return "https://www.google.com";
        if (engine == "bing")   return "https://www.bing.com";
        if (engine == "yandex") return "https://yandex.com";
        return "https://duckduckgo.com";
    }
}

bool looksLikeUrl(const std::string& rawText) {
    std::string text = trim(rawText);
    if (text.empty()) return false;
    if (text.find(' ') != std::string::npos) return false; // queries can have spaces, hostnames can't

    std::string lower = toLower(text);

    // Explicit scheme -> definitely a URL.
    if (lower.find("://") != std::string::npos) return true;

    // localhost / bare IPv4, optionally with :port and/or a path.
    std::string hostPart = text;
    auto slashPos = hostPart.find('/');
    if (slashPos != std::string::npos) hostPart = hostPart.substr(0, slashPos);
    auto colonPos = hostPart.find(':');
    if (colonPos != std::string::npos) hostPart = hostPart.substr(0, colonPos);

    if (toLower(hostPart) == "localhost") return true;
    if (isIPv4(hostPart)) return true;

    // "word.word[...][/path]" with a plausible TLD at the end and no
    // spaces anywhere -- e.g. "github.com", "docs.geode-sdk.org/tutorials".
    auto dotPos = hostPart.rfind('.');
    if (dotPos != std::string::npos && dotPos + 1 < hostPart.size()) {
        std::string tld = hostPart.substr(dotPos + 1);
        bool tldLooksValid = tld.size() >= 2 && tld.size() <= 24 &&
            std::all_of(tld.begin(), tld.end(), [](unsigned char c) { return std::isalpha(c); });
        bool hostLooksValid = !hostPart.empty() &&
            std::all_of(hostPart.begin(), hostPart.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-' || c == '.';
            });
        if (tldLooksValid && hostLooksValid) return true;
    }

    return false;
}

std::string resolveNavigationTarget(const std::string& input) {
    std::string text = trim(input);
    if (looksLikeUrl(text)) {
        if (text.find("://") == std::string::npos) {
            return "https://" + text;
        }
        return text;
    }
    auto engine = Mod::get()->getSettingValue<std::string>("search-engine");
    if (engine == "brave") {
        return "https://search.brave.com/search?q=" + urlEncode(text);
    }
    if (engine == "google") {
        return "https://www.google.com/search?q=" + urlEncode(text);
    }
    if (engine == "bing") {
        return "https://www.bing.com/search?q=" + urlEncode(text);
    }
    if (engine == "yandex") {
        return "https://yandex.com/search/?text=" + urlEncode(text);
    }
    return "https://duckduckgo.com/?q=" + urlEncode(text);
}

// BrowserWindow

BrowserWindow& BrowserWindow::get() {
    static BrowserWindow instance;
    return instance;
}

void BrowserWindow::init() {
    m_runtimeAvailable = win::WebViewEnvironment::checkRuntimeAvailable();
    m_runtimeChecked = true;

    if (!m_runtimeAvailable) {
        log::warn("[Browser] WebView2 Runtime not found; browser will show an install prompt instead of webpages.");
        return;
    }

    m_env.initialize(
        [this] {
            m_envReady = true;
            // If the user already toggled the window open before the
            // (async) environment finished initializing, we'll have no
            // tabs yet -- give them one now.
            if (m_tabs.empty()) {
                addTab();
            } else {
                // Tabs created before the environment was ready only got
                // as far as queuing a pending URL; (re)create their
                // webviews for real now.
                for (auto& tab : m_tabs) {
                    if (!tab.webview->isReady()) {
                        auto* gdWindow = win::getGDWindow();
                        tab.webview->create(gdWindow, m_env.get(), tab.urlBuf);
                    }
                }
            }
        },
        [this](HRESULT hr) {
            m_envFailed = true;
            m_envFailureCode = hr;
        }
    );
}

void BrowserWindow::shutdown() {
    for (auto& tab : m_tabs) {
        tab.webview->destroy();
    }
    m_tabs.clear();
    m_activeIndex = -1;
    m_env.shutdown();
}

void BrowserWindow::toggleVisible() {
    m_visible = !m_visible;
    ImGuiCocos::get().setInputMode(
        m_visible ? ImGuiCocos::InputMode::Blocking : ImGuiCocos::InputMode::Default
    );
    if (!m_visible && m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_tabs.size())) {
        // Hide immediately rather than waiting for the next draw() to no-op
        // its way there, so there's no one-frame flash of a stale webview.
        activeTab().webview->setActive(false);
    }
    if (m_visible && m_tabs.empty() && m_envReady) {
        addTab();
    } else if (m_visible && m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_tabs.size())) {
        activeTab().webview->setActive(true);
    }
}

void BrowserWindow::clearBrowserData(bool siteData, const std::string& action) {
    createQuickPopup(
        "Clear browser data",
        fmt::format("This will close the browser tabs and clear {}. Continue?", action),
        "Cancel", "Clear",
        [this, siteData](auto, bool confirmed) {
            if (!confirmed) return;

            shutdown();
            std::error_code ec;
            auto profileDir = Mod::get()->getSaveDir() / "webview2-profile";
            if (siteData) {
                std::filesystem::remove_all(profileDir, ec);
            } else if (std::filesystem::exists(profileDir, ec)) {
                for (std::filesystem::recursive_directory_iterator it(profileDir, ec), end;
                     it != end && !ec; it.increment(ec)) {
                    if (!it->is_directory(ec)) continue;
                    const auto name = it->path().filename().string();
                    if (name == "Cache" || name == "Code Cache"
                        || name == "GPUCache" || name == "CacheStorage") {
                        std::filesystem::remove_all(it->path(), ec);
                        it.disable_recursion_pending();
                    }
                }
            }
            if (ec) {
                log::error("[Browser] Failed to clear WebView2 profile data: {}", ec.message());
                return;
            }

            m_envReady = false;
            m_envFailed = false;
            m_runtimeChecked = false;
            init();
        }
    );
}

void BrowserWindow::focusChrome() {
    auto* gdWindow = win::getGDWindow();
    if (gdWindow) {
        // This is the actual OS-level focus handoff: as long as the
        // webview child HWND holds Win32 focus, GD's window (and
        // therefore cocos2d's keyboard dispatcher, and therefore ImGui)
        // receives no WM_KEYDOWN/WM_CHAR at all. Pulling Win32 focus back
        // to the top-level window is what makes typing in the URL bar
        // possible again.
        SetFocus(gdWindow);
    }
    m_wantsChromeFocus = true;
}

Tab& BrowserWindow::activeTab() {
    return m_tabs[m_activeIndex];
}

void BrowserWindow::addTab(const std::string& url) {
    const auto initialUrl = url.empty() ? newTabUrl() : url;
    Tab tab;
    tab.id = m_nextTabId++;
    tab.webview = std::make_unique<win::WebViewTab>();
    strncpy_s(tab.urlBuf, initialUrl.c_str(), sizeof(tab.urlBuf) - 1);

    auto* gdWindow = win::getGDWindow();
    if (gdWindow && m_envReady) {
        tab.webview->create(gdWindow, m_env.get(), initialUrl);
    }
    // else: webview stays un-created; init()'s onReady callback (or the
    // next addTab-adjacent path) will create it once the environment/HWND
    // are available. urlBuf already holds the intended URL either way.

    m_tabs.push_back(std::move(tab));
    switchToTab(static_cast<int>(m_tabs.size()) - 1);

    Tab& added = m_tabs.back();
    added.webview->onTitleChanged = [this](win::WebViewTab& wv) {
        for (auto& t : m_tabs) {
            if (t.webview.get() == &wv) { updateDisplayTitle(t); break; }
        }
    };
    added.webview->onNavStateChanged = [this](win::WebViewTab& wv) {
        for (auto& t : m_tabs) {
            if (t.webview.get() == &wv) {
                updateDisplayTitle(t);
                // Keep the URL bar in sync with real navigation (e.g. the
                // user followed a link inside the page), but don't stomp
                // on text the user is actively typing/editing right now.
                if (!ImGui::IsItemActive()) {
                    strncpy_s(t.urlBuf, wv.url().c_str(), sizeof(t.urlBuf) - 1);
                }
                break;
            }
        }
    };
    added.webview->onNewWindowRequested = [this](const std::string& url) {
        addTab(url);
    };
}

void BrowserWindow::closeTab(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;

    m_tabs[index].webview->destroy();
    m_tabs.erase(m_tabs.begin() + index);

    if (m_tabs.empty()) {
        m_activeIndex = -1;
        addTab(); // never leave zero tabs open, matches normal browser UX
        return;
    }

    if (m_activeIndex >= static_cast<int>(m_tabs.size())) {
        m_activeIndex = static_cast<int>(m_tabs.size()) - 1;
    } else if (index < m_activeIndex) {
        m_activeIndex--;
    }
    switchToTab(m_activeIndex);
}

void BrowserWindow::switchToTab(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;

    if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_tabs.size()) && m_activeIndex != index) {
        m_tabs[m_activeIndex].webview->setActive(false);
    }
    m_activeIndex = index;
    m_tabs[index].webview->setActive(true);
}

void BrowserWindow::updateDisplayTitle(Tab& tab) {
    const auto& title = tab.webview->title();
    tab.displayTitle = !title.empty() ? title : (tab.webview->url().empty() ? "New Tab" : tab.webview->url());
    // Tab strip real estate is limited; truncate long titles for display
    // only (full title is still in `title`/tooltip if you want to add one).
    if (tab.displayTitle.size() > 28) {
        tab.displayTitle = tab.displayTitle.substr(0, 27) + "...";
    }
}

void BrowserWindow::navigateFromUrlBar(Tab& tab) {
    std::string target = resolveNavigationTarget(tab.urlBuf);
    tab.webview->navigate(target);
}

// ============================================================================
// Drawing
// ============================================================================

void BrowserWindow::draw() {
    if (!m_runtimeChecked) return; // init() hasn't run yet

    if (!m_visible) {
        // Nothing on screen this frame -- make sure no tab's HWND is left
        // dangling visible from before the window was hidden.
        if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_tabs.size())) {
            m_tabs[m_activeIndex].webview->syncRect(win::Rect{0, 0, 0, 0});
        }
        return;
    }

    if (!m_runtimeAvailable) {
        drawRuntimeMissingWindow();
        return;
    }

    if (m_envFailed) {
        ImGui::SetNextWindowSize(ImVec2(420, 140), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Browser - Error", &m_visible)) {
            ImGui::TextWrapped("Failed to start the WebView2 environment (HRESULT 0x%08X).", static_cast<unsigned>(m_envFailureCode));
            ImGui::TextWrapped("This usually means the WebView2 Runtime is present but couldn't write its profile folder, or a conflicting WebView2 process is stuck.");
        }
        ImGui::End();
        return;
    }

    if (!m_envReady) {
        ImGui::SetNextWindowSize(ImVec2(320, 90), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Ingame Browser", &m_visible)) {
            ImGui::TextUnformatted("Starting WebView2...");
        }
        ImGui::End();
        return;
    }

    drawBrowserWindow();
}

void BrowserWindow::drawRuntimeMissingWindow() {
    ImGui::SetNextWindowSize(ImVec2(460, 160), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Browser - WebView2 Runtime Required", &m_visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextWrapped(
            "This feature needs the Microsoft Edge WebView2 Runtime, which "
            "isn't installed (or isn't detected)."
        );
        ImGui::Spacing();
        ImGui::TextWrapped("Most Windows 10/11 installs already have it. If not, install it from:");
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.f, 1.f), "https://go.microsoft.com/fwlink/p/?LinkId=2124703");
        if (ImGui::IsItemClicked()) {
            geode::utils::web::openLinkInBrowser("https://go.microsoft.com/fwlink/p/?LinkId=2124703");
        }
        ImGui::Spacing();
        if (ImGui::Button("Check again")) {
            m_runtimeAvailable = win::WebViewEnvironment::checkRuntimeAvailable();
            if (m_runtimeAvailable) init();
        }
    }
    ImGui::End();
}

void BrowserWindow::drawBrowserWindow() {
    ImGui::SetNextWindowSize(ImVec2(960, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 240), ImVec2(FLT_MAX, FLT_MAX));

    // No ImGuiWindowFlags_NoResize/NoMove: the whole point is this behaves
    // like a normal ImGui window, resize grips included. See the "inset"
    // gotcha below for why that's specifically compatible with the HWND
    // overlay trick.
    if (!ImGui::Begin("Ingame Browser", &m_visible, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        // Window collapsed -- still need to hide the active webview.
        if (m_activeIndex >= 0) activeTab().webview->syncRect(win::Rect{0, 0, 0, 0});
        return;
    }

    drawTabBar();

    ImGui::Separator();

    drawToolbar();

    ImGui::Separator();

    // Remaining space in the window becomes the webview's content rect.
    // GetCursorScreenPos()/GetContentRegionAvail() are in the same pixel
    // space as io.DisplaySize under gd-imgui-cocos (see the coordinate
    // system note at the top of win/GDWindow.cpp) -- i.e. already directly
    // usable as GD-window-client-relative coordinates for MoveWindow().
    ImVec2 contentPos = ImGui::GetCursorScreenPos();
    ImVec2 contentSize = ImGui::GetContentRegionAvail();

    // Reserve the space in ImGui's layout (so the window's own border/
    // background still gets drawn under where the webview will sit, and
    // so auto-resize/scrollbar logic sees it as occupied) without ImGui
    // actually drawing anything there -- the real pixels come from the
    // WebView2 child HWND compositing on top.
    ImGui::InvisibleButton("##webview_host_area", contentSize, ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemClicked()) {
        // Clicking the live page area: let OS focus go wherever the user
        // actually clicked (into the real child HWND -- see focus notes),
        // i.e. explicitly do NOT set chromeClickedThisFrame here.
    }

    bool windowIsActuallyVisible = m_visible && !ImGui::IsWindowCollapsed();

    if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_tabs.size())) {
        win::Rect rect;
        if (windowIsActuallyVisible) {
            // Inset by a few px so ImGui's own window border and the
            // corner/edge resize grips remain on top of (and clickable
            // over) the webview, instead of the child HWND fully covering
            // them -- see the z-order gotcha below.
            rect = win::Rect{contentPos.x, contentPos.y, contentSize.x, contentSize.y}.inset(2.5f);
        }
        activeTab().webview->syncRect(rect);
    }

    ImGui::End();

    if (m_wantsChromeFocus) {
        ImGui::SetWindowFocus("Ingame Browser");
        m_wantsChromeFocus = false;
    }
}

void BrowserWindow::drawTabBar() {
    if (ImGui::BeginTabBar("##browser_tabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        int indexToClose = -1;

        for (int i = 0; i < static_cast<int>(m_tabs.size()); i++) {
            Tab& tab = m_tabs[i];
            bool open = true;

            ImGuiTabItemFlags flags = (i == m_activeIndex) ? ImGuiTabItemFlags_SetSelected : 0;

            ImGui::PushID(tab.id);
            // The "x" close button comes from passing `&open` -- ImGui
            // draws it for us and flips `open` to false when clicked.
            bool selected = ImGui::BeginTabItem(tab.displayTitle.c_str(), &open, flags);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                switchToTab(i);
                // Clicking a tab is chrome input -- make sure OS keyboard
                // focus is (back) on GD's window, in case it was sitting
                // on the previous tab's webview HWND.
                if (auto* gdWindow = win::getGDWindow()) SetFocus(gdWindow);
            }
            if (selected) {
                if (m_activeIndex != i) switchToTab(i);
                ImGui::EndTabItem();
            }
            if (!open) indexToClose = i;
            ImGui::PopID();
        }

        // Trailing "+" tab button -- this is ImGui's documented idiom for
        // an "add tab" affordance inside a native tab bar (see
        // imgui_demo.cpp's "Tabs" section).
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
            addTab();
        }

        ImGui::EndTabBar();

        if (indexToClose != -1) closeTab(indexToClose);
    }
}

void BrowserWindow::drawToolbar() {
    if (m_activeIndex < 0 || m_activeIndex >= static_cast<int>(m_tabs.size())) return;
    Tab& tab = m_tabs[m_activeIndex];
    auto& wv = *tab.webview;
    auto showTooltip = [](const char* text) {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
    };

    // Any click on the toolbar is chrome input -- same focus-handoff
    // reasoning as the tab bar (see drawTabBar()). Doing this once here
    // instead of after every single button keeps it from getting
    // forgotten when buttons are added/reordered later.
    auto reclaimFocusIfClicked = [] {
        if (ImGui::IsItemClicked()) {
            if (auto* gdWindow = win::getGDWindow()) SetFocus(gdWindow);
        }
    };

    ImGui::BeginDisabled(!wv.canGoBack());
    if (ImGui::Button("<-")) wv.goBack();
    showTooltip("Go back");
    reclaimFocusIfClicked();
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(!wv.canGoForward());
    if (ImGui::Button("->")) wv.goForward();
    showTooltip("Go forward");
    reclaimFocusIfClicked();
    ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Button(wv.isLoading() ? "X" : "<>")) {
        wv.isLoading() ? wv.stop() : wv.reload();
    }
    showTooltip(wv.isLoading() ? "Stop loading" : "Refresh");
    reclaimFocusIfClicked();
    ImGui::SameLine();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGuiInputTextFlags urlFlags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (ImGui::InputText("##url", tab.urlBuf, sizeof(tab.urlBuf), urlFlags)) {
        navigateFromUrlBar(tab);
        // Keep focus in the URL bar after submitting rather than letting
        // click-through logic bounce it to the webview -- most desktop
        // browsers do the same on Enter.
        ImGui::SetKeyboardFocusHere(-1);
    }
    if (ImGui::IsItemActivated()) {
        // Same reasoning as the tab-click case: clicking into the URL box
        // is chrome input, so OS keyboard focus needs to be on GD's
        // window for ImGui to actually see the keystrokes.
        if (auto* gdWindow = win::getGDWindow()) SetFocus(gdWindow);
    }
}

} // namespace browser

#endif // GEODE_IS_WINDOWS
