#pragma once

#include <memory>
#include <string_view>

namespace layoutfix {

struct PasswordFocusState;

enum class WidgetRole { Other, Entry, Text, Password };

inline bool equals_password(std::string_view text) {
    constexpr std::string_view kPassword = "password";
    if (text.size() != kPassword.size()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<unsigned char>(ch - 'A' + 'a');
        }
        if (ch != static_cast<unsigned char>(kPassword[i])) {
            return false;
        }
    }
    return true;
}

// Роль password або атрибут text-input-type=password.
// Порожній атрибут і звичайне поле вводу сюди не потрапляють.
inline bool is_password_widget(WidgetRole role, std::string_view text_input_type) {
    if (role == WidgetRole::Password) {
        return true;
    }
    return equals_password(text_input_type);
}

// Знімок уже відомих властивостей. Невідома роль тут не є паролем.
struct FocusSnapshot {
    bool defunct = false;
    bool role_known = false;
    WidgetRole role = WidgetRole::Other;
    bool attribute_known = false;
    std::string_view text_input_type;
};

inline bool focus_snapshot_is_password(const FocusSnapshot& snap) {
    if (snap.defunct || !snap.role_known) {
        return false;
    }
    if (snap.role == WidgetRole::Password) {
        return true;
    }
    if (!snap.attribute_known) {
        return false;
    }
    return is_password_widget(snap.role, snap.text_input_type);
}

// Втрата фокуса знімає паузу для поля пароля або для віджета, який уже зник.
inline bool focus_loss_ends_password(const FocusSnapshot& snap) {
    return snap.defunct || focus_snapshot_is_password(snap);
}

class PasswordFocus {
public:
    PasswordFocus();
    ~PasswordFocus();
    PasswordFocus(const PasswordFocus&) = delete;
    PasswordFocus& operator=(const PasswordFocus&) = delete;

    bool open();
    bool active() const;
    bool take_activated();

private:
    std::unique_ptr<PasswordFocusState> impl_;
};

}  // namespace layoutfix
