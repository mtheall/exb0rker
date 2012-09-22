#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include "scandir.h"
#include "mainapp.h"
#include "gfx.h"
#include <sys/stat.h>

IGuiManager* g_guiManager;

typedef struct {
  u16 *src, *main, *sub;
  u32 len;
} icon_t;

enum {
  ICON_FILE = 0,
  ICON_FOLDER,
  ICON_FX2,
  ICON_COPY,
  ICON_CUT,
  ICON_PASTE,
  ICON_RENAME,
  ICON_DELETE,
  ICON_NO,
  ICON_YES,
};

static icon_t icons[] = {
  [ICON_FILE]   = { (u16*)fileBitmap,   NULL, NULL, fileBitmapLen,   },
  [ICON_FOLDER] = { (u16*)folderBitmap, NULL, NULL, folderBitmapLen, },
  [ICON_FX2]    = { (u16*)fx2Bitmap,    NULL, NULL, fx2BitmapLen,    },
  [ICON_COPY]   = { (u16*)copyTiles,    NULL, NULL, copyTilesLen,    },
  [ICON_CUT]    = { (u16*)cutTiles,     NULL, NULL, cutTilesLen,     },
  [ICON_PASTE]  = { (u16*)pasteTiles,   NULL, NULL, pasteTilesLen,   },
  [ICON_RENAME] = { (u16*)renameTiles,  NULL, NULL, renameTilesLen,  },
  [ICON_DELETE] = { (u16*)deleteTiles,  NULL, NULL, deleteTilesLen,  },
  [ICON_NO]     = { (u16*)noTiles,      NULL, NULL, noTilesLen,      },
  [ICON_YES]    = { (u16*)yesTiles,     NULL, NULL, yesTilesLen,     },
};
#define NUM_ICONS (sizeof(icons)/sizeof(icons[0]))

MainApp::MainApp() {
  SetTitle("FeOS File Manager");
  SetIcon((color_t*)appiconBitmap);
  dirList    = NULL;
  numDirs    =  0;
  selected   = -1;
  scroll     =  0;
  memset(&cwdstr, 0, sizeof(cwdstr));
  memset(&info,   0, sizeof(info));
  memset(&list,   0, sizeof(list));
  swapped = false;
  command = COMMAND_NONE;
}

MainApp::~MainApp() {
  if(dirList != NULL)
    freescandir(dirList, numDirs);
}

