#pragma once
#include "stdint.h"


void make_clear_abs_path(char *path, char *final_path);
void in_pwd(uint32_t argc, char **argv);
char *in_cd(uint32_t argc, char **argv);
void in_ls(uint32_t argc, char **argv);
void in_ps(uint32_t argc, char **argv);
void in_clear(uint32_t argc, char **argv);
int32_t in_mkdir(uint32_t argc, char **argv);
int32_t in_rmdir(uint32_t argc, char **argv);
int32_t in_mkfile(uint32_t argc, char **argv);
int32_t in_rm(uint32_t argc, char **argv);