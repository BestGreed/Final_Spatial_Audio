#ifndef FSA_FLUENT_MENU_H
#define FSA_FLUENT_MENU_H
#include <windows.h>
typedef struct { int selected,automatic,startup; unsigned available; } FSA_MenuState;
UINT fsa_popup(HWND owner,const FSA_MenuState *state);
#endif
