#pragma once

#define TYPE_DIR(n) (n == DT_DIR ? 1 : 0)

#ifdef __cplusplus
extern "C" {
#endif

int scandir(const char *dir,
            struct dirent ***dirList,
            int(*filter)(const struct dirent *),
            int(*compar)(const struct dirent **, const struct dirent **));
void freescandir(struct dirent **dirList, int numEntries);

int generic_scandir_filter(const struct dirent* dent);
int generic_scandir_compar(const struct dirent **dent1, const struct dirent **dent2);

#ifdef __cplusplus
}
#endif
