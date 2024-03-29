#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>


#include "Command.h"
#include "error.h"



typedef struct {
  char *file;
  char **argv;
  T_redir redir;
} *CommandRep;

#define BIARGS CommandRep r, int *eof, Jobs jobs
#define BINAME(name) bi_##name
#define BIDEFN(name) static void BINAME(name) (BIARGS)
#define BIENTRY(name) {#name,BINAME(name)}

static char *owd=0;
static char *cwd=0;
FILE *fptr;
int status;

int checkBG(CommandRep r);

static void builtin_args(CommandRep r, int n) {
  char **argv=r->argv;
  for (n++; *argv++; n--);
  if (n)
    ERROR("wrong number of arguments to builtin command"); // warn
}

BIDEFN(exit) {
  builtin_args(r,0);
  *eof=1;
}

BIDEFN(pwd) {
  builtin_args(r,0);
  if (!cwd)
    cwd=getcwd(0,0);
  printf("%s\n",cwd);
}

BIDEFN(history) {
  builtin_args(r,0);
  fptr = fopen(".history", "r");
  char c = fgetc(fptr);
  while (c != EOF) {
        printf ("%c", c);
        c = fgetc(fptr);
  }
  
    fclose(fptr);
}

BIDEFN(cd) {
  builtin_args(r,1);
  if (strcmp(r->argv[1],"-")==0) {
    char *twd=cwd;
    cwd=owd;
    owd=twd;
  } else {
    if (owd) free(owd);
    owd=cwd;
    cwd=strdup(r->argv[1]);
  }
  if (cwd && chdir(cwd))
    ERROR("chdir() failed"); // warn
}

static int builtin(BIARGS) {
  typedef struct {
    char *s;
    void (*f)(BIARGS);
  } Builtin;
  static const Builtin builtins[]={
    BIENTRY(exit),
    BIENTRY(pwd),
    BIENTRY(cd),
    BIENTRY(history),
    {0,0}
  };
  int i;
  for (i=0; builtins[i].s; i++)
    if (!strcmp(r->file,builtins[i].s)) {
      builtins[i].f(r,eof,jobs);
      return 1;
    }
  return 0;
}

static char **getargs(T_words words) {
  int n=0;
  T_words p=words;
  while (p) {
    p=p->words;
    n++;
  }
  char **argv=(char **)malloc(sizeof(char *)*(n+1));
  if (!argv)
    ERROR("malloc() failed");
  p=words;
  int i=0;
  while (p) {
    argv[i++]=strdup(p->word->s);
    p=p->words;
  }
  argv[i]=0;
  return argv;
}

extern Command newCommand(T_words words, T_redir redir) {
  CommandRep r=(CommandRep)malloc(sizeof(*r));
  if (!r)
    ERROR("malloc() failed");
  r->argv=getargs(words);
  r->file=r->argv[0];
  r->redir=redir;
  return r;
}

static void child(CommandRep r, int fg) {
  int eof=0;
  Jobs jobs=newJobs();
  if (builtin(r,&eof,jobs))
    return;
  execvp(r->argv[0],r->argv);
  ERROR("execvp() failed");
  exit(0);
}

extern void execCommand(Command command, Pipeline pipeline, Jobs jobs,
			int *jobbed, int *eof, int fg) {
  CommandRep r=command;

  T_redir dir = r->redir;
  int save =  dup(STDOUT_FILENO);
  int input =  dup(STDIN_FILENO);
  int fd0 = 0;
  int fd1 = 0;
  char *io = NULL;
  char *out_file= NULL;
  char *in_file= NULL;

   //if < word
  if(dir->in){
    close(STDIN_FILENO);
      in_file = dir->word->s;
      fd0 = open(in_file, O_RDONLY);
      if(fd0<0)
        exit(1);
      //if < word > word
      if(dir->out){
        close(fd0);
        dup2(input,STDIN_FILENO);
        close(input);
        out_file = strdup(dir->word1->s);
        fd1 = open(out_file, O_CREAT | O_WRONLY | O_TRUNC, 0777);
        if(fd1<0)
           exit(1);
        dup(fd1);
      }
  } //if > word
  else if(dir->out){
      close(STDOUT_FILENO);
      out_file = strdup(dir->word->s);
      fd1 = open(out_file, O_CREAT | O_WRONLY | O_TRUNC, 0777);
      if(fd1<0)
        exit(1);
      dup(fd1);
  }

    if (fg && builtin(r,eof,jobs)){
      if(dir->in){
        close(fd0);
        dup2(input,STDIN_FILENO);
        close(input);
      }

      if(dir->out){
        close(fd1);
        dup2(save,STDOUT_FILENO);
        close(save);
      }

      fflush(stdout);
      fflush(stdin);
      free(io);
      free(out_file);
      free(in_file);
      return;
    }

    if (!*jobbed) {
      *jobbed=1;
      addJobs(jobs,pipeline);
    }

    int pid=fork();
    if (pid==-1)
      ERROR("fork() failed");
      
    if (pid==0){
      child(r,fg);
      return;
    } 
    else wait(NULL);

  dup2(input,STDIN_FILENO);
  close(input);
  dup2(save,STDOUT_FILENO);
  close(save);
}

extern void freeCommand(Command command) {
  CommandRep r=command;
  char **argv=r->argv;
  while (*argv)
    free(*argv++);
  free(r->argv);
  free(r);
}

extern void freestateCommand() {
  if (cwd) free(cwd);
  if (owd) free(owd);
}

extern int checkBG(CommandRep r){
  int fg = 0;
  int i = 0;
  while(r->argv[i] != NULL){
    if(!strcmp(r->argv[i], "&")){
      fg = 1;
      r->argv[i] = NULL;
    }
    i++;
  }
  return fg;
}