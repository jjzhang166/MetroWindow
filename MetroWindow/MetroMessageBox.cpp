#include "stdafx.h"
#include "MetroMessageBox.h"
#include <vector>

#if defined(_SYSINFOAPI_H_) && defined(NOT_BUILD_WINDOWS_DEPRECATE) && (_WIN32_WINNT >= 0x0501)
#include <VersionHelpers.h>
#endif

namespace MetroWindow
{

class DialogTemplateT
{
public:
    LPCDLGTEMPLATE Template() {
        return (LPCDLGTEMPLATE)&v[0];
    }

    void AlignToDword() {
        if (v.size() % 4) {
            Write(NULL, 4 - (v.size() % 4));
        }
    }

    void Write(LPCVOID pvWrite, DWORD cbWrite) {
        v.insert(v.end(), cbWrite, 0);
        if (pvWrite) {
            CopyMemory(&v[v.size() - cbWrite], pvWrite, cbWrite);
        }
    }

    template<typename T> void Write(T t) {
        Write(&t, sizeof(T));
    }

    void WriteString(LPCWSTR psz) {
        Write(psz, (lstrlenW(psz) + 1) * sizeof(WCHAR));
    }

private:
    std::vector<BYTE> v;
};

// Get the size of a struct up to and including the specified member.
// This is necessary to set compatible struct sizes for different versions
// of certain Windows APIs (e.g. SystemParametersInfo).
#define SIZEOF_STRUCT_WITH_SPECIFIED_LAST_MEMBER(struct_name, member) \
    offsetof(struct_name, member) + \
    (sizeof static_cast<struct_name*>(NULL)->member)

#define NONCLIENTMETRICS_SIZE_PRE_VISTA \
    SIZEOF_STRUCT_WITH_SPECIFIED_LAST_MEMBER(NONCLIENTMETRICS, lfMessageFont)

bool IsWindowsVistaOrLater()
{
#ifdef _versionhelpers_H_INCLUDED_
    return ::IsWindowsVistaOrGreater();
#else // !_versionhelpers_H_INCLUDED_
    OSVERSIONINFO version_info = { sizeof version_info };
    BOOL bRet = ::GetVersionEx(&version_info);
    return ((bRet != FALSE) && (version_info.dwMajorVersion >= 6));
#endif
}

bool GetNonClientMetrics(NONCLIENTMETRICS* metrics)
{

    static const UINT SIZEOF_NONCLIENTMETRICS =
        IsWindowsVistaOrLater() ?
        sizeof(NONCLIENTMETRICS) : NONCLIENTMETRICS_SIZE_PRE_VISTA;
    metrics->cbSize = SIZEOF_NONCLIENTMETRICS;
    return !!SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
                                  SIZEOF_NONCLIENTMETRICS, metrics,
                                  0);
}

int GetDluBaseX(HDC hdc)
{
    // This is how Microsoft recommends calculating the dialog unit
    // conversions. See: http://support.microsoft.com/kb/125681
    SIZE size;
    GetTextExtentPoint32(hdc, L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &size);
    int dlux = (size.cx/26 + 1) / 2;
    return dlux;
}

int GetDluBaseY(HDC hdc)
{
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    int dluy = std::max<int>(1, tm.tmHeight);
    return dluy;
}

enum {
    ButtonWidth             = 50,
    ButtonHeight            = 14,
    ButtonSpacing           = 6,
    BottomMargin            = 12,
    MinimalHeight           = 70,
    SpacingWidth            = 8,
    SpacingHeight           = 5,
};

#define ICON_CONTROL_ID 1000
#define MESSAGE_CONTROL_ID 1001

#define DIALOG_ITEM_BUTTON 0x0080FFFF
#define DIALOG_ITEM_STATIC 0x0082FFFF

struct ButtonInfo {
    UINT Id;
    WCHAR *Text;
};

//static const ButtonInfo kMessageBoxButtons[] =
//{
//    0,                  L"",
//    IDOK,               L"OK",
//    IDCANCEL,           L"Cancel",
//    IDABORT,            L"&Abort",
//    IDRETRY,            L"&Retry",
//    IDIGNORE,           L"&Ignore",
//    IDYES,              L"&Yes",
//    IDNO,               L"&No",
//    IDCLOSE,            L"Close",
//    IDHELP,             L"&Help",
//    IDTRYAGAIN,         L"&Try Again",
//    IDCONTINUE,         L"&Continue",
//};

