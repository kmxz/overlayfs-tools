#ifndef OVERLAYFS_TOOLS_SH_H
#define OVERLAYFS_TOOLS_SH_H

FILE* create_shell_script(char *tmp_path_buffer);
int quote(const char* filename, FILE* output);

#endif //OVERLAYFS_TOOLS_SH_H