void MainApp::OnActivate() {
  // get font
  font = g_guiManager->GetSystemFont();

  // initialize video
  lcdMainOnBottom();
  videoSetMode   (MODE_3_2D);
  videoSetModeSub(MODE_3_2D);
  vramSetPrimaryBanks(VRAM_A_MAIN_BG, VRAM_B_MAIN_SPRITE, VRAM_C_SUB_BG, VRAM_D_SUB_SPRITE);

  // initialize backgrounds
  int botfb  = bgInit   (3, BgType_Bmp16,    BgSize_B16_256x256, 0, 0);
  int topovl = bgInitSub(2, BgType_Text8bpp, BgSize_T_256x256,  15, 0);
  int topfb  = bgInitSub(3, BgType_Bmp16,    BgSize_B16_256x256, 2, 0);
  bgSetPriority(topovl, 3);
  bgSetPriority(botfb,  2);

  // initialize canvases
  cwdstr.buf   = &bgGetGfxPtr(topfb)[256*144];
  cwdstr.size  = 256*40*sizeof(u16);
  cwdstr.stale = true;
  info.buf     = &bgGetGfxPtr(topfb)[256*16];
  info.size    = 256*72*sizeof(u16);
  info.stale   = true;
  list.buf     = bgGetGfxPtr(botfb);
  list.size    = 256*192*sizeof(u16);
  list.stale   = true;

  // clear framebuffers
  dmaFillHalfWords(Colors::Transparent, bgGetGfxPtr(topfb), 256*192*sizeof(u16));
  dmaFillHalfWords(Colors::Transparent, bgGetGfxPtr(botfb), 256*192*sizeof(u16));

  // load top screen tiled background
  dmaCopy(topbgTiles, bgGetGfxPtr(topovl), topbgTilesLen);
  dmaCopy(topbgMap,   bgGetMapPtr(topovl), topbgMapLen);
  dmaCopy(topbgPal,   BG_PALETTE_SUB,      topbgPalLen);

  // set the backdrop color
  BG_PALETTE[0]     = RGB15(30, 31, 31);
  BG_PALETTE_SUB[0] = RGB15(30, 31, 31);

  // initialize OAM
  oamInit(&oamMain, SpriteMapping_Bmp_1D_128, false);
  oamInit(&oamSub,  SpriteMapping_Bmp_1D_128, false);

  // copy sprite palette
  dmaCopy(commandPal, SPRITE_PALETTE_SUB, commandPalLen);

  // allocate sprites for icons
  for(int i = ICON_FILE; i <= ICON_FX2; i++) {
    if(icons[i].main == NULL) icons[i].main = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);
    if(icons[i].sub  == NULL) icons[i].sub  = oamAllocateGfx(&oamSub,  SpriteSize_16x16, SpriteColorFormat_Bmp);
  }
  for(int i = ICON_COPY; i <= ICON_YES; i++)
    if(icons[i].sub  == NULL) icons[i].sub  = oamAllocateGfx(&oamSub,  SpriteSize_16x16, SpriteColorFormat_256Color);

  // copy sprites into vram
  for(unsigned int i = 0; i < NUM_ICONS; i++) {
    if(icons[i].main != NULL) dmaCopy(icons[i].src, icons[i].main, icons[i].len);
    if(icons[i].sub  != NULL) dmaCopy(icons[i].src, icons[i].sub,  icons[i].len);
  }

  // reinitialize directory listing
  if(dirList != NULL)
    freescandir(dirList, numDirs);

  numDirs = scandir(".", &dirList, generic_scandir_filter, generic_scandir_compar);
  selected = -1;
  scroll   = 0;

  keysSetRepeat(15, 4);
}

void MainApp::OnDeactivate() {
}

void MainApp::OnVBlank() {
  touchPosition touch;
  word_t        down      = keysDown();
  word_t        repeat    = keysDownRepeat();

  // draw the scene ASAP
  if(cwdstr.stale) redrawCwd();
  if(info.stale)   redrawInfo();
  if(list.stale)   redrawList();

  if(down & KEY_TOUCH)
    touchRead(&touch);

  if(down & KEY_START)
    swapped = !swapped;

  if(swapped) {
    lcdMainOnTop();
    processSubScreen(touch, down, repeat);
  }
  else {
    lcdMainOnBottom();
    processMainScreen(touch, down, repeat);
  }

  // update scroll
  if((repeat & KEY_DOWN) && scroll < numDirs - (192-8)/16) {
    scroll++;
    list.stale = true;
  }
  else if((repeat & KEY_UP) && scroll > 0) {
    scroll--;
    list.stale = true;
  }

  // check for exit
  if(down & KEY_B) {
    Close();
    return;
  }
}

void MainApp::processMainScreen(touchPosition &touch, int down, int repeat) {
  char directory[256];
  int  selection = -1;

  if(down & KEY_TOUCH) {
    if(dirList != NULL) {
      if((touch.py-8)/16 + scroll < numDirs
      && (touch.py-8)/16 >= 0
      && (touch.py-8)/16 < (192-8)/16) {
        // determine what was touched
        selection = (touch.py-8)/16 + scroll;

        // update the selection
        if(selection != selected) {
          surface_t surface[2] = {
            { &list.buf[(selected -scroll)*256*16 + 256*8], 256, 16, 256, },
            { &list.buf[(selection-scroll)*256*16 + 256*8], 256, 16, 256, },
          };
          if(selected != -1)
            font->PrintText(&surface[0], 24, 16-4, dirList[selected]->d_name, Colors::Black, PrintTextFlags::AtBaseline);
          font->PrintText(&surface[1], 24, 16-4, dirList[selection]->d_name, Colors::Blue, PrintTextFlags::AtBaseline);
          selected = selection;
          info.stale = true;
        }
        else { // we have selected a selected direntry
          // open a directory
          if(TYPE_DIR(dirList[selected]->d_type)) {
            strcpy(directory, dirList[selected]->d_name);
            freescandir(dirList, numDirs);

            // move to the new directory
            chdir(directory);

            // scan the new directory
            numDirs = scandir(".", &dirList, generic_scandir_filter, generic_scandir_compar);

            // reset the selected direntry and scroll
            selected     = -1;
            scroll       = 0;
            cwdstr.stale = true;
            info.stale   = true;
            list.stale   = true;
            return;
          }
        }
      }
    }
  }
}