static const ButtonInfo kMessageBoxButtons[] = {
    0,                  L"",
    IDOK,               L"确定",
    IDCANCEL,           L"取消",
    IDABORT,            L"终止(&A)",
    IDRETRY,            L"重试(&R)",
    IDIGNORE,           L"忽略(&I)",
    IDYES,              L"是(&Y)",
    IDNO,               L"否(&N)",
    IDCLOSE,            L"关闭",
    IDHELP,             L"帮助",
    IDTRYAGAIN,         L"重试(&T)",
    IDCONTINUE,         L"继续(&C)",
};

const WCHAR * GetMessageBoxButtonText(UINT buttonId)
{
    // This is the safe version.
    //for (int i = 0; i < arraysize(kMessageBoxButtons); ++i)
    //{
    //    if (buttonId == kMessageBoxButtons[i].Id)
    //        return kMessageBoxButtons[i].Text;
    //}

    //return kMessageBoxButtons[0].Text;

    if (buttonId > 0 && buttonId < arraysize(kMessageBoxButtons)) {
        return kMessageBoxButtons[buttonId].Text;
    } else {
        return kMessageBoxButtons[0].Text;
    }
}

LONG CALLBACK IconProc(HWND hwnd, UINT message, WPARAM, LPARAM)
{
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc;
        hdc = BeginPaint(hwnd, &ps);
        ::DrawIcon(hdc, 0, 0, (HICON)(::GetWindowLongPtr(hwnd, GWLP_USERDATA)));
        EndPaint(hwnd, &ps);
    }

    return FALSE;
}

CMetroMessageBox::CMetroMessageBox(HINSTANCE hInstance)
    : CMetroFrame(hInstance), message_box_icon_(NULL), font_(NULL),
      right_justify_buttons_(false), disable_close_(false), button_width_(0), button_height_(0),
      button_count_(0), default_button_(1), default_button_id_(0), base_unit_x_(1), base_unit_y_(1)
{
    for (int i = 0; i < 5; ++i) {
        DialogItemTemplate item = { 0 };
        dialog_items_[i] = item;
    }
    
    ClientAreaMovable(true);
}


CMetroMessageBox::~CMetroMessageBox(void)
{
    if (message_box_icon_)
        DestroyIcon(message_box_icon_);
    message_box_icon_ = 0;

    if (font_)
        ::DeleteObject(font_);
}

