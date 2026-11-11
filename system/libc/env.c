#include "unistd.h"
#include "env.h"
/* envp strings supplied at exec remain valid on the initial user stack.
 * Keep them by reference until a caller replaces the corresponding entry. */
#define SLOTS 16
static char *table[SLOTS+1];
static char owned[SLOTS][192];
char **environ=table;
static unsigned key_size(const char *name){
    unsigned n=0;
    if(name)while(name[n] && name[n]!='=')n++;
    return name && !name[n]?n:0;
}
static int matches(const char *entry,const char *key,unsigned n){
    unsigned i=0;
    while(i<n && entry[i]==key[i])i++;
    return i==n && entry[i]=='=';
}
char *getenv(const char *key){
    unsigned n=key_size(key);
    if(n && environ)for(unsigned i=0;environ[i];i++)
        if(matches(environ[i],key,n))return environ[i]+n+1;
    return 0;
}
static int import(void){
    if(environ==table)return 0;
    unsigned i=0;
    while(environ && environ[i]){
        if(i==SLOTS){errno=12;return -1;}
        table[i]=environ[i];i++;
    }
    table[i]=0;environ=table;return 0;
}
int setenv(const char *key,const char *value,int overwrite){
    unsigned n=key_size(key),i=0,v=0;
    if(!n||!value){errno=22;return -1;}
    if(import()<0)return -1;
    while(table[i] && !matches(table[i],key,n))i++;
    if(table[i] && !overwrite)return 0;
    while(value[v])v++;
    if(i==SLOTS || n+v+2>sizeof owned[0]){errno=12;return -1;}
    char tmp[sizeof owned[0]];
    for(unsigned j=0;j<n;j++)tmp[j]=key[j];
    tmp[n]='=';
    for(unsigned j=0;j<=v;j++)tmp[n+1+j]=value[j];
    for(unsigned j=0;j<n+v+2;j++)owned[i][j]=tmp[j];
    table[i]=owned[i];return 0;
}
int unsetenv(const char *key){
    unsigned n=key_size(key);
    if(!n){errno=22;return -1;}
    if(import()<0)return -1;
    unsigned dst=0;
    for(unsigned i=0;table[i];i++){
        if(matches(table[i],key,n))continue;
        if(i!=dst && table[i]==owned[i]){
            unsigned j=0;
            do {owned[dst][j]=owned[i][j];}while(owned[i][j++]);
            table[dst]=owned[dst];
        }else table[dst]=table[i];
        dst++;
    }
    table[dst]=0;return 0;
}
