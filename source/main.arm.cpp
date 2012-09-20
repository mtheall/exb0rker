#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include "scandir.h"
#include "mainapp.h"
#include "file.h"
#include "folder.h"
#include "fx2.h"
#include "appicon.h"
#include <feos3d.h>

IGuiManager* g_guiManager;

typedef struct {
  u16 *main, *sub;
} icon_t;

static icon_t fileIcon   = { NULL, NULL, };
static icon_t folderIcon = { NULL, NULL, };
static icon_t fx2Icon    = { NULL, NULL, };

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
  font = g_guiManager->GetSystemFont();

  lcdMainOnBottom();
  videoSetMode   (MODE_0_3D);
  videoSetModeSub(MODE_3_2D);
  vramSetPrimaryBanks(VRAM_A_TEXTURE, VRAM_B_MAIN_SPRITE, VRAM_C_SUB_BG, VRAM_D_SUB_SPRITE);

  oamInit(&oamMain, SpriteMapping_Bmp_1D_128, false);
  oamInit(&oamSub,  SpriteMapping_Bmp_1D_128, false);

  if(fileIcon.main   == NULL) fileIcon.main   = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(fileIcon.sub    == NULL) fileIcon.sub    = oamAllocateGfx(&oamSub,  SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(folderIcon.main == NULL) folderIcon.main = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(folderIcon.sub  == NULL) folderIcon.sub  = oamAllocateGfx(&oamSub,  SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(fx2Icon.main    == NULL) fx2Icon.main    = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(fx2Icon.sub     == NULL) fx2Icon.sub     = oamAllocateGfx(&oamSub,  SpriteSize_16x16, SpriteColorFormat_Bmp);

  DC_FlushAll();
  dmaCopy(fileBitmap,   fileIcon.main,   fileBitmapLen);
  dmaCopy(fileBitmap,   fileIcon.sub,    fileBitmapLen);
  dmaCopy(folderBitmap, folderIcon.main, folderBitmapLen);
  dmaCopy(folderBitmap, folderIcon.sub,  folderBitmapLen);
  dmaCopy(fx2Bitmap,    fx2Icon.main,    fx2BitmapLen);
  dmaCopy(fx2Bitmap,    fx2Icon.sub,     fx2BitmapLen);

  glInit();
  glEnable(GL_TEXTURE_2D|GL_BLEND);
  glClearColor(31, 31, 31, 31);
  glClearPolyID(63);
  glClearDepth(GL_MAX_DEPTH);
  glViewport(0, 0, 255, 191);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrthof32(0, 256, 192, 0, -1<<12, 1<<12);
  glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glColor(RGB15(31, 31, 31));
  glMaterialf(GL_AMBIENT,  RGB15(31, 31, 31));
  glMaterialf(GL_DIFFUSE,  RGB15(31, 31, 31));
  glMaterialf(GL_EMISSION, RGB15(31, 31, 31));
  glMaterialf(GL_SPECULAR, ARGB16(1, 31, 31, 31));
  glMaterialShinyness();

  for(u32 i = 0; i < sizeof(entries)/sizeof(entries[0]); i++) {
    glGenTextures(1, &entries[i].texture);
    glBindTexture(GL_TEXTURE_2D, entries[i].texture);
    glTexImage2D(0, 0, GL_RGBA, TEXTURE_SIZE_256, TEXTURE_SIZE_16, 0, TEXGEN_TEXCOORD, NULL);
    entries[i].entry = -1;
    entries[i].selected = false;
    entries[i].oldSelected = false;
  }

  if(dirList != NULL)
    freescandir(dirList, numDirs);
  numDirs = scandir(".", &dirList, generic_scandir_filter, generic_scandir_compar);
  redraw();

  selected = -1;
  scroll   = 0;

  keysSetRepeat(15, 4);
}

void MainApp::OnDeactivate() {
  for(int i = 0; i < NUM_ENTRIES; i++)
    glDeleteTextures(1, &entries[i].texture);
  glDeinit();
}

void MainApp::OnVBlank() {
  touchPosition touch;
  char          directory[256];
  int           selection = -1;
  word_t        down      = keysDown();
  word_t        repeat    = keysDownRepeat();

  if(down & KEY_TOUCH) {
    touchRead(&touch);

    if(dirList != NULL) {
      if((touch.py-8)/16 + scroll < numDirs
      && (touch.py-8)/16 >= 0
      && (touch.py-8)/16 < (192-8)/16) {
        selection = (touch.py-8)/16 + scroll;
        if(selection != selected) {
          selected = selection;
          for(int i = 0; i < NUM_ENTRIES; i++)
            entries[i].selected = entries[i].entry == selected;
        }
        else {
          if(TYPE_DIR(dirList[selected]->d_type)) {
            strcpy(directory, dirList[selected]->d_name);
            freescandir(dirList, numDirs);
            chdir(directory);
            numDirs = scandir(".", &dirList, generic_scandir_filter, generic_scandir_compar);
            for(int i = 0; i < NUM_ENTRIES; i++) {
              entries[i].entry = -1;
              entries[i].selected = false;
              entries[i].oldSelected = false;
            }
            selected = -1;
            scroll = 0;
          }
        }
      }
    }
  }

  if((repeat & KEY_DOWN) && scroll < numDirs - (192-8)/16) {
    scroll++;
  }
  else if((repeat & KEY_UP) && scroll > 0) {
    scroll--;
  }

  if (down & KEY_B) {
    Close();
    return;
  }

  redraw();
}

void MainApp::redraw() {
  u32 offset;
  int tex;
  oamClear(&oamMain, 0, 0);
  oamClear(&oamSub,  0, 0);

  vramSetBankA(VRAM_A_LCD);
  for(int i = 0; i < numDirs && i < NUM_ENTRIES-2; i++) {
    offset = (scroll + i) % NUM_ENTRIES;
    tex = entries[offset].texture;
    glBindTexture(GL_TEXTURE_2D, tex);
    if(entries[offset].entry != scroll+i || entries[offset].selected != entries[offset].oldSelected) {
      entries[offset].entry = scroll+i;
      dmaFillWords(0, glGetTexturePointer(tex), 256*16*sizeof(u16));
      if(scroll+i == selected)
        font->PrintText((color_t*)glGetTexturePointer(tex), 24, 0, dirList[scroll+i]->d_name, Colors::Blue);
      else
        font->PrintText((color_t*)glGetTexturePointer(tex), 24, 0, dirList[scroll+i]->d_name, Colors::Black);
    }
    entries[offset].oldSelected = entries[offset].selected;
  }
  vramSetBankA(VRAM_A_TEXTURE);

  glBegin(GL_QUADS);
  for(int i = 0; i < numDirs && i < NUM_ENTRIES-2; i++) {
    if(TYPE_DIR(dirList[scroll+i]->d_type)) {
      oamSet(&oamMain, i, 4, 8+16*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             folderIcon.main, -1, false, false, false, false, false);
    }
    else if(strcmp(".fx2", dirList[scroll+i]->d_name + strlen(dirList[scroll+i]->d_name)-4) == 0) {
      oamSet(&oamMain, i, 4, 8+16*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             fx2Icon.main, -1, false, false, false, false, false);
    }
    else {
      oamSet(&oamMain, i, 4, 8+16*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             fileIcon.main, -1, false, false, false, false, false);
    }

    offset = (scroll + i) % NUM_ENTRIES;
    glBindTexture(GL_TEXTURE_2D, entries[offset].texture);
    glTexCoord2t16(inttot16(0), inttot16(0));
    glVertex3v16(0, 8+16*i, 2);
    glTexCoord2t16(inttot16(0), inttot16(16));
    glVertex3v16(0, 8+16*i+16, 2);
    glTexCoord2t16(inttot16(256), inttot16(16));
    glVertex3v16(256, 8+16*i+16, 2);
    glTexCoord2t16(inttot16(256), inttot16(0));
    glVertex3v16(256, 8+16*i, 2);
  }
  glEnd();
  glFlush(0);
}

int main() {
  MainApp app;

  g_guiManager = GetGuiManagerChecked();
  g_guiManager->RunApplication(&app);

  return 0;
}
