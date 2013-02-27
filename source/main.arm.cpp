#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include "scandir.h"
#include "mainapp.h"
#include "gfx.h"

IGuiManager* g_guiManager;

typedef struct {
  u16 *src, *main, *sub;
  u32 len;
} icon_t;

enum {
  ICON_COPY = 0,
  ICON_CUT,
  ICON_PASTE,
  ICON_RENAME,
  ICON_DELETE,
  ICON_NO,
  ICON_YES,

  NUM_HARDCODED_ICONS,
  FIRST_FILE_ICON = NUM_HARDCODED_ICONS,
  LAST_FILE_ICON = FIRST_FILE_ICON + NUM_ENTRIES - 1,
  NUM_ICONS
};

static icon_t icons[NUM_ICONS] = {
  [ICON_COPY]   = { (u16*)copyTiles,    NULL, NULL, copyTilesLen,    },
  [ICON_CUT]    = { (u16*)cutTiles,     NULL, NULL, cutTilesLen,     },
  [ICON_PASTE]  = { (u16*)pasteTiles,   NULL, NULL, pasteTilesLen,   },
  [ICON_RENAME] = { (u16*)renameTiles,  NULL, NULL, renameTilesLen,  },
  [ICON_DELETE] = { (u16*)deleteTiles,  NULL, NULL, deleteTilesLen,  },
  [ICON_NO]     = { (u16*)noTiles,      NULL, NULL, noTilesLen,      },
  [ICON_YES]    = { (u16*)yesTiles,     NULL, NULL, yesTilesLen,     },
};

MainApp::MainApp() {
  SetTitle("FeOS File Manager");
  SetIcon((color_t*)appiconBitmap);
  dirList     = NULL;
  numDirs     =  0;
  selected    = -1;
  scroll      =  0;
  statusTimer =  0;
  memset(&cwdstr, 0, sizeof(cwdstr));
  memset(&info,   0, sizeof(info));
  memset(&list,   0, sizeof(list));
  command = COMMAND_NONE;
  state = STATE_PROCESS_MAIN;
  pIcons = NULL;
}

MainApp::~MainApp() {
  if(dirList != NULL)
  {
    freescandir(dirList, numDirs);
    delete [] pIcons;
  }
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
  status.buf   = &bgGetGfxPtr(topfb)[256*90];
  status.size  = 256*48*sizeof(u16);
  status.stale = false;

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
  for (int i = 0; i < NUM_ICONS; i ++) {
    if (i < NUM_HARDCODED_ICONS) {
      icons[i].sub = oamAllocateGfx(&oamSub, SpriteSize_16x16, SpriteColorFormat_256Color);
      // copy the sprite into vram
      dmaCopy(icons[i].src, icons[i].sub, icons[i].len);
	} else
      icons[i].main = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);
  }
  icons[FIRST_FILE_ICON].sub = oamAllocateGfx(&oamSub, SpriteSize_16x16, SpriteColorFormat_Bmp);

  // reinitialize directory listing
  if(dirList != NULL) {
    freescandir(dirList, numDirs);
    delete [] pIcons;
  }

  numDirs = scandir(".", &dirList, generic_scandir_filter, generic_scandir_compar);
  selected = -1;
  scroll   = 0;
  loadIcons();
  oamClear(&oamSub, 0, 1);

  keysSetRepeat(15, 4);
}

void MainApp::loadIcons() {
  pIcons = new FileIconPtr[numDirs];
  for (int i = 0; i < numDirs; i ++)
    pIcons[i] = g_guiManager->GetFileIcon(dirList[i]->d_name);
}

void MainApp::OnDeactivate() {
}

void MainApp::OnVBlank() {
  touchPosition touch;
  word_t        down   = keysDown();
  word_t        repeat = keysDownRepeat();

  if(down & KEY_TOUCH)
    touchRead(&touch);

  // draw the scene ASAP
  if(cwdstr.stale) redrawCwd();
  if(info.stale)   redrawInfo();
  if(list.stale)   redrawList();

  if(statusTimer > 0) {
    statusTimer--;
    if(statusTimer == 0)
      dmaFillHalfWords(0, status.buf, status.size);
  }

  oamClear(&oamSub, 1, 7);

  switch(state) {
    case STATE_PROCESS_MAIN:
      lcdMainOnBottom();
      processMainScreen(touch, down, repeat);
      break;
    case STATE_PROCESS_SUB:
      lcdMainOnTop();
      processSubScreen(touch, down, repeat);
      break;
    case STATE_COPY:
      Copy(touch, down, repeat);
      break;
    case STATE_MOVE:
      Move(touch, down, repeat);
      break;
    case STATE_DELETE:
      Delete(touch, down, repeat);
      break;
    case STATE_RENAME:
      Rename(touch, down, repeat);
      break;
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

  if(down & KEY_START) {
    state = STATE_PROCESS_SUB;
    return;
  }

  // set up the command sprites
  if(selected != -1) {
    for(int i = ICON_COPY; i <= ICON_DELETE; i++)
      oamSet(&oamSub, i+2, (i+2)*24 + 8, 128, 0, 0, SpriteSize_16x16, SpriteColorFormat_256Color,
        icons[i].sub, -1, false, false, false, false, false);
  }

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
            delete [] pIcons;

            // move to the new directory
            chdir(directory);

            // scan the new directory
            numDirs = scandir(".", &dirList, generic_scandir_filter, generic_scandir_compar);
            loadIcons();

            // reset the selected direntry and scroll
            selected     = -1;
            scroll       = 0;
            cwdstr.stale = true;
            info.stale   = true;
            list.stale   = true;
            return;
          }
          else {
            char tmpBuf[256];
            getcwd(tmpBuf, sizeof(tmpBuf));
            strncat(tmpBuf, dirList[selected]->d_name, sizeof(tmpBuf));
            g_guiManager->OpenFile(tmpBuf);
          }
        }
      }
    }
  }
}