void MainApp::processSubScreen(touchPosition &touch, int down, int repeat) {
  command_t cmd = COMMAND_NONE;

  for(int i = ICON_COPY; cmd == COMMAND_NONE && i <= ICON_DELETE; i++) {
    if(touch.px > i*24 + 8 && touch.px < (i+1)*24 &&
       touch.py > 128      && touch.py < 128+16) {
      cmd = (command_t)(i - (int)ICON_COPY + (int)COMMAND_COPY);

      switch(cmd) {
        case COMMAND_COPY:
          if(selected != -1 && strcmp("..", dirList[selected]->d_name) != 0) {
            strcpy(file, cwd);
            strcat(file, dirList[selected]->d_name);
            command = COMMAND_COPY;
            fprintf(stderr, "Copy %s\n", file);
          }
          break;
        case COMMAND_CUT:
          if(selected != -1 && strcmp("..", dirList[selected]->d_name) != 0) {
            strcpy(file, cwd);
            strcat(file, dirList[selected]->d_name);
            command = COMMAND_CUT;
            fprintf(stderr, "Cut %s\n", file);
          }
          break;
        case COMMAND_PASTE:
          if(command == COMMAND_COPY) {
            fprintf(stderr, "Copy %s, Paste into %s\n", file, cwd);
            command = COMMAND_NONE;
          }
          if(command == COMMAND_CUT) {
            fprintf(stderr, "Cut %s, Paste into %s\n", file, cwd);
            command = COMMAND_NONE;
          }
          break;
        case COMMAND_RENAME:
          if(selected != -1 && strcmp("..", dirList[selected]->d_name) != 0) {
            fprintf(stderr, "Rename %s\n", dirList[selected]->d_name);
          }
          break;
        case COMMAND_DELETE:
          if(selected != -1 && strcmp("..", dirList[selected]->d_name) != 0) {
            fprintf(stderr, "Delete %s\n", dirList[selected]->d_name);
          }
          break;
        default:
          break;
      }
    }
  }
}

void MainApp::redrawList() {
  surface_t surface = { NULL, 256, 16, 256, };

  list.stale = false;

  // clear all the sprites
  oamClear(&oamMain, 0, 0);

  // clear the list
  dmaFillWords(Colors::Transparent, list.buf, list.size);

  // update the list
  for(int i = 0; i < numDirs && i < NUM_ENTRIES; i++) {
    // draw blue if selected, black if not selected
    surface.buffer = &list.buf[i*256*16 + 256*8];
    if(scroll+i == selected)
      font->PrintText(&surface, 24, 16-4, dirList[scroll+i]->d_name, Colors::Blue, PrintTextFlags::AtBaseline);
    else
      font->PrintText(&surface, 24, 16-4, dirList[scroll+i]->d_name, Colors::Black, PrintTextFlags::AtBaseline);
  }

  // draw the sprites
  for(int i = 0; i < numDirs && i < NUM_ENTRIES; i++) {
    // this is a directory, give it a folder sprite!
    if(TYPE_DIR(dirList[scroll+i]->d_type)) {
      oamSet(&oamMain, i, 4, 8+16*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             icons[ICON_FOLDER].main, -1, false, false, false, false, false);
    }
    // this is an fx2, give it an executable sprite!
    else if(strcmp(".fx2", dirList[scroll+i]->d_name + strlen(dirList[scroll+i]->d_name)-4) == 0) {
      oamSet(&oamMain, i, 4, 8+16*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             icons[ICON_FX2].main, -1, false, false, false, false, false);
    }
    // this is something else, give it a file sprite!
    else {
      oamSet(&oamMain, i, 4, 8+16*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             icons[ICON_FILE].main, -1, false, false, false, false, false);
    }
  }
}

