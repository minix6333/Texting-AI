#include "ui.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace zl::tsf {

namespace {
const wchar_t* kClass = kPopupClass;
const wchar_t* kFace = L"Microsoft JhengHei UI";

struct Theme {
    COLORREF bg, border, text, secondary, selBg, selText;
};

DWORD readDword(const wchar_t* name, DWORD def) {
    DWORD value = def, size = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", name, RRF_RT_REG_DWORD,
                     nullptr, &value, &size) != ERROR_SUCCESS) {
        return def;
    }
    return value;
}

Theme theme() {
    if (appsUseLightTheme()) return {RGB(255, 255, 255), RGB(200, 200, 200), RGB(26, 26, 26), RGB(110, 110, 110), RGB(0, 103, 192), RGB(255, 255, 255)};
    return {RGB(43, 43, 43), RGB(70, 70, 70), RGB(240, 240, 240), RGB(160, 160, 160), RGB(76, 160, 255), RGB(0, 0, 0)};
}

const COLORREF kOrange = RGB(240, 140, 0);
const COLORREF kRed = RGB(224, 60, 60);

/// A font in pixels (already scaled).
struct Font {
    HFONT h;
    Font(int px, int weight = FW_NORMAL)
        : h(CreateFontW(-std::max(px, 6), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, kFace)) {}
    ~Font() { DeleteObject(h); }
};

SIZE textSize(HDC dc, HFONT f, const std::wstring& s) {
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, s.c_str(), int(s.size()), &sz);
    SelectObject(dc, old);
    return sz;
}

void drawText(HDC dc, HFONT f, COLORREF color, const std::wstring& s, RECT rc, UINT format) {
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), int(s.size()), &rc, format | DT_NOPREFIX | DT_SINGLELINE);
    SelectObject(dc, old);
}

void fillRound(HDC dc, const RECT& rc, COLORREF color, int radius) {
    HBRUSH b = CreateSolidBrush(color);
    HPEN p = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

void frame(HDC dc, const RECT& rc, const Theme& t) {
    HBRUSH bg = CreateSolidBrush(t.bg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    HBRUSH border = CreateSolidBrush(t.border);
    FrameRect(dc, &rc, border);
    DeleteObject(border);
}

void dot(HDC dc, int cx, int cy, int r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    HPEN p = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

UINT windowDpi(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn && hwnd) {
        UINT dpi = fn(hwnd);
        if (dpi) return dpi;
    }
    HDC dc = GetDC(nullptr);
    UINT dpi = UINT(GetDeviceCaps(dc, LOGPIXELSY));
    ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}
}  // namespace

bool systemUsesLightTheme() { return readDword(L"SystemUsesLightTheme", 0) != 0; }
bool appsUseLightTheme() { return readDword(L"AppsUseLightTheme", 1) != 0; }

// MARK: PopupWindow

PopupWindow::~PopupWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
}

bool PopupWindow::ensure() {
    if (hwnd_) return true;
    static std::once_flag once;
    std::call_once(once, [] {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.style = CS_DROPSHADOW;
        wc.lpfnWndProc = &PopupWindow::proc;
        wc.hInstance = g_module;
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
    });
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                            g_module, this);
    return hwnd_ != nullptr;
}

void PopupWindow::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void PopupWindow::redraw() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

float PopupWindow::scaleFor(const RECT&) const { return float(windowDpi(hwnd_)) / 96.0f; }

