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
#include <sys/stat.h>

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

  // initialize video
  lcdMainOnBottom();
  videoSetMode   (MODE_0_3D);
  videoSetModeSub(MODE_3_2D);
  vramSetPrimaryBanks(VRAM_A_TEXTURE, VRAM_B_MAIN_SPRITE, VRAM_C_SUB_BG, VRAM_D_SUB_SPRITE);

  bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);

  // initialize OAM
  oamInit(&oamMain, SpriteMapping_Bmp_1D_128, false);
  oamInit(&oamSub,  SpriteMapping_Bmp_1D_128, false);

  // allocate sprites for icons
  if(fileIcon.main   == NULL) fileIcon.main   = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(fileIcon.sub    == NULL) fileIcon.sub    = oamAllocateGfx(&oamSub,  SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(folderIcon.main == NULL) folderIcon.main = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(folderIcon.sub  == NULL) folderIcon.sub  = oamAllocateGfx(&oamSub,  SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(fx2Icon.main    == NULL) fx2Icon.main    = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_Bmp);
  if(fx2Icon.sub     == NULL) fx2Icon.sub     = oamAllocateGfx(&oamSub,  SpriteSize_16x16, SpriteColorFormat_Bmp);

  // copy sprites into vram
  DC_FlushAll();
  dmaCopy(fileBitmap,   fileIcon.main,   fileBitmapLen);
  dmaCopy(fileBitmap,   fileIcon.sub,    fileBitmapLen);
  dmaCopy(folderBitmap, folderIcon.main, folderBitmapLen);
  dmaCopy(folderBitmap, folderIcon.sub,  folderBitmapLen);
  dmaCopy(fx2Bitmap,    fx2Icon.main,    fx2BitmapLen);
  dmaCopy(fx2Bitmap,    fx2Icon.sub,     fx2BitmapLen);

  // initialize 3D engine
  glInit();
  glEnable(GL_TEXTURE_2D|GL_BLEND);
  glClearColor(31, 31, 31, 31);
  glClearPolyID(63);
  glClearDepth(GL_MAX_DEPTH);

  // set up the view
  glViewport(0, 0, 255, 191);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrthof32(0, 256, 192, 0, -1<<12, 1<<12);
  glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  // initialize the materials
  glColor(RGB15(31, 31, 31));
  glMaterialf(GL_AMBIENT,  RGB15(31, 31, 31));
  glMaterialf(GL_DIFFUSE,  RGB15(31, 31, 31));
  glMaterialf(GL_EMISSION, RGB15(31, 31, 31));
  glMaterialf(GL_SPECULAR, ARGB16(1, 31, 31, 31));
  glMaterialShinyness();

  // allocate textures
  for(u32 i = 0; i < sizeof(entries)/sizeof(entries[0]); i++) {
    glGenTextures(1, &entries[i].texture);
    glBindTexture(GL_TEXTURE_2D, entries[i].texture);
    glTexImage2D(0, 0, GL_RGBA, TEXTURE_SIZE_256, TEXTURE_SIZE_16, 0, TEXGEN_TEXCOORD, NULL);
    entries[i].entry = -1;
    entries[i].selected = false;
    entries[i].oldSelected = false;
  }

  // reinitialize directory listing
  if(dirList != NULL)
    freescandir(dirList, numDirs);
  getcwd(cwd, sizeof(cwd));
  dmaFillHalfWords(Colors::White, bgGetGfxPtr(7), 256*256*sizeof(u16));
  font->PrintText(bgGetGfxPtr(7), 0, 16-4, cwd, Colors::Black, PrintTextFlags::AtBaseline);

  numDirs = scandir(".", &dirList, generic_scandir_filter, generic_scandir_compar);
  selected = -1;
  scroll   = 0;

  keysSetRepeat(15, 4);
}

void MainApp::OnDeactivate() {
  // deallocate textures
  for(int i = 0; i < NUM_ENTRIES; i++)
    glDeleteTextures(1, &entries[i].texture);

  // uninitialize 3D engine
  glDeinit();
}