INT_PTR CMetroMessageBox::Show(HWND hWndParent, LPCTSTR lpszMessage, LPCTSTR lpszCaption, UINT uType)
{
    if (lpszMessage == NULL) lpszMessage = L"";
    if (lpszCaption == NULL) lpszCaption = L"Error";

    button_count_ = 0;
    disable_close_ = false;

    // set default button
    switch (uType & MB_DEFMASK) {
    case MB_DEFBUTTON1 :
        default_button_ = 1;
        break;
    case MB_DEFBUTTON2 :
        default_button_ = 2;
        break;
    case MB_DEFBUTTON3 :
        default_button_ = 3;
        break;
    case MB_DEFBUTTON4 :
        default_button_ = 4;
        break;
    default:
        default_button_ = 1;
        break;
    }

    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    if (!GetNonClientMetrics(&ncm))
        return IDCANCEL;

    ::MessageBeep(uType & MB_ICONMASK);

    int nMaxWidth = (::GetSystemMetrics(SM_CXSCREEN) / 2) + 80;
    if (uType & MB_ICONMASK)
        nMaxWidth -= GetSystemMetrics(SM_CXICON) + 2 * SpacingWidth;

    RECT msgRect;
    SetRect(&msgRect, 0, 0, nMaxWidth, nMaxWidth);

    {
        HDC hdc = ::CreateDC(L"DISPLAY", NULL, NULL, NULL);

        font_ = ::CreateFontIndirect(&ncm.lfMessageFont);
        HFONT hOldFont = (HFONT)::SelectObject(hdc, font_);

        base_unit_x_ = GetDluBaseX(hdc);
        base_unit_y_ = GetDluBaseY(hdc);

        // translate dialog units to pixels
        button_width_  = MulDiv(ButtonWidth, base_unit_x_, 4);
        button_height_ = MulDiv(ButtonHeight, base_unit_y_, 8);

        // Get bounds of message text
        ::DrawText(hdc, lpszMessage, -1, &msgRect,
                   DT_LEFT | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT | DT_EXPANDTABS);

        // See text for discussion of fancy formula.
        if (ncm.lfMessageFont.lfHeight < 0) {
            ncm.lfMessageFont.lfHeight = -MulDiv(ncm.lfMessageFont.lfHeight,
                                                 72, GetDeviceCaps(hdc, LOGPIXELSY));
        }

        ::SelectObject(hdc, hOldFont);
        ::DeleteDC(hdc);
    }

    msgRect.left    = SpacingWidth;
    msgRect.top     = SpacingWidth;
    msgRect.right  += 2 * SpacingWidth;
    msgRect.bottom += 2 * SpacingWidth;
    if (uType & MB_ICONMASK) {
        msgRect.left  += SpacingWidth;
        msgRect.right += SpacingWidth;
    }

    int width = msgRect.right - msgRect.left;
    int height = msgRect.bottom - msgRect.top;

    // client rect
    RECT mbrect;
    SetRect(&mbrect, 0, 0, width + (2 * SpacingWidth), height + (2 * SpacingWidth));
    if ((mbrect.bottom - mbrect.top) < MinimalHeight)
        mbrect.bottom = MinimalHeight;

    dialog_templ_.x = 0;
    dialog_templ_.y = 0;
    dialog_templ_.cdit = 0;

    dialog_templ_.style = WS_CAPTION | WS_SYSMENU | DS_SETFONT | DS_MODALFRAME | DS_CENTER;
    if (uType & MB_SYSTEMMODAL) {
        dialog_templ_.style |= DS_SYSMODAL;
    }

    dialog_templ_.dwExtendedStyle = 0;

    RECT iconrect = { 0 };
    if (uType & MB_ICONMASK) {
        LPTSTR lpIcon = NULL;

        switch (uType & MB_ICONMASK) {
        case MB_ICONEXCLAMATION:
            lpIcon = (LPTSTR)IDI_EXCLAMATION;
            break;
        case MB_ICONHAND:
            lpIcon = (LPTSTR)IDI_HAND;
            break;
        case MB_ICONQUESTION:
            lpIcon = (LPTSTR)IDI_QUESTION;
            break;
        case MB_ICONASTERISK:
            lpIcon = (LPTSTR)IDI_ASTERISK;
            break;
        default:
            lpIcon = (LPTSTR)IDI_EXCLAMATION;
            break;
        }

        if (lpIcon)
            message_box_icon_ = ::LoadIcon(NULL, lpIcon);
    }

    if (message_box_icon_) {
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);

        int icon_x = SpacingWidth;
        int icon_y = SpacingWidth;

        msgRect.left  += cxIcon + icon_x;
        msgRect.right += cxIcon + icon_x;

        mbrect.right = msgRect.right + SpacingWidth;

        SetRect(&iconrect, icon_x, icon_y, icon_x + cxIcon + 2, icon_y + cyIcon + 2);
        AddItem(DIALOG_ITEM_STATIC, ICON_CONTROL_ID, &iconrect);
    }

    // add message text
    AddItem(DIALOG_ITEM_STATIC, MESSAGE_CONTROL_ID, &msgRect);

    int cItems = 0;
    int nWidthStdButtons = 0;

    switch (uType & MB_TYPEMASK) {
    case MB_OK:
        cItems = 1;
        break;
    case MB_OKCANCEL:
    case MB_RETRYCANCEL:
    case MB_YESNO:
        cItems = 2;
        break;
    case MB_YESNOCANCEL:
    case MB_ABORTRETRYIGNORE:
    case MB_CANCELTRYCONTINUE:
        cItems = 3;
        break;
    }

    int x = 0;
    int y = 0;

    y = (msgRect.bottom > iconrect.bottom) ? msgRect.bottom : iconrect.bottom;
    y += SpacingHeight;

    if (message_box_icon_)
        y += 9;

    int nTotalButtonWidth = button_width_ * cItems + (ButtonSpacing * (cItems-1));

    RECT buttonRow;
    SetRect(&buttonRow, 0, y, nTotalButtonWidth, y + button_height_);

    mbrect.bottom = buttonRow.bottom + BottomMargin;

    int bw = buttonRow.right - buttonRow.left;
    int bleft = 2 * SpacingWidth;
    int bright = bleft + bw;

    if (mbrect.right <= (bright + (2 * SpacingWidth)))
        mbrect.right = bright + (2 * SpacingWidth);

    x = ((mbrect.right - mbrect.left) - bw) / 2;
    y = buttonRow.top;

    if (right_justify_buttons_) {
        x = mbrect.right - nTotalButtonWidth - 2 * SpacingWidth;
    }

    switch (uType & MB_TYPEMASK) {
    case MB_OK:
        x += AddButton(IDOK, x, y);
        break;

    case MB_OKCANCEL:
        x += AddButton(IDOK, x, y);
        x += AddButton(IDCANCEL, x, y);
        break;

    case MB_RETRYCANCEL:
        x += AddButton(IDRETRY, x, y);
        x += AddButton(IDCANCEL, x, y);
        break;

    case MB_YESNO:
        x += AddButton(IDYES, x, y);
        x += AddButton(IDNO, x, y);
        disable_close_ = true;
        break;

    case MB_YESNOCANCEL:
        x += AddButton(IDYES, x, y);
        x += AddButton(IDNO, x, y);
        x += AddButton(IDCANCEL, x, y);
        break;

    case MB_ABORTRETRYIGNORE:
        x += AddButton(IDABORT, x, y);
        x += AddButton(IDRETRY, x, y);
        x += AddButton(IDIGNORE, x, y);
        disable_close_ = true;
        break;

    case MB_CANCELTRYCONTINUE:
        x += AddButton(IDCANCEL, x, y);
        x += AddButton(IDTRYAGAIN, x, y);
        x += AddButton(IDCONTINUE, x, y);
        break;

    default:
        x += AddButton(IDOK, x, y);
        break;
    }

    if (buttonRow.bottom >= mbrect.bottom)
        mbrect.bottom = buttonRow.bottom + (2 * SpacingWidth);

    if (mbrect.right < (buttonRow.right + (2 * SpacingWidth)))
        mbrect.right = buttonRow.right + (2 * SpacingWidth);

    dialog_templ_.x = 0;
    dialog_templ_.y = 0;
    dialog_templ_.cx = (short)PixelToDluX(mbrect.right - mbrect.left);
    dialog_templ_.cy = (short)PixelToDluY(mbrect.bottom - mbrect.top);

    DialogTemplateT tmp;
    tmp.Write<WORD>(1); // dialog version
    tmp.Write<WORD>(0xFFFF); // extended dialog template
    tmp.Write<DWORD>(0); // help ID
    tmp.Write<DWORD>(dialog_templ_.dwExtendedStyle); // extended style
    tmp.Write<DWORD>(dialog_templ_.style);
    tmp.Write<WORD>(dialog_templ_.cdit); // number of controls
    tmp.Write<WORD>(dialog_templ_.x); // X
    tmp.Write<WORD>(dialog_templ_.y); // Y
    tmp.Write<WORD>(dialog_templ_.cx); // width
    tmp.Write<WORD>(dialog_templ_.cy); // height
    tmp.WriteString(L""); // no menu
    tmp.WriteString(L""); // default dialog class
    tmp.WriteString(lpszCaption); // title

    // Next comes the font description.
    tmp.Write<WORD>((WORD)ncm.lfMessageFont.lfHeight); // point
    tmp.Write<WORD>((WORD)ncm.lfMessageFont.lfWeight); // weight
    tmp.Write<BYTE>(ncm.lfMessageFont.lfItalic); // Italic
    tmp.Write<BYTE>(ncm.lfMessageFont.lfCharSet); // CharSet
    tmp.WriteString(ncm.lfMessageFont.lfFaceName);

    for (int i = 0; i < dialog_templ_.cdit; ++i) {
        DialogItemTemplate * dlgItem = &dialog_items_[i];

        tmp.AlignToDword();
        tmp.Write<DWORD>(0); // help id
        tmp.Write<DWORD>(dlgItem->dwExtendedStyle); // window extended style
        tmp.Write<DWORD>(dlgItem->style); // style
        tmp.Write<WORD>(dlgItem->x); // x
        tmp.Write<WORD>(dlgItem->y); // y
        tmp.Write<WORD>(dlgItem->cx); // width
        tmp.Write<WORD>(dlgItem->cy); // height
        tmp.Write<DWORD>(dlgItem->id); // control ID
        tmp.Write<DWORD>(dlgItem->ctype); // static

        if (dlgItem->id == MESSAGE_CONTROL_ID)
            tmp.WriteString(lpszMessage);
        else if (dlgItem->id == ICON_CONTROL_ID)
            tmp.WriteString(L"");
        else
            tmp.WriteString(GetMessageBoxButtonText(dlgItem->id));

        tmp.Write<WORD>(0); // no extra data
    }

    // Show the message dialog.
    INT_PTR result = ::DialogBoxIndirectParam(GetModuleInstance(), tmp.Template(),
                     hWndParent, &CMetroMessageBox::MsgBoxProc, (LPARAM)this);

    return result;
}