void MainApp::redrawInfo() {
  struct stat statbuf;
  char str[1024];
  surface_t surface = { info.buf + 16, 256 - 16*2, 72, 256 };

  info.stale = false;

  // clear the graphics
  oamClear(&oamSub,  0, 0);
  dmaFillHalfWords(Colors::Transparent, info.buf, info.size);

  // set up the command sprites
  for(int i = ICON_COPY; i <= ICON_DELETE; i++)
    oamSet(&oamSub, i, i*24 + 8, 128, 0, 0, SpriteSize_16x16, SpriteColorFormat_256Color,
      icons[i].sub, -1, false, false, false, false, false);

  if(selected == -1)
    return;

  stat(dirList[selected]->d_name, &statbuf);

  sprintf(str, "%s\n", dirList[selected]->d_name);
  oamSet(&oamSub, 0, 14, 18, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
         icons[ICON_FOLDER].sub, -1, false, false, false, false, false);
  if(!TYPE_DIR(dirList[selected]->d_type)) {
    if(strcmp(".fx2", dirList[selected]->d_name + strlen(dirList[selected]->d_name)-4) == 0)
      oamSet(&oamSub, 0, 14, 18, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             icons[ICON_FX2].sub, -1, false, false, false, false, false);
    else
      oamSet(&oamSub, 0, 14, 18, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             icons[ICON_FILE].sub, -1, false, false, false, false, false);
    strcat(str, "File\nSize: "); // TODO: description
    if(statbuf.st_size < 1000)
      sprintf(str+strlen(str), "%u byte%c\n", statbuf.st_size, statbuf.st_size != 1 ? 's' : ' ');
    else if(statbuf.st_size < 10240)
      sprintf(str+strlen(str), "%u.%02u KB\n", statbuf.st_size/1024,
                           (statbuf.st_size%1024)*100/1024);
    else if(statbuf.st_size < 102400)
      sprintf(str+strlen(str), "%u.%01u KB\n", statbuf.st_size/1024,
                           (statbuf.st_size%1024)*10/1024);
    else if(statbuf.st_size < 1000000)
      sprintf(str+strlen(str), "%u KB\n", statbuf.st_size/1024);
    else if(statbuf.st_size < 10485760)
      sprintf(str+strlen(str), "%u.%02u MB\n", statbuf.st_size/1048576,
                           (statbuf.st_size%1048576)*100/1048576);
    else if(statbuf.st_size < 104857600)
      sprintf(str+strlen(str), "%u.%01u MB\n", statbuf.st_size/1048576,
                           (statbuf.st_size%1048576)*10/1048576);
    else
      sprintf(str+strlen(str), "%u MB\n", statbuf.st_size/1048576);
  }
  else if(strcmp("..", dirList[selected]->d_name) == 0)
    strcat(str, "Parent Directory");
  else
    strcat(str, "Directory");

  font->PrintText(&surface, 16, 16-1, str, Colors::Black, PrintTextFlags::AtBaseline);
}

void MainApp::redrawCwd() {
  surface_t surface = { cwdstr.buf + 16, 256 - 16*2, 40, 256 };

  cwdstr.stale = false;

  getcwd(cwd, sizeof(cwd));
  dmaFillHalfWords(Colors::Transparent, cwdstr.buf, cwdstr.size);
  font->PrintText(&surface, 0, 16-1, cwd, Colors::Black, PrintTextFlags::AtBaseline);
}

int main() {
  MainApp *app = new MainApp();

  g_guiManager = GetGuiManagerChecked();
  g_guiManager->RunApplication(app);

  delete app;

  return 0;
}
