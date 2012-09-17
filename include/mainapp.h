#pragma once
#include <coopgui.h>
using namespace FeOS::UI;

// Main application class
class MainApp : public CApplication {
private:
  color_t* buf;
  FontPtr font;
  int fontHeight;
  struct dirent **dirList;
  int numDirs;
  int selected;
  int scroll;

  void print();

public:
  MainApp();
  ~MainApp();
  void OnActivate();
  void OnVBlank();
};