// Dialog units to pixels conversion.
// See http://support.microsoft.com/kb/145994 for details.
int CMetroMessageBox::PixelToDluX(int dluX)
{
    return dluX * 4 / base_unit_x_;
}

int CMetroMessageBox::PixelToDluY(int dluY)
{
    return dluY * 8 / base_unit_y_;
}

int CMetroMessageBox::AddButton(UINT nID, int x, int y)
{
    RECT rect;
    SetRect(&rect, x, y, x + button_width_, y + button_height_);

    AddItem(DIALOG_ITEM_BUTTON, nID, &rect);
    return button_width_ + ButtonSpacing;
}

void CMetroMessageBox::AddItem(DWORD cType, UINT nID, RECT *pRect)
{
    if (dialog_templ_.cdit < 5) {

        DialogItemTemplate * dlgItem = &dialog_items_[dialog_templ_.cdit];

        dlgItem->x = (short)PixelToDluX(pRect->left);
        dlgItem->y = (short)PixelToDluY(pRect->top);
        dlgItem->cx = (short)PixelToDluX(pRect->right - pRect->left);
        dlgItem->cy = (short)PixelToDluY(pRect->bottom - pRect->top);

        dlgItem->dwExtendedStyle = 0;
        dlgItem->id = (WORD)nID;
        dlgItem->ctype = cType;

        switch (cType) {
        case DIALOG_ITEM_BUTTON:
            button_count_++;
            dlgItem->style = WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_OWNERDRAW;
            if (button_count_ == default_button_) {
                dlgItem->style |= BS_DEFPUSHBUTTON;
                default_button_id_ = nID;
            } else {
                dlgItem->style |= BS_PUSHBUTTON;
            }
            break;

        case DIALOG_ITEM_STATIC:
            dlgItem->style = WS_CHILD | WS_VISIBLE | SS_LEFT;
            break;

        default:
            _ASSERTE(FALSE); // should never get here
        }

        dialog_templ_.cdit++;
    }
}

