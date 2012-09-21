#pragma once
#include <feos.h>
#include <coopgui.h>
using namespace FeOS::UI;

typedef struct {
  int     texture;
  int     entry;
  bool    selected;
  bool    oldSelected;
} entry_t;
#define NUM_ENTRIES (13)
#define COLOR_UIBACKDROP MakeColor(30,31,31)

// Main application class
class MainApp : public CApplication {
private:
  char    cwd[FILENAME_MAX];
  entry_t entries[NUM_ENTRIES];
  FontPtr font;
  struct dirent **dirList;
  u16* topBmpBuf;
  int numDirs;
  int selected;
  int scroll;
  bool needToRedrawInfo;

  void redraw();
  void redrawInfo();
  void redrawCwd();

public:
  MainApp();
  ~MainApp();
  void OnActivate();
  void OnDeactivate();
  void OnVBlank();
};
