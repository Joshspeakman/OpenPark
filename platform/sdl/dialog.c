/* The launch window's file chooser (Music Instruments: a SoundFont or DLS file), the Mac's NSOpenPanel
 * (launcher.m:804-809): Windows' own Open dialog; on Linux zenity's, else kdialog's. Kept apart from launcher.c so
 * Windows' headers don't meet its names. */
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

int tp_pick_soundfont(SDL_Window *parent, char *out, size_t n) {
    if (n) out[0] = 0;
#ifdef _WIN32
    wchar_t file[1024] = L"";
    OPENFILENAMEW o; memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    SDL_SysWMinfo wm; SDL_VERSION(&wm.version);
    if (parent && SDL_GetWindowWMInfo(parent, &wm)) o.hwndOwner = wm.info.win.window;
    o.lpstrFilter = L"SoundFonts and DLS files (*.sf2, *.dls)\0*.sf2;*.dls\0All files (*.*)\0*.*\0";
    o.lpstrFile = file; o.nMaxFile = (DWORD)(sizeof file / sizeof *file);
    o.lpstrTitle = L"Choose a SoundFont (.sf2) or DLS file for the music";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&o)) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, file, -1, out, (int)n, NULL, NULL) > 0;
#else
    (void)parent;
    static const char *const ask[] = {
        "zenity --file-selection --title='Choose a SoundFont (.sf2) or DLS file for the music' "
        "--file-filter='SoundFonts and DLS files | *.sf2 *.SF2 *.dls *.DLS' 2>/dev/null",
        "kdialog --title 'Choose a SoundFont (.sf2) or DLS file for the music' --getopenfilename . '*.sf2 *.dls' 2>/dev/null"};
    for (int i = 0; i < 2; i++) {
        char probe[32]; snprintf(probe, sizeof probe, "command -v %s >/dev/null", i ? "kdialog" : "zenity");
        if (system(probe) != 0) continue;
        FILE *f = popen(ask[i], "r");
        if (!f) continue;
        int got = fgets(out, (int)n, f) != NULL;
        pclose(f);
        if (got) { out[strcspn(out, "\r\n")] = 0; return out[0] != 0; }
        return 0;                                   /* (the player cancelled) */
    }
    fprintf(stderr, "launcher: no file chooser (zenity or kdialog) for the SoundFont\n");
    return 0;
#endif
}