LRESULT CMetroMessageBox::OnDefWndProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return 0;
}

LRESULT CMetroMessageBox::OnWndProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (uMsg) {
    case WM_INITDIALOG: {
        BOOL bHandled = FALSE;
        LRESULT lRet = CMetroFrame::OnCreate(uMsg, wParam, lParam, bHandled);

        // Disable close button just like real MessageBox
        if (disable_close_) {
            ::RemoveMenu(GetSystemMenu(GetHWnd(), FALSE), SC_CLOSE, MF_BYCOMMAND);

            EnableCloseButton(false);
        }

        if (message_box_icon_) {
            HWND hwndIcon;

            hwndIcon = ::GetDlgItem(GetHWnd(), ICON_CONTROL_ID);
            if (hwndIcon && ::IsWindow(hwndIcon)) {
                ::SetWindowLongPtr(hwndIcon, GWLP_WNDPROC, (LONG_PTR) IconProc);
                ::SetWindowLongPtr(hwndIcon, GWLP_USERDATA, (LONG_PTR) message_box_icon_);
            }
        }

        HWND hwndChild = ::GetDlgItem(GetHWnd(), default_button_id_);
        if (hwndChild && ::IsWindow(hwndChild))
            ::SetFocus(hwndChild);

        return lRet;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        return (LONG_PTR)::GetStockObject(WHITE_BRUSH);
    case WM_COMMAND:
        if (wParam == IDCLOSE) {
            return TRUE;
        } else {
            EndDialog(hWnd_, wParam);
            return FALSE;
        }
    case WM_DRAWITEM:
        return DrawMetroButton((LPDRAWITEMSTRUCT) lParam);
    default:
        return CMetroFrame::OnWndProc(uMsg, wParam, lParam);
    }
}

