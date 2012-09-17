#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//minimum space needed to fit dirent with name and fit on 4-byte boundary
#define DENTSIZE(dent) (sizeof(struct dirent) - sizeof(dent->d_name) + ((strlen(dent->d_name) + 1 + 4) & ~3))

#define TYPE_DIR(n) (n == DT_DIR ? 1 : 0)

int scandir(const char *dir,
            struct dirent ***dirList,
            int(*filter)(const struct dirent *),
            int(*compar)(const struct dirent **, const struct dirent **)) {
  struct dirent **pList = NULL;
  struct dirent **temp  = NULL;
  struct dirent *dent;
  int numEntries = 0;

  DIR *dp = opendir(dir);
  if(dp == NULL)
    goto error;

  while((dent = readdir(dp)) != NULL) { //read all the directory entries
    if(filter == NULL  //filter out nothing
    || filter(dent)) { //filter out unwanted entries
      temp = realloc(pList, sizeof(struct dirent*)*(numEntries+1)); //increase size of list
      if(temp) {
        pList = temp;
        pList[numEntries] = malloc(DENTSIZE(dent));
        if(pList[numEntries]) {
          memcpy(pList[numEntries], dent, DENTSIZE(dent));
          numEntries++;
        } else //failed to allocate space for a directory entry
          goto error;
      } else //failed to allocate space for directory entry pointer
        goto error;
    }
  }

  //sort the list
  if(compar != NULL)
    qsort(pList, numEntries, sizeof(struct dirent*), (int (*)(const void*, const void*))compar);

  closedir(dp);
  *dirList = pList;
  return numEntries;

error:
  if(dp != NULL)
    closedir(dp);
  if(pList) { //clean up the list
    int i;
    for(i = 0; i < numEntries; i++)
      free(pList[i]);
    free(pList);
  }
  *dirList = NULL;
  return -1;
}

void freescandir(struct dirent **dirList, int numEntries) {
  int i;
  for(i = 0; i < numEntries; i++)
    free(dirList[i]);
  free(dirList);
}

int generic_scandir_filter(const struct dirent* dent) {
  return (dent->d_name[0] != '.') || (strcmp(dent->d_name, "..") == 0);
}

int generic_scandir_compar(const struct dirent **dent1, const struct dirent **dent2) {
  char isDir[2];

  // push '..' to beginning
  if(strcmp("..", (*dent1)->d_name) == 0)
    return -1;
  else if(strcmp("..", (*dent2)->d_name) == 0)
    return 1;

  isDir[0] = TYPE_DIR((*dent1)->d_type);
  isDir[1] = TYPE_DIR((*dent2)->d_type);

  if(isDir[0] == isDir[1]) // sort by name
    return stricmp((*dent1)->d_name, (*dent2)->d_name);
  else
    return isDir[1] - isDir[0]; // put directories first
}