void MainApp::processSubScreen(touchPosition &touch, int down, int repeat) {
  command_t cmd = COMMAND_NONE;

  if(down & KEY_START) {
    state = STATE_PROCESS_MAIN;
    return;
  }

  // set up the command sprites
  if(selected != -1) {
    for(int i = ICON_COPY; i <= ICON_DELETE; i++)
      oamSet(&oamSub, i+2, (i+2)*24 + 8, 128, 0, 0, SpriteSize_16x16, SpriteColorFormat_256Color,
        icons[i].sub, -1, false, false, false, false, false);
  }

  for(int i = ICON_COPY; cmd == COMMAND_NONE && i <= ICON_DELETE; i++) {
    if(touch.px > (i+2)*24 + 8 && touch.px < (i+3)*24 &&
       touch.py > 128      && touch.py < 128+16) {
      cmd = (command_t)(i - (int)ICON_COPY + (int)COMMAND_COPY);

      switch(cmd) {
        case COMMAND_COPY:
          if(selected != -1 && strcmp("..", dirList[selected]->d_name) != 0) {
            strcpy(file, cwd);
            strcat(file, dirList[selected]->d_name);
            command = COMMAND_COPY;
          }
          break;
        case COMMAND_CUT:
          if(selected != -1 && strcmp("..", dirList[selected]->d_name) != 0) {
            strcpy(file, cwd);
            strcat(file, dirList[selected]->d_name);
            command = COMMAND_CUT;
          }
          break;
        case COMMAND_PASTE:
          if(command == COMMAND_COPY) {
            command = COMMAND_NONE;
            state = STATE_COPY;
          }
          if(command == COMMAND_CUT) {
            command = COMMAND_NONE;
            state = STATE_MOVE;
          }
          break;
        case COMMAND_RENAME:
          if(selected != -1 && strcmp("..", dirList[selected]->d_name) != 0) {
            state = STATE_RENAME;
          }
          break;
        case COMMAND_DELETE:
          if(selected != -1 && strcmp("..", dirList[selected]->d_name) != 0) {
            state = STATE_DELETE;
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
  oamClear(&oamMain, 0, 11);

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
    u16* gfxPtr = icons[FIRST_FILE_ICON + i].main;
    oamSet(&oamMain, i, 4, 8+16*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             gfxPtr, -1, false, false, false, false, false);
    // this is a directory, give it a folder sprite!
    if(TYPE_DIR(dirList[scroll+i]->d_type))
      dmaCopy(folderBitmap, gfxPtr, folderBitmapLen);
	else
	  dmaCopy(pIcons[scroll+i]->GetData(), gfxPtr, folderBitmapLen);
  }
}

void MainApp::redrawInfo() {
  struct stat statbuf;
  char str[1024];
  surface_t surface = { info.buf + 16, 256 - 16*2, 72, 256 };

  info.stale = false;

  // clear the graphics
  dmaFillHalfWords(Colors::Transparent, info.buf, info.size);

  if(selected == -1)
    return;

  stat(dirList[selected]->d_name, &statbuf);

  sprintf(str, "%s\n", dirList[selected]->d_name);
  u16* gfxPtr = icons[FIRST_FILE_ICON].sub;
  oamSet(&oamSub, 0, 14, 18, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
         gfxPtr, -1, false, false, false, false, false);
  if(!TYPE_DIR(dirList[selected]->d_type)) {
    dmaCopy(pIcons[selected]->GetData(), gfxPtr, folderBitmapLen);
    int tmpLen = strlen(str);
    g_guiManager->GetFileDescription(dirList[selected]->d_name, str + tmpLen, sizeof(str)-tmpLen);
    strncat(str, "\nSize: ", sizeof(str));
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
  else {
    dmaCopy(folderBitmap, gfxPtr, folderBitmapLen);
    if(strcmp("..", dirList[selected]->d_name) == 0)
      strcat(str, "Parent Directory");
    else
      strcat(str, "Directory");
  }

  font->PrintText(&surface, 16, 16-1, str, Colors::Black, PrintTextFlags::AtBaseline);
}

void MainApp::redrawCwd() {
  surface_t surface = { cwdstr.buf + 16, 256 - 16*2, 40, 256 };

  cwdstr.stale = false;

  getcwd(cwd, sizeof(cwd));
  dmaFillHalfWords(Colors::Transparent, cwdstr.buf, cwdstr.size);
  font->PrintText(&surface, 0, 16-1, cwd, Colors::Black, PrintTextFlags::AtBaseline);
}

// Commands
static char buf[4096];
#define YES_X (256 - 16*3)
#define YES_Y (128)
#define NO_X  (YES_X + 16)
#define NO_Y  YES_Y

void MainApp::Copy(touchPosition &touch, int down, int repeat) {
  surface_t surface = { status.buf + 16, 256 - 16*2, 48, 256, };

  // print status message
  dmaFillHalfWords(Colors::Transparent, status.buf, status.size);
  strcpy(buf, "This operation is not implemented yet.");
  font->PrintText(&surface, 0, 16-4, buf, Colors::Black, PrintTextFlags::AtBaseline);
  statusTimer = 180;
  state = STATE_PROCESS_MAIN;
}

void MainApp::Move(touchPosition &touch, int down, int repeat) {
  Copy(touch, down, repeat);
}

void MainApp::Delete(touchPosition &touch, int down, int repeat) {
  int rc;
  enum { NONE, YES, NO, } choice = NONE;
  surface_t surface = { status.buf + 16, 256 - 16*2, 48, 256, };

  // print confirmation dialog
  dmaFillHalfWords(Colors::Transparent, status.buf, status.size);
  sprintf(buf, "Delete %s?", dirList[selected]->d_name);
  font->PrintText(&surface, 0, 16-4, buf, Colors::Black, PrintTextFlags::AtBaseline);
  statusTimer = 0;

  // replace sprites with YES/NO icons
  oamSet(&oamSub, 1, YES_X, YES_Y, 0, 0, SpriteSize_16x16, SpriteColorFormat_256Color,
    icons[ICON_YES].sub, -1, false, false, false, false, false);
  oamSet(&oamSub, 2, NO_X,  NO_Y,  0, 0, SpriteSize_16x16, SpriteColorFormat_256Color,
    icons[ICON_NO].sub, -1, false, false, false, false, false);

  // check for the input
  if(down & KEY_TOUCH) {
    touchRead(&touch);
    if(touch.px > YES_X && touch.px < YES_X + 16 &&
       touch.py > YES_Y && touch.py < YES_Y + 16)
      choice = YES;
    else if(touch.px > NO_X && touch.px < NO_X + 16 &&
            touch.py > NO_Y && touch.py < NO_Y + 16)
      choice = NO;
  }
  else if(down & KEY_A)
    choice = YES;
  else if(down & KEY_B)
    choice = NO;

  if(choice == NONE)
    return;

  // clear the dialog
  dmaFillHalfWords(Colors::Transparent, status.buf, status.size);

  // delete if choice was YES
  if(choice == YES) {
    // TODO: recursive delete for directories
    rc = remove(dirList[selected]->d_name);
    if(rc == -1)
      sprintf(buf, "Failed to delete %s: %s", dirList[selected]->d_name, strerror(errno));
    else {
      sprintf(buf, "Successfully deleted %s", dirList[selected]->d_name);

      // hack to prevent another scandir!
      // free the dirent
      free(dirList[selected]);
      // slide everything after it 1 space down (if it's not the last entry)
      if(selected != numDirs-1)
        memmove(&dirList[selected], &dirList[selected+1], (numDirs-selected-1)*sizeof(struct dirent *));
      // decrement the dirlist counter
      numDirs--;
      // list needs to be updated
      list.stale = true;
      // we just deleted the selected entry!
      selected = -1;
      info.stale = true;
      oamClear(&oamSub, 0, 1);
    }

    // print status message
    font->PrintText(&surface, 0, 16-4, buf, Colors::Black, PrintTextFlags::AtBaseline);
    statusTimer = 180;
  }

  state = STATE_PROCESS_MAIN;
}

void MainApp::Rename(touchPosition &touch, int down, int repeat) {
  Copy(touch, down, repeat);
}

int main() {
  g_guiManager = GetGuiManagerChecked();

  MainApp *app = new MainApp();
  g_guiManager->RunApplication(app);

  delete app;

  return 0;
}
