// Simple UI menu composed of buttons laid out in a region
#pragma once

#include <SDL.h>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <SDL_ttf.h>
#include <cctype>

// Hotkey mapping may be added later; keep API surface in MenuButton.

struct HotKey {
    SDL_Keycode key;
    bool ctrl;
    bool alt;
    bool shift;
    bool super;  // Windows key / Command
};

struct MenuButton {
    std::string key;     // logical action key
    // Template text; supports $var substitution via 'vars'
    std::string text_tmpl;
    // Vars for $var expansion
    std::unordered_map<std::string, std::string> vars;
    bool enabled = true;
    std::function<void()> on_click; // invoked on left click
    HotKey hotkey;       // hot key to invoke the button (optional)
    std::function<void()> fn;

    std::string expanded_text() const {
        if (text_tmpl.find('$') == std::string::npos) return text_tmpl;
        std::string out; out.reserve(text_tmpl.size());
        for (size_t i = 0; i < text_tmpl.size();) {
            char c = text_tmpl[i];
            if (c == '$') {
                size_t j = i + 1;
                while (j < text_tmpl.size()) {
                    char d = text_tmpl[j];
                    if (!(std::isalnum((unsigned char)d) || d == '_')) break;
                    ++j;
                }
                if (j > i + 1) {
                    std::string name = text_tmpl.substr(i + 1, j - (i + 1));
                    auto it = vars.find(name);
                    if (it != vars.end()) out += it->second;
                    i = j; continue;
                }
            }
            out.push_back(c); ++i;
        }
        return out;
    }
};

class Menu {
public:
    enum class FillOrder { TopToBottom, LeftToRight };

    void set_area(int x, int y, int w, int h) { area_ = {x,y,w,h}; }
    void set_fill(FillOrder f) { fill_ = f; }
    void set_button_size(int w, int h) { bw_ = w; bh_ = h; }
    void set_gap(int px) { gap_ = px; }

    void set_colors(const std::unordered_map<std::string, SDL_Color>& bg,
                    const SDL_Color& text) { colors_ = bg; text_ = text; }

    void add_button(MenuButton b) { buttons_.push_back(std::move(b)); layout_dirty_ = true; }

    // Returns true if the click was handled
    bool handle_click(int mx, int my) {
        if (!point_in_area(mx, my)) return false;
        ensure_layout();
        for (size_t i = 0; i < buttons_.size(); ++i) {
            SDL_Point p{mx,my};
            if (SDL_PointInRect(&p, &slots_[i])) {
                if (buttons_[i].enabled && buttons_[i].on_click) buttons_[i].on_click();
                return true;
            }
        }
        return true; // inside menu area even if no button matched
    }

    void draw(SDL_Renderer* ren, TTF_Font* font) {
        ensure_layout();
        for (size_t i = 0; i < buttons_.size(); ++i) {
            const auto& b = buttons_[i];
            const SDL_Rect& r = slots_[i];
            SDL_Color bg = lookup_color(b.key);
            SDL_SetRenderDrawColor(ren, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, 255,255,255,180);
            SDL_RenderDrawRect(ren, &r);
            const std::string text = b.expanded_text();
            if (font && !text.empty()) {
                SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), text_);
                if (s) { SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s); SDL_Rect dst{ r.x + (r.w - s->w)/2, r.y + (r.h - s->h)/2, s->w, s->h }; SDL_RenderCopy(ren, t, nullptr, &dst); SDL_DestroyTexture(t); SDL_FreeSurface(s); }
            }
        }
    }

private:
    bool point_in_area(int x, int y) const { return x >= area_.x && y >= area_.y && x < area_.x + area_.w && y < area_.y + area_.h; }
    SDL_Color lookup_color(const std::string& key) const {
        auto it = colors_.find(key);
        if (it != colors_.end()) return it->second;
        // fallback
        return SDL_Color{80, 120, 160, 255};
    }
    void ensure_layout() {
        if (!layout_dirty_) return;
        slots_.clear(); slots_.resize(buttons_.size());
        int x = area_.x, y = area_.y;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            slots_[i] = SDL_Rect{ x, y, bw_, bh_ };
            if (fill_ == FillOrder::TopToBottom) y += bh_ + gap_; else x += bw_ + gap_;
        }
        layout_dirty_ = false;
    }

    SDL_Rect area_{0,0,0,0};
    int bw_ = 120, bh_ = 32, gap_ = 8;
    FillOrder fill_ = FillOrder::TopToBottom;
    std::vector<MenuButton> buttons_;
    std::vector<SDL_Rect> slots_;
    bool layout_dirty_ = true;
    std::unordered_map<std::string, SDL_Color> colors_;
    SDL_Color text_{235,235,235,255};
};
