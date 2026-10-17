// The text service's own windows: the candidate list, the focus bubble (the character under the
// cursor with its reading and, if the AI corrected or doubts it, why), and a short flash of the
// mode (中 / 英 / 全 / 半). Popups that never take the focus, drawn with GDI.
#pragma once

#include "common.h"

#include <functional>
#include <string>
#include <vector>

namespace zl::tsf {

class PopupWindow {
public:
    virtual ~PopupWindow();
    void hide();
    bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

protected:
    /// Size of the content at `scale` (DPI / 96).
    virtual SIZE measure(HDC dc, float scale) = 0;
    virtual void paint(HDC dc, const RECT& rc, float scale) = 0;
    virtual void click(POINT) {}
    virtual void timer() {}
    /// Shows the window under `anchor` (screen coordinates of the text it belongs to), or above
    /// it when there is no room below.
    void showNear(const RECT& anchor);
    void redraw();
    float scaleFor(const RECT& anchor) const;

    HWND hwnd_ = nullptr;
    float scale_ = 1.0f;

private:
    bool ensure();
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
};

class CandidateWindow : public PopupWindow {
public:
    void show(const std::vector<std::wstring>& items, int selected, int pageSize, const RECT& anchor);
    std::function<void(int index)> onSelect;

protected:
    SIZE measure(HDC dc, float scale) override;
    void paint(HDC dc, const RECT& rc, float scale) override;
    void click(POINT pt) override;

private:
    std::vector<std::wstring> items_;
    int selected_ = 0;
    int pageSize_ = 9;
    int rowHeight_ = 0;
    int pad_ = 0;
};

class BubbleWindow : public PopupWindow {
public:
    /// The focus bubble. dot: 0 none, 1 corrected (orange), 2 suspicious (red).
    void show(const std::wstring& text, const std::wstring& reading, const std::wstring& note, int dot, const RECT& anchor);
    /// A big glyph with a note that hides itself after a moment.
    void flash(const std::wstring& glyph, const std::wstring& note, const RECT& anchor);
    bool flashing() const { return flash_ && visible(); }

protected:
    SIZE measure(HDC dc, float scale) override;
    void paint(HDC dc, const RECT& rc, float scale) override;
    void timer() override;

private:
    std::wstring text_, reading_, note_;
    int dot_ = 0;
    bool flash_ = false;
};

/// The mode icon for the taskbar (a white or black glyph, following the taskbar's theme).
HICON makeModeIcon(const wchar_t* glyph);

/// Taskbar / app theme from the user's settings.
bool systemUsesLightTheme();
bool appsUseLightTheme();

}  // namespace zl::tsf