INT_PTR CALLBACK CMetroMessageBox::MsgBoxProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CMetroMessageBox* pThis = NULL;

    if (uMsg == WM_INITDIALOG) {
        // get the pointer to the window from lpCreateParams
        ::SetWindowLongPtr(hWnd, GWLP_USERDATA, lParam);
        pThis = (CMetroMessageBox*)lParam;
        pThis->hWnd_ = hWnd;
    } else {
        pThis = (CMetroMessageBox *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (uMsg == WM_NCDESTROY && pThis != NULL) {
            LRESULT lRes = ::DefWindowProc(hWnd, uMsg, wParam, lParam);
            ::SetWindowLongPtr(pThis->hWnd_, GWLP_USERDATA, 0L);
            pThis->hWnd_ = NULL;
            return lRes;
        }
    }

    // if we have the pointer, go to the message handler of the window
    if (pThis) {
        LRESULT lRes = pThis->OnWndProc(uMsg, wParam, lParam);
        SetWindowLongPtr(hWnd, DWLP_MSGRESULT, lRes);
        return lRes;
    } else {
        return 0;
    }
}

LRESULT CMetroMessageBox::DrawMetroButton(LPDRAWITEMSTRUCT lpDIS)
{
    HDC dc = lpDIS->hDC;

    BOOL bIsPressed  = (lpDIS->itemState & ODS_SELECTED);
    BOOL bIsFocused  = (lpDIS->itemState & ODS_FOCUS);
    BOOL bIsDisabled = (lpDIS->itemState & ODS_DISABLED);

    RECT itemRect = lpDIS->rcItem;

    SetBkMode(dc, TRANSPARENT);
    if (bIsFocused) {
        HBRUSH br = CreateSolidBrush(RGB(0, 174, 219));
        FrameRect(dc, &itemRect, br);
        InflateRect(&itemRect, -1, -1);
        DeleteObject(br);
    }

    COLORREF crBroder = bIsFocused ?
        (bIsPressed ? RGB(0, 144, 189) : RGB(0, 174, 219)) : RGB(153, 153, 153);

    HBRUSH brBroder = CreateSolidBrush(crBroder);
    FrameRect(dc, &itemRect, brBroder);
    InflateRect(&itemRect, -1, -1);
    DeleteObject(brBroder);

    COLORREF crColor = bIsFocused ? RGB(235, 235, 235) : RGB(255, 255, 255);
    HBRUSH brBackground = CreateSolidBrush(crColor);
    FillRect(dc, &itemRect, brBackground);
    DeleteObject(brBackground);

    WCHAR title[32];
    int titleLen = ::GetWindowTextW(lpDIS->hwndItem, title, 32);
    if (titleLen > 0) {
        RECT captionRect = lpDIS->rcItem;

        if (bIsPressed) OffsetRect(&captionRect, 1, 1);

        RECT centerRect = captionRect;
        DrawTextW(dc, title, -1, &captionRect, DT_WORDBREAK | DT_CALCRECT | DT_CENTER);

        LONG captionRectWidth = captionRect.right - captionRect.left;
        LONG captionRectHeight = captionRect.bottom - captionRect.top;
        LONG centerRectWidth = centerRect.right - centerRect.left;
        LONG centerRectHeight = centerRect.bottom - centerRect.top;

        OffsetRect(&captionRect, (centerRectWidth - captionRectWidth)/2, 
            (centerRectHeight - captionRectHeight)/2);

        SetBkMode(dc, TRANSPARENT);

        if (bIsDisabled) {
            OffsetRect(&captionRect, 1, 1);
            SetTextColor(dc, ::GetSysColor(COLOR_3DHILIGHT));
            DrawTextW(dc, title, -1, &captionRect, DT_WORDBREAK | DT_CENTER);
            OffsetRect(&captionRect, -1, -1);
            SetTextColor(dc, ::GetSysColor(COLOR_3DSHADOW));
            DrawTextW(dc, title, -1, &captionRect, DT_WORDBREAK | DT_CENTER);
        } else {
            SetTextColor(dc, ::GetSysColor(COLOR_BTNTEXT));
            SetBkColor(dc, crColor);
            DrawTextW(dc, title, -1, &captionRect, DT_WORDBREAK | DT_CENTER);
        }
    }

    return TRUE;
}

} // namespace MetroWindow
