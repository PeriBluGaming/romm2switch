#pragma once

#include "ui/renderer.hpp"
#include "ui/screens/screen.hpp"
#include "config.hpp"

#include <string>
#include <vector>
#include <functional>

// ---------------------------------------------------------------------------
// LoginScreen — enter server URL, username, password
// ---------------------------------------------------------------------------
class LoginScreen : public Screen {
public:
    // onSave: called when the user saves a valid configuration
    using SaveCallback = std::function<void(const romm::Config&)>;

    LoginScreen(Renderer& renderer, NavigateFn navigate,
                const romm::Config& existing, SaveCallback onSave);

    void onEnter() override;
    bool update(const SDL_Event& event) override;
    void render() override;

private:
    enum class Field { ServerUrl = 0, Username, Password, DownloadPath, COUNT };

    romm::Config m_config;
    SaveCallback m_onSave;

    Field m_selectedField = Field::ServerUrl;
    bool  m_editing       = false;
    std::string m_statusMsg;
    bool  m_statusIsError = false;

    // Text fields
    std::vector<std::string*> m_fields;
    std::vector<std::string>  m_fieldLabels;

    static constexpr int FIELD_H    = 60;
    static constexpr int FIELD_W    = 840;
    static constexpr int FIELD_X    = 220;
    static constexpr int START_Y    = 100;
    static constexpr int SPACING    = 80;

    void handleTextInput(const SDL_Event& event);
    int  selectedIndex() const { return static_cast<int>(m_selectedField); }
};
