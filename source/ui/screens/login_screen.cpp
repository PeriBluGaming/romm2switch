#include "ui/screens/login_screen.hpp"
#include "ui/renderer.hpp"

static constexpr int HEADER_H = 60;
static constexpr int STATUS_H = 44;

LoginScreen::LoginScreen(Renderer& renderer, NavigateFn navigate,
                         const romm::Config& existing, SaveCallback onSave)
    : Screen(renderer, std::move(navigate))
    , m_config(existing)
    , m_onSave(std::move(onSave))
    , m_fieldLabels({"Server URL", "Username", "Password", "Download Path"})
{
    m_fields = { &m_config.serverUrl, &m_config.username,
                 &m_config.password,  &m_config.downloadPath };
}

void LoginScreen::onEnter() {
    m_statusMsg.clear();
    m_editing = false;
    SDL_StopTextInput();
}

bool LoginScreen::update(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN) {
        if (m_editing) {
            handleTextInput(event);
            return true;
        }
        switch (event.key.keysym.sym) {
        case SDLK_UP:
            if (selectedIndex() > 0)
                m_selectedField = static_cast<Field>(selectedIndex() - 1);
            break;
        case SDLK_DOWN:
            if (selectedIndex() < static_cast<int>(Field::COUNT) - 1)
                m_selectedField = static_cast<Field>(selectedIndex() + 1);
            break;
        case SDLK_RETURN:   // A button / Enter — start editing
        case SDLK_KP_ENTER:
            m_editing = true;
            m_statusMsg.clear();
            SDL_StartTextInput();
            break;
        case SDLK_b:        // B button — go back / cancel edit
            navigateTo("main", 0);
            break;
        case SDLK_x:        // X button — save
        {
            // Basic validation
            if (m_config.serverUrl.empty()) {
                m_statusMsg    = "Server URL must not be empty.";
                m_statusIsError = true;
                break;
            }
            m_statusMsg.clear();
            m_onSave(m_config);
            navigateTo("main", 0);
            break;
        }
        default: break;
        }
    } else if (event.type == SDL_TEXTINPUT) {
        if (m_editing) {
            *m_fields[selectedIndex()] += event.text.text;
        }
    }
    return true;
}

void LoginScreen::handleTextInput(const SDL_Event& event) {
    std::string& field = *m_fields[selectedIndex()];
    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_ESCAPE:
            m_editing = false;
            SDL_StopTextInput();
            break;
        case SDLK_BACKSPACE:
            if (!field.empty()) field.pop_back();
            break;
        default: break;
        }
    }
}

void LoginScreen::render() {
    auto& R = m_renderer;
    R.fillRect(0, 0, SCREEN_W, SCREEN_H, Color::Background);
    R.drawHeader("RomM2Switch \xe2\x80\x94 Settings");

    int cy = HEADER_H + START_Y;
    for (int i = 0; i < static_cast<int>(Field::COUNT); ++i) {
        bool selected = (i == selectedIndex());
        bool editing  = selected && m_editing;

        // Label
        R.drawText(m_fieldLabels[static_cast<size_t>(i)],
                   FIELD_X - 200, cy + 16, Color::TextDim);

        // Input box
        SDL_Color boxColor = editing   ? Color::CardHover
                           : selected  ? Color::CardHover
                                       : Color::Card;
        R.fillRect(FIELD_X, cy, FIELD_W, FIELD_H, boxColor);
        R.drawRect(FIELD_X, cy, FIELD_W, FIELD_H,
                   editing ? Color::CardHover : Color::Separator);

        // Field value (mask password)
        const std::string& val = *m_fields[static_cast<size_t>(i)];
        std::string display;
        if (static_cast<Field>(i) == Field::Password) {
            display = std::string(val.size(), '*');
        } else {
            display = val;
        }
        if (editing) display += "|"; // cursor

        R.drawText(display, FIELD_X + 12, cy + (FIELD_H - 26) / 2,
                   Color::TextWhite);

        cy += SPACING;
    }

    // Status / error message
    if (!m_statusMsg.empty()) {
        SDL_Color col = m_statusIsError ? Color::Error : Color::Success;
        R.drawText(m_statusMsg, FIELD_X, cy + 10, col);
    }

    R.drawStatusBar("\xe2\x86\x91\xe2\x86\x93 Select  \xe2\x8f\xb5 Edit  Backspace Delete  \xe2\x8f\xb5 Confirm  X Save  B Cancel");
}
