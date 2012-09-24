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

typedef enum {
  COMMAND_NONE = 0,
  COMMAND_COPY,
  COMMAND_CUT,
  COMMAND_PASTE,
  COMMAND_RENAME,
  COMMAND_DELETE,
} command_t;

// Main application class
class MainApp : public CApplication {
private:
  char          cwd[FILENAME_MAX];
  char          file[FILENAME_MAX];
  FontPtr       font;
  struct dirent **dirList;
  int           numDirs;
  int           selected;
  int           scroll;
  canvas_t      cwdstr;
  canvas_t      info;
  canvas_t      list;
  bool          swapped;
  command_t     command;

  void redrawCwd();
  void redrawInfo();
  void redrawList();
  void processMainScreen(touchPosition &touch, int down, int repeat);
  void processSubScreen(touchPosition &touch, int down, int repeat);
  void Copy();
  void Move();
  void Delete();
  void Rename();

public:
  MainApp();
  ~MainApp();
  void OnActivate();
  void OnDeactivate();
  void OnVBlank();
};
