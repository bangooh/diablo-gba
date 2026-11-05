#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <wchar.h>
#include "builder.h"
#include "config.h"
#include "embedded.h"

#define ID_MPQ 100
#define ID_OUTPUT 101
#define ID_BUILD 102
#define ID_CANCEL 103
#define ID_HELP 104
#define WM_PROGRESS (WM_APP+1)
#define WM_FINISHED (WM_APP+2)
static HWND window,mpq_edit,output_edit,mpq_button,output_button,build_button,cancel_button,status,bar;
static HANDLE worker;
static LONG cancel_requested;
static int close_when_done;
static int dpi=96;
static HINSTANCE instance;
static HFONT body_font,heading_font,small_font;
static HBRUSH background,field_background;
static wchar_t mpq_path[32768],output_path[32768];
static int scale(int x) { return MulDiv(x,dpi,96); }
static void wide(const char *s,wchar_t *out,int capacity) {
    if (!MultiByteToWideChar(CP_UTF8,0,s,-1,out,capacity)) out[0]=0;
}
static HWND control(const wchar_t *type,const wchar_t *text,DWORD style,
                    int x,int y,int w,int h,int id,HFONT font) {
    HWND c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,scale(x),scale(y),scale(w),scale(h),
        window,(HMENU)(INT_PTR)id,instance,NULL);
    SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE); return c;
}
static void update_buttons(int busy) {
    EnableWindow(mpq_button,!busy); EnableWindow(output_button,!busy);
    EnableWindow(build_button,!busy && mpq_path[0] && output_path[0]);
    EnableWindow(cancel_button,busy);
}
static void choose_mpq(void) {
    OPENFILENAMEW d={0}; d.lStructSize=sizeof(d); d.hwndOwner=window;
    d.lpstrFilter=L"Diablo archive (DIABDAT.MPQ)\0*.mpq\0All files\0*.*\0";
    d.lpstrFile=mpq_path; d.nMaxFile=32768;
    d.lpstrTitle=L"DIABDAT.MPQ: Diablo 1.0 (517,501,242 bytes) or GOG (517,501,282 bytes)";
    d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&d)) SetWindowTextW(mpq_edit,mpq_path);
    update_buttons(0);
}
static void choose_output(void) {
    if (!output_path[0]) wcscpy(output_path,L"DIABLO.gba");
    OPENFILENAMEW d={0}; d.lStructSize=sizeof(d); d.hwndOwner=window;
    d.lpstrFilter=L"Game Boy Advance ROM\0*.gba\0";
    d.lpstrFile=output_path; d.nMaxFile=32768; d.lpstrDefExt=L"gba";
    d.lpstrTitle=L"Choose a NEW ROM filename (existing files cannot be replaced)";
    d.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&d)) SetWindowTextW(output_edit,output_path);
    else {
        /* A cancelled initial dialog must not silently select a relative output. */
        GetWindowTextW(output_edit,output_path,32768);
    }
    update_buttons(0);
}
static int progress(enum Stage stage,unsigned percent,void *data) {
    unsigned *last=data;
    unsigned value=((unsigned)stage<<16)|percent;
    if (*last!=value) { *last=value; PostMessageW(window,WM_PROGRESS,(WPARAM)percent,(LPARAM)stage); }
    return InterlockedCompareExchange(&cancel_requested,0,0)==0;
}
static DWORD WINAPI run_worker(LPVOID unused) {
    (void)unused; unsigned last=~0u;
    int result=builder_run_memory(mpq_path,embedded_package,embedded_package_size,output_path,progress,&last);
    PostMessageW(window,WM_FINISHED,(WPARAM)result,0); return 0;
}
static void start_build(void) {
    if (worker || !mpq_path[0] || !output_path[0]) return;
    InterlockedExchange(&cancel_requested,0); close_when_done=0;
    update_buttons(1); SendMessageW(bar,PBM_SETPOS,0,0);
    SetWindowTextW(status,L"Checking files...");
    worker=CreateThread(NULL,0,run_worker,NULL,0,NULL);
    if (!worker) { update_buttons(0); SetWindowTextW(status,L"Could not start. Please try again."); }
}
static void cancel_build(void) {
    if (!worker) return;
    InterlockedExchange(&cancel_requested,1); EnableWindow(cancel_button,FALSE);
    SetWindowTextW(status,L"Cancelling safely...");
}
static LRESULT CALLBACK proc(HWND h,UINT message,WPARAM w,LPARAM l) {
    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_MPQ: if (!worker) choose_mpq(); break;
        case ID_OUTPUT: if (!worker) choose_output(); break;
        case ID_BUILD: start_build(); break;
        case ID_CANCEL: cancel_build(); break;
        case ID_HELP:
            MessageBoxW(h,
                L"1. Browse: select your original DIABDAT.MPQ.\r\n"
                L"2. Save as: choose a NEW .gba filename.\r\n"
                L"3. Create ROM: restore and verify V0.9.5.\r\n\r\n"
                L"No separate data file, installation or network is needed.\r\n"
                L"Supported DIABDAT.MPQ editions:\r\n"
                L"Diablo 1.0 (517,501,242 bytes)\r\n"
                L"GOG (517,501,282 bytes)\r\n"
                L"equal file size does not guarantee compatibility.\r\n\r\n"
                L"This EXE contains an encrypted ROM, including game assets.\r\n"
                L"It restores that ROM; it does not re-encode assets from MPQ.\r\n"
                L"Native core verified on Linux. Windows runtime testing pending.\r\n\r\n"
                L"사용법: 원본 MPQ 선택 → 새 .gba 저장 위치 선택 → Create ROM.\r\n"
                L"별도 데이터 파일은 필요 없습니다. 기존 파일은 덮어쓰지 않습니다.\r\n"
                L"지원: Diablo 1.0 (517,501,242바이트), GOG (517,501,282바이트).\r\n"
                L"두 판본 모두 같은 0.9.5 롬을 생성합니다.\r\n\r\n"
                L"Flash cartridge: set Save Type to FLASH 128KB (1Mbit).\r\n"
                L"플래시 카트리지: 저장 타입을 FLASH 128KB(1Mbit)로 지정하세요.\r\n"
                L"EZ-Flash IV: use the separate EZ4 converter and run from NOR.\r\n"
                L"EZ-Flash IV는 별도 EZ4 컨버터를 사용하고 NOR에서 실행하세요.",
                L"Help / 사용법 - 0.9.5 Standard",MB_OK|MB_ICONINFORMATION);
            break;
        }
        return 0;
    case WM_PROGRESS: {
        wchar_t stage[128],line[192]; wide(builder_stage((enum Stage)l),stage,128);
        swprintf(line,192,L"%ls   %u%%",stage,(unsigned)w);
        if (!InterlockedCompareExchange(&cancel_requested,0,0)) SetWindowTextW(status,line);
        SendMessageW(bar,PBM_SETPOS,w,0); return 0;
    }
    case WM_FINISHED: {
        if (worker) { CloseHandle(worker); worker=NULL; }
        update_buttons(0);
        wchar_t text[512]; wide(builder_message((int)w),text,512); SetWindowTextW(status,text);
        if (close_when_done) { DestroyWindow(h); return 0; }
        if (w==RESULT_OK) wcscat(text,L"\r\n\r\nFlash cartridge: set Save Type to FLASH 128KB (1Mbit).\r\n플래시 카트리지: 저장 타입을 FLASH 128KB(1Mbit)로 지정하세요.");
        if (w!=RESULT_CANCELLED) MessageBoxW(h,text,L"Diablo GBA ROM Builder",
            MB_OK|(w?MB_ICONERROR:MB_ICONINFORMATION));
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)w;
        if ((HWND)l==mpq_edit || (HWND)l==output_edit) {
            SetTextColor(dc,RGB(245,242,235)); SetBkColor(dc,RGB(39,36,36)); return (LRESULT)field_background;
        }
        SetTextColor(dc,RGB(229,221,205)); SetBkColor(dc,RGB(20,18,19)); return (LRESULT)background;
    }
    case WM_CLOSE:
        if (worker) { close_when_done=1; cancel_build(); } else DestroyWindow(h);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,message,w,l);
}
int WINAPI wWinMain(HINSTANCE current,HINSTANCE previous,LPWSTR command,int show) {
    (void)previous; (void)command; instance=current;
    SetProcessDPIAware(); HDC dc=GetDC(NULL); if (dc) { dpi=GetDeviceCaps(dc,LOGPIXELSX); ReleaseDC(NULL,dc); }
    INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_PROGRESS_CLASS}; InitCommonControlsEx(&icc);
    background=CreateSolidBrush(RGB(20,18,19)); field_background=CreateSolidBrush(RGB(39,36,36));
    body_font=CreateFontW(-scale(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    heading_font=CreateFontW(-scale(25),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    small_font=CreateFontW(-scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    WNDCLASSW wc={0}; wc.lpfnWndProc=proc; wc.hInstance=instance;
    wc.lpszClassName=L"DiabloGbaBuilder095"; wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
    wc.hbrBackground=background; if (!RegisterClassW(&wc)) return 1;
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    RECT rect={0,0,scale(740),scale(595)}; AdjustWindowRect(&rect,style,FALSE);
    window=CreateWindowExW(0,wc.lpszClassName,L"Diablo GBA ROM Builder - 0.9.5 Standard",style,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,NULL,NULL,instance,NULL);
    if (!window) return 1;
    control(L"STATIC",L"DIABLO GBA ROM BUILDER",0,28,22,680,36,0,heading_font);
    control(L"STATIC",L"V0.9.5     |     Windows x64     |     Offline",0,30,62,680,22,0,body_font);
    control(L"STATIC",L"Supported DIABDAT.MPQ: Diablo 1.0 (517,501,242 bytes)\r\nor GOG (517,501,282 bytes). Both create the same Diablo GBA 0.9.5 ROM.",0,30,99,680,48,0,body_font);
    control(L"STATIC",L"1   ORIGINAL GAME FILE",0,30,163,660,20,0,small_font);
    mpq_edit=control(L"EDIT",L"",ES_READONLY|ES_AUTOHSCROLL|WS_BORDER,30,189,570,29,0,body_font);
    mpq_button=control(L"BUTTON",L"Browse...",WS_TABSTOP,612,188,98,31,ID_MPQ,body_font);
    control(L"STATIC",L"2   SAVE A NEW GBA ROM",0,30,237,660,20,0,small_font);
    output_edit=control(L"EDIT",L"",ES_READONLY|ES_AUTOHSCROLL|WS_BORDER,30,263,570,29,0,body_font);
    output_button=control(L"BUTTON",L"Save as...",WS_TABSTOP,612,262,98,31,ID_OUTPUT,body_font);
    build_button=control(L"BUTTON",L"Create ROM",BS_DEFPUSHBUTTON|WS_TABSTOP,30,319,194,38,ID_BUILD,body_font);
    cancel_button=control(L"BUTTON",L"Cancel",WS_TABSTOP,236,319,110,38,ID_CANCEL,body_font);
    control(L"BUTTON",L"Help / 사용법",WS_TABSTOP,546,319,164,38,ID_HELP,body_font);
    bar=control(PROGRESS_CLASSW,L"",PBS_SMOOTH,30,380,680,12,0,body_font);
    SendMessageW(bar,PBM_SETRANGE32,0,100); SendMessageW(bar,PBM_SETBARCOLOR,0,RGB(194,153,83));
    SendMessageW(bar,PBM_SETBKCOLOR,0,RGB(50,45,44));
    status=control(L"STATIC",L"Select your MPQ and a new output filename to begin.",0,30,405,680,48,0,body_font);
    control(L"STATIC",L"Flash cartridge: set Save Type to FLASH 128KB (1Mbit).\r\n플래시 카트리지: 저장 타입을 FLASH 128KB(1Mbit)로 지정하세요.",0,30,457,680,42,0,body_font);
    control(L"STATIC",L"EZ-Flash IV: use the separate EZ4 converter; run DIABLO.gba from NOR.",0,30,505,680,24,0,body_font);
    control(L"STATIC",L"UNOFFICIAL FAN PROJECT - Not affiliated with Nintendo or Blizzard.\r\nMPQ stays on your computer. Existing files are never replaced.",0,30,545,680,36,0,small_font);
    update_buttons(0); ShowWindow(window,show); UpdateWindow(window);
    MSG msg; int ret;
    while ((ret=GetMessageW(&msg,NULL,0,0))>0) {
        if (!IsDialogMessageW(window,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    DeleteObject(body_font); DeleteObject(heading_font); DeleteObject(small_font);
    DeleteObject(field_background); DeleteObject(background);
    return ret<0?1:0;
}
