#ifdef GEODE_IS_WINDOWS

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "BrowserWindow.hpp"

using namespace geode::prelude;

namespace browser {

class BrowserDataButtonSetting final : public SettingV3 {
    std::string m_key;

public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    ) {
        auto result = std::make_shared<BrowserDataButtonSetting>();
        auto root = checkJson(json, "BrowserDataButtonSetting");
        result->m_key = key;
        result->init(key, modID, root);
        result->parseNameAndDescription(root);
        result->parseEnableIf(root);
        root.checkUnknownKeys();
        return root.ok(std::static_pointer_cast<SettingV3>(result));
    }


    bool load(matjson::Value const&) override { return true; }
    bool save(matjson::Value&) const override { return true; }
    bool isDefaultValue() const override { return true; }
    void reset() override {}

    bool isSiteData() const {
        return m_key == "clear-site-data";
    }

    SettingNodeV3* createNode(float width) override;
};

class BrowserDataButtonNode final : public SettingNodeV3 {
    ButtonSprite* m_buttonSprite = nullptr;
    CCMenuItemSpriteExtra* m_button = nullptr;
    std::shared_ptr<BrowserDataButtonSetting> m_setting;

    bool init(std::shared_ptr<BrowserDataButtonSetting> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;
        m_setting = std::move(setting);

        m_buttonSprite = ButtonSprite::create(
            m_setting->isSiteData() ? "Clear site data" : "Clear cache",
            "goldFont.fnt", "GJ_button_01.png", .8f
        );
        m_buttonSprite->setScale(.5f);
        m_button = CCMenuItemSpriteExtra::create(
            m_buttonSprite, this, menu_selector(BrowserDataButtonNode::onButton)
        );
        getButtonMenu()->addChildAtPosition(m_button, Anchor::Center);
        getButtonMenu()->setContentWidth(60);
        getButtonMenu()->updateLayout();
        updateState(nullptr);
        return true;
    }

    void updateState(CCNode* invoker) override {
        SettingNodeV3::updateState(invoker);
        auto enabled = m_setting->shouldEnable();
        m_button->setEnabled(enabled);
        m_buttonSprite->setOpacity(enabled ? 255 : 155);
        m_buttonSprite->setColor(enabled ? ccWHITE : ccGRAY);
    }

    void onButton(CCObject*) {
        BrowserWindow::get().clearBrowserData(
            m_setting->isSiteData(),
            m_setting->isSiteData() ? "cookies and site data" : "the browser cache"
        );
    }

public:
    static BrowserDataButtonNode* create(
        std::shared_ptr<BrowserDataButtonSetting> setting, float width
    ) {
        auto result = new BrowserDataButtonNode();
        if (result->init(std::move(setting), width)) {
            result->autorelease();
            return result;
        }
        delete result;
        return nullptr;
    }

    void onCommit() override {} // crap forgot this
    void onResetToDefault() override {}

    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue() const override { return false; }
};

SettingNodeV3* BrowserDataButtonSetting::createNode(float width) {
    return BrowserDataButtonNode::create(
        std::static_pointer_cast<BrowserDataButtonSetting>(shared_from_this()), width
    );
}

} // namespace browser

$on_mod(Loaded) {
    (void)Mod::get()->registerCustomSettingType(
        "browser-data-button", &browser::BrowserDataButtonSetting::parse
    );
}

#endif
