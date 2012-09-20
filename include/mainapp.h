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

// Main application class
class MainApp : public CApplication {
private:
  char    cwd[FILENAME_MAX];
  entry_t entries[NUM_ENTRIES];
  FontPtr font;
  struct dirent **dirList;
  int numDirs;
  int selected;
  int scroll;

  void redraw();

public:
  MainApp();
  ~MainApp();
  void OnActivate();
  void OnDeactivate();
  void OnVBlank();
};