void PopupWindow::showNear(const RECT& anchor) {
    if (!ensure()) return;
    // move first: a per-monitor aware app gives the window the target monitor's DPI
    SetWindowPos(hwnd_, HWND_TOPMOST, anchor.left, anchor.bottom, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
    scale_ = scaleFor(anchor);
    HDC dc = GetDC(hwnd_);
    const SIZE sz = measure(dc, scale_);
    ReleaseDC(hwnd_, dc);
    MONITORINFO mi = {};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT work = mi.rcWork;
    const int gap = int(4 * scale_);
    int x = anchor.left, y = anchor.bottom + gap;
    if (y + sz.cy > work.bottom) y = anchor.top - gap - sz.cy;
    x = std::max<int>(work.left, std::min<int>(x, work.right - sz.cx));
    y = std::max<int>(work.top, std::min<int>(y, work.bottom - sz.cy));
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, sz.cx, sz.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK PopupWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<PopupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            break;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONUP:
            if (self) self->click(POINT{short(LOWORD(lp)), short(HIWORD(lp))});
            return 0;
        case WM_TIMER:
            if (self) self->timer();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            if (self) self->paint(mem, rc, self->scale_);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            if (self) self->hwnd_ = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// MARK: CandidateWindow

void CandidateWindow::show(const std::vector<std::wstring>& items, int selected, int pageSize, const RECT& anchor) {
    items_ = items;
    pageSize_ = std::max(1, pageSize);
    selected_ = std::max(0, std::min(selected, int(items_.size()) - 1));
    if (items_.empty()) {
        hide();
        return;
    }
    showNear(anchor);
}

SIZE CandidateWindow::measure(HDC dc, float s) {
    Font item(int(20 * s)), label(int(14 * s));
    pad_ = int(5 * s);
    rowHeight_ = int(30 * s);
    const int first = selected_ / pageSize_ * pageSize_;
    const int last = std::min(int(items_.size()), first + pageSize_);
    int width = int(120 * s);
    for (int i = first; i < last; ++i)
        width = std::max(width, int(40 * s) + int(textSize(dc, item.h, items_[size_t(i)]).cx) + int(16 * s));
    const bool pages = int(items_.size()) > pageSize_;
    const int height = pad_ * 2 + (last - first) * rowHeight_ + (pages ? int(22 * s) : 0);
    (void)label;
    return SIZE{width + pad_ * 2, height};
}

void CandidateWindow::paint(HDC dc, const RECT& rc, float s) {
    const Theme t = theme();
    frame(dc, rc, t);
    Font item(int(20 * s)), label(int(14 * s));
    const int first = selected_ / pageSize_ * pageSize_;
    const int last = std::min(int(items_.size()), first + pageSize_);
    for (int i = first; i < last; ++i) {
        RECT row = {rc.left + pad_, rc.top + pad_ + (i - first) * rowHeight_, rc.right - pad_, rc.top + pad_ + (i - first + 1) * rowHeight_};
        const bool sel = i == selected_;
        if (sel) fillRound(dc, row, t.selBg, int(6 * s));
        RECT num = row;
        num.left += int(8 * s);
        num.right = num.left + int(20 * s);
        drawText(dc, label.h, sel ? t.selText : t.secondary, std::to_wstring(i - first + 1), num, DT_LEFT | DT_VCENTER);
        RECT txt = row;
        txt.left = num.right + int(6 * s);
        drawText(dc, item.h, sel ? t.selText : t.text, items_[size_t(i)], txt, DT_LEFT | DT_VCENTER);
    }
    if (int(items_.size()) > pageSize_) {
        const int pages = (int(items_.size()) + pageSize_ - 1) / pageSize_;
        RECT foot = {rc.left + pad_, rc.bottom - pad_ - int(22 * s), rc.right - pad_ - int(4 * s), rc.bottom - pad_};
        drawText(dc, label.h, t.secondary, std::to_wstring(first / pageSize_ + 1) + L" / " + std::to_wstring(pages), foot,
                 DT_RIGHT | DT_VCENTER);
    }
}

void CandidateWindow::click(POINT pt) {
    if (rowHeight_ <= 0) return;
    const int row = (pt.y - pad_) / rowHeight_;
    const int first = selected_ / pageSize_ * pageSize_;
    const int index = first + row;
    if (row >= 0 && row < pageSize_ && index < int(items_.size()) && onSelect) onSelect(index);
}

// MARK: BubbleWindow

void BubbleWindow::show(const std::wstring& text, const std::wstring& reading, const std::wstring& note, int d, const RECT& anchor) {
    if (hwnd_) KillTimer(hwnd_, 1);
    flash_ = false;
    text_ = text;
    reading_ = reading;
    note_ = note;
    dot_ = d;
    showNear(anchor);
}

void BubbleWindow::flash(const std::wstring& glyph, const std::wstring& note, const RECT& anchor) {
    flash_ = true;
    text_ = glyph;
    reading_.clear();
    note_ = note;
    dot_ = 0;
    showNear(anchor);
    if (hwnd_) SetTimer(hwnd_, 1, 900, nullptr);
}

void BubbleWindow::timer() {
    if (hwnd_) KillTimer(hwnd_, 1);
    flash_ = false;
    hide();
}

SIZE BubbleWindow::measure(HDC dc, float s) {
    const int pad = int(8 * s);
    if (flash_) {
        Font big(int(26 * s), FW_SEMIBOLD), small(int(13 * s));
        const SIZE g = textSize(dc, big.h, text_), n = textSize(dc, small.h, note_);
        return SIZE{pad * 3 + g.cx + n.cx, pad * 2 + std::max(g.cy, n.cy)};
    }
    Font big(int(22 * s)), small(int(13 * s));
    const SIZE g = textSize(dc, big.h, text_), r = textSize(dc, small.h, reading_), n = textSize(dc, small.h, note_);
    const int line1 = (dot_ ? int(14 * s) : 0) + g.cx + int(8 * s) + r.cx;
    const int width = std::max(line1, note_.empty() ? 0 : int(n.cx));
    return SIZE{pad * 2 + width, pad * 2 + g.cy + (note_.empty() ? 0 : int(4 * s) + n.cy)};
}

void BubbleWindow::paint(HDC dc, const RECT& rc, float s) {
    const Theme t = theme();
    frame(dc, rc, t);
    const int pad = int(8 * s);
    if (flash_) {
        Font big(int(26 * s), FW_SEMIBOLD), small(int(13 * s));
        const SIZE g = textSize(dc, big.h, text_);
        RECT r1 = {rc.left + pad, rc.top, rc.left + pad + g.cx, rc.bottom};
        drawText(dc, big.h, t.text, text_, r1, DT_LEFT | DT_VCENTER);
        RECT r2 = {r1.right + pad, rc.top, rc.right - pad, rc.bottom};
        drawText(dc, small.h, t.secondary, note_, r2, DT_LEFT | DT_VCENTER);
        return;
    }
    Font big(int(22 * s)), small(int(13 * s));
    const SIZE g = textSize(dc, big.h, text_);
    int x = rc.left + pad;
    const int y = rc.top + pad;
    if (dot_) {
        dot(dc, x + int(4 * s), y + g.cy / 2, int(4 * s), dot_ == 1 ? kOrange : kRed);
        x += int(14 * s);
    }
    RECT r1 = {x, y, x + g.cx, y + g.cy};
    drawText(dc, big.h, t.text, text_, r1, DT_LEFT | DT_VCENTER);
    RECT r2 = {r1.right + int(8 * s), y, rc.right - pad, y + g.cy};
    drawText(dc, small.h, t.secondary, reading_, r2, DT_LEFT | DT_VCENTER);
    if (!note_.empty()) {
        RECT r3 = {rc.left + pad, y + g.cy + int(4 * s), rc.right - pad, rc.bottom - pad};
        drawText(dc, small.h, dot_ == 1 ? kOrange : dot_ == 2 ? kRed : t.secondary, note_, r3, DT_LEFT | DT_VCENTER);
    }
}

// MARK: icon

HICON makeModeIcon(const wchar_t* glyph) {
    const int size = std::max(16, GetSystemMetrics(SM_CXSMICON));
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) return nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBmp = SelectObject(dc, color);
    std::fill_n(static_cast<uint32_t*>(bits), size_t(size) * size_t(size), 0u);
    HFONT font = CreateFontW(-int(size * 0.9), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, kFace);
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rc = {0, 0, size, size};
    DrawTextW(dc, glyph, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    // white-on-black coverage → the glyph in the taskbar's text color, with alpha
    const bool light = systemUsesLightTheme();
    const uint32_t fg = light ? 0x000000u : 0xFFFFFFu;
    auto* px = static_cast<uint32_t*>(bits);
    for (int i = 0; i < size * size; ++i) {
        const uint32_t v = px[i];
        const uint32_t a = std::max({v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF});
        const uint32_t r = ((fg >> 16) & 0xFF) * a / 255, g = ((fg >> 8) & 0xFF) * a / 255, b = (fg & 0xFF) * a / 255;
        px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBmp);
    DeleteDC(dc);
    std::vector<BYTE> zeros(size_t((size + 15) / 16 * 2) * size_t(size), 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, zeros.data());
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

}  // namespace zl::tsf
