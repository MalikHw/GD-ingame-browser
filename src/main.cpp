#include <Geode/Geode.hpp>

using namespace geode::prelude;

#ifdef GEODE_IS_WINDOWS

#include <imgui-cocos.hpp>
#include <imgui.h>
#include <Geode/loader/SettingV3.hpp>
#include "BrowserWindow.hpp"
#include "win/GDWindow.hpp"

$on_mod(Loaded) {
    ImGuiCocos::get()
        .setup([] {
            auto& style = ImGui::GetStyle();
            style.Alpha = 1.0f;
            style.WindowRounding = 3.0f;
            style.ChildRounding = 3.0f;
            style.FrameRounding = 3.0f;
            style.GrabRounding = 1.0f;
            style.GrabMinSize = 20.0f;
            style.Colors[ImGuiCol_Text] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.00f, 0.40f, 0.41f, 1.00f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.00f, 1.00f, 1.00f, 0.65f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.44f, 0.80f, 0.80f, 0.18f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.44f, 0.80f, 0.80f, 0.27f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.44f, 0.81f, 0.86f, 0.66f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.00f, 0.65f, 0.65f, 0.46f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.01f, 1.00f, 1.00f, 0.43f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.62f);
            auto fontPath = Mod::get()->getResourcesDir() / "OpenSans-Medium.ttf";
            ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 16.0f);
        })
        .draw([] {
            browser::BrowserWindow::get().draw();
        });

    browser::BrowserWindow::get().init();
    listenForKeybindSettingPresses("toggle-browser", [](geode::Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) {
            browser::BrowserWindow::get().toggleVisible();
        }
    });
}
$execute {
    geode::KeyboardInputEvent().listen([](auto& evt) {
        if (evt.action == geode::KeyboardInputData::Action::Press
            && evt.key == cocos2d::enumKeyCodes::KEY_Tab
            && (evt.modifiers & geode::KeyboardModifier::Control)) {
            browser::BrowserWindow::get().focusChrome();
        }
        return geode::ListenerResult::Propagate;
    }).leak();
}

#include <Geode/modify/CCEGLView.hpp>
class $modify(BrowserCCEGLViewHook, CCEGLView) {
    void toggleFullScreen(bool value, bool borderless, bool fix) {
        CCEGLView::toggleFullScreen(value, borderless, fix);
        browser::win::invalidateGDWindowCache();
    }
};

#else
$on_mod(Loaded) {
    log::warn("This mod only supports Windows; doing nothing on this platform.");
}

#endif
