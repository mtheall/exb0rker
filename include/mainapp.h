#pragma once
#include <feos.h>
#include <coopgui.h>
using namespace FeOS::UI;

#define NUM_ENTRIES 11

typedef struct {
  u16  *buf;
  int  size;
  bool stale;
} canvas_t;

// Main application class
class MainApp : public CApplication {
private:
  char          cwd[FILENAME_MAX];
  FontPtr       font;
  struct dirent **dirList;
  int           numDirs;
  int           selected;
  int           scroll;
  canvas_t      cwdstr;
  canvas_t      info;
  canvas_t      list;

  void redrawCwd();
  void redrawInfo();
  void redrawList();

public:
  MainApp();
  ~MainApp();
  void OnActivate();
  void OnDeactivate();
  void OnVBlank();
};
