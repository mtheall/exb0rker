#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include "scandir.h"
#include "mainapp.h"
#include "file.h"
#include "folder.h"
#include "fx2.h"
#include "appicon.h"

IGuiManager* g_guiManager;

static u16* fileIcon   = NULL;
static u16* folderIcon = NULL;
static u16* fx2Icon    = NULL;
static u16* icons[3];


MainApp::MainApp() {
  SetTitle("FeOS File Manager");
  SetIcon((color_t*)appiconBitmap);
  dirList  = NULL;
  numDirs  = 0;
  selected = -1;
  scroll   = 0;
}

MainApp::~MainApp() {
  if(dirList != NULL)
    freescandir(dirList, numDirs);
}

void MainApp::OnActivate() {
  int bg;

  font       = g_guiManager->GetSystemFont();
  fontHeight = font->GetHeight();

  lcdMainOnBottom();
  videoSetMode(MODE_3_2D);
  vramSetPrimaryBanks(VRAM_A_MAIN_BG, VRAM_B_MAIN_SPRITE, VRAM_C_LCD, VRAM_D_LCD);

  bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
  buf = bgGetGfxPtr(bg);

  dmaFillHalfWords(Colors::White, buf, 256*192*2);

  oamInit(&oamMain, SpriteMapping_Bmp_1D_128, false);
  if(fileIcon   == NULL) icons[0] = fileIcon   = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(folderIcon == NULL) icons[1] = folderIcon = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(fx2Icon    == NULL) icons[2] = fx2Icon    = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);

  DC_FlushAll();
  dmaCopy(fileBitmap,   fileIcon,   fileBitmapLen);
  dmaCopy(folderBitmap, folderIcon, folderBitmapLen);
  dmaCopy(fx2Bitmap,    fx2Icon,    fx2BitmapLen);

  if(dirList != NULL)
    freescandir(dirList, numDirs);
  numDirs = scandir(".", &dirList, generic_scandir_filter, generic_scandir_compar);
  selected = -1;
  scroll   = 0;
}

void MainApp::OnVBlank() {
  touchPosition touch;
  char          directory[256];
  int           selection = -1;
  word_t        down      = keysDown();

  if(down & KEY_TOUCH) {
    touchRead(&touch);

    if(dirList != NULL) {
      if((touch.py-8)/fontHeight + scroll < numDirs
      && (touch.py-8)/fontHeight >= 0
      && (touch.py-8)/fontHeight < (192-8)/fontHeight) {
        selection = (touch.py-8)/fontHeight + scroll;
        if(selection != selected) {
          selected = selection;
        }
        else {
          if(TYPE_DIR(dirList[selected]->d_type)) {
            strcpy(directory, dirList[selected]->d_name);
            freescandir(dirList, numDirs);
            chdir(directory);
            numDirs = scandir(".", &dirList, generic_scandir_filter, generic_scandir_compar);
            selected = -1;
            scroll = 0;
          }
        }
      }
    }
  }

  if((down & KEY_DOWN) && scroll < numDirs - (192-8)/fontHeight) {
    scroll++;
  }
  else if((down & KEY_UP) && scroll > 0) {
    scroll--;
  }

  if (down & KEY_B) {
    Close();
    return;
  }

  print();
}

void MainApp::print() {
  oamClear(&oamMain, 0, 0);
  dmaFillHalfWords(Colors::White, buf, 256*192*2);

  for(int i = 0; i < numDirs && i < (192-8)/fontHeight; i++) {
    if(TYPE_DIR(dirList[scroll+i]->d_type))
      oamSet(&oamMain, i, 4, 8+fontHeight*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             folderIcon, -1, false, false, false, false, false);
    else if(strcmp(".fx2", dirList[scroll+i]->d_name + strlen(dirList[scroll+i]->d_name)-4) == 0)
      oamSet(&oamMain, i, 4, 8+fontHeight*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             fx2Icon, -1, false, false, false, false, false);
    else
      oamSet(&oamMain, i, 4, 8+fontHeight*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             fileIcon, -1, false, false, false, false, false);
    if(scroll+i == selected)
      font->PrintText(buf, 24, 8+fontHeight*i, dirList[scroll+i]->d_name, Colors::Blue);
    else
      font->PrintText(buf, 24, 8+fontHeight*i, dirList[scroll+i]->d_name, Colors::Black);
  }
}

int main() {
  static MainApp *app = NULL;

  if(app == NULL)
    app = new MainApp();

  g_guiManager = GetGuiManagerChecked();
  g_guiManager->RunApplication(app);

  return 0;
}
