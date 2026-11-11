#ifndef KILONS_USER_ENV_H
#define KILONS_USER_ENV_H
extern char **environ;
char *getenv(const char *name);
int setenv(const char *name,const char *value,int overwrite);
int unsetenv(const char *name);
#endif