void MainApp::OnVBlank() {
  touchPosition touch;
  char          directory[256];
  int           selection = -1;
  word_t        down      = keysDown();
  word_t        repeat    = keysDownRepeat();

  // draw the scene ASAP
  redraw();

  if(down & KEY_TOUCH) {
    touchRead(&touch);

    if(dirList != NULL) {
      if((touch.py-8)/16 + scroll < numDirs
      && (touch.py-8)/16 >= 0
      && (touch.py-8)/16 < (192-8)/16) {
        // determine what was touched
        selection = (touch.py-8)/16 + scroll;

        // update the selection
        if(selection != selected) {
          u16 *ptr = &(bgGetGfxPtr(7)[256*16]);
          struct stat statbuf;
          char str[1024];
          surface_t surface = { ptr + 24, 256 - 24, 256-16, 256, };

          selected = selection;
          stat(dirList[selected]->d_name, &statbuf);

          dmaFillHalfWords(Colors::White, ptr, 256*(256-16)*sizeof(u16));
          sprintf(str, "%s\n", dirList[selected]->d_name);
          if(!TYPE_DIR(dirList[selected]->d_type)) {
            sprintf(str+strlen(str), "Size: ");
            if(statbuf.st_size < 1000)
              sprintf(str+strlen(str), "%u\n", statbuf.st_size);
            else if(statbuf.st_size < 10240)
              sprintf(str+strlen(str), "%u.%02uKB\n", statbuf.st_size/1024,
                                   (statbuf.st_size%1024)*100/1024);
            else if(statbuf.st_size < 102400)
              sprintf(str+strlen(str), "%u.%01uKB\n", statbuf.st_size/1024,
                                   (statbuf.st_size%1024)*10/1024);
            else if(statbuf.st_size < 1000000)
              sprintf(str+strlen(str), "%uKB\n", statbuf.st_size/1024);
            else if(statbuf.st_size < 10485760)
              sprintf(str+strlen(str), "%u.%02uMB\n", statbuf.st_size/1048576,
                                   (statbuf.st_size%1048576)*100/1048576);
            else if(statbuf.st_size < 104857600)
              sprintf(str+strlen(str), "%u.%01uMB\n", statbuf.st_size/1048576,
                                   (statbuf.st_size%1048576)*10/1048576);
            else
              sprintf(str+strlen(str), "%uMB\n", statbuf.st_size/1048576);
          }

          font->PrintText(&surface, 0, 16-4, str, Colors::Blue, PrintTextFlags::AtBaseline);
          // refresh the 'selected' status for each texture
          for(int i = 0; i < NUM_ENTRIES; i++)
            entries[i].selected = entries[i].entry == selected;
        }
        else { // we have selected a selected direntry
          // open a directory
          if(TYPE_DIR(dirList[selected]->d_type)) {
            strcpy(directory, dirList[selected]->d_name);
            freescandir(dirList, numDirs);

            // move to the new directory
            chdir(directory);
            getcwd(cwd, sizeof(cwd));
            dmaFillHalfWords(Colors::White, bgGetGfxPtr(7), 256*256*sizeof(u16));
            font->PrintText(bgGetGfxPtr(7), 0, 16-4, cwd, Colors::Black, PrintTextFlags::AtBaseline);

            // scan the new directory
            numDirs = scandir(".", &dirList, generic_scandir_filter, generic_scandir_compar);

            // reinitialize the texture entries
            for(int i = 0; i < NUM_ENTRIES; i++) {
              entries[i].entry = -1;
              entries[i].selected = false;
              entries[i].oldSelected = false;
            }

            // reset the selected direntry and scroll
            selected = -1;
            scroll = 0;
          }
        }
      }
    }
  }

  // update scroll
  if((repeat & KEY_DOWN) && scroll < numDirs - (192-8)/16) {
    scroll++;
  }
  else if((repeat & KEY_UP) && scroll > 0) {
    scroll--;
  }

  // check for exit
  if(down & KEY_B) {
    Close();
    return;
  }
}

void MainApp::redraw() {
  u32 offset;
  int tex;
  surface_t surface = { NULL, 256, 16, 256, };

  // clear all the sprites
  oamClear(&oamMain, 0, 0);
  oamClear(&oamSub,  0, 0);

  // update the textures
  vramSetBankA(VRAM_A_LCD); // can only write to texture memory in LCD mode!
  for(int i = 0; i < numDirs && i < NUM_ENTRIES-2; i++) {
    // figure out which texture entry to use
    offset = (scroll + i) % NUM_ENTRIES;
    tex = entries[offset].texture;

    // this texture does not belong to this direntry or has just been selected/deselected
    if(entries[offset].entry != scroll+i || entries[offset].selected != entries[offset].oldSelected) {
      // update the direntry number
      entries[offset].entry = scroll+i;

      // clear the texture
      dmaFillWords(0, glGetTexturePointer(tex), 256*16*sizeof(u16));

      // draw blue if selected, black if not selected
      surface.buffer = (color_t*)glGetTexturePointer(tex);
      if(scroll+i == selected)
        font->PrintText(&surface, 24, 16-4, dirList[scroll+i]->d_name, Colors::Blue, PrintTextFlags::AtBaseline);
      else
        font->PrintText(&surface, 24, 16-4, dirList[scroll+i]->d_name, Colors::Black, PrintTextFlags::AtBaseline);
    }
    // update the "previous" selection state
    entries[offset].oldSelected = entries[offset].selected;
  }
  vramSetBankA(VRAM_A_TEXTURE); // switch back to textured mode

  // draw the graphics hooray!
  glBegin(GL_QUADS);
  for(int i = 0; i < numDirs && i < NUM_ENTRIES-2; i++) {
    // this is a directory, give it a folder sprite!
    if(TYPE_DIR(dirList[scroll+i]->d_type)) {
      oamSet(&oamMain, i, 4, 8+16*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             folderIcon.main, -1, false, false, false, false, false);
    }
    // this is an fx2, give it an executable sprite!
    else if(strcmp(".fx2", dirList[scroll+i]->d_name + strlen(dirList[scroll+i]->d_name)-4) == 0) {
      oamSet(&oamMain, i, 4, 8+16*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             fx2Icon.main, -1, false, false, false, false, false);
    }
    // this is something else, give it a file sprite!
    else {
      oamSet(&oamMain, i, 4, 8+16*i, 0, 15, SpriteSize_16x16, SpriteColorFormat_Bmp,
             fileIcon.main, -1, false, false, false, false, false);
    }

    // figure out which texture entry to use
    offset = (scroll + i) % NUM_ENTRIES;
    glBindTexture(GL_TEXTURE_2D, entries[offset].texture);

    // draw a textured quad
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

  // flush the 3D fifo
  glFlush(0);
}

int main() {
  MainApp app;

  g_guiManager = GetGuiManagerChecked();
  g_guiManager->RunApplication(&app);

  return 0;
}
