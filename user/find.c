#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *begin)
{
  static char buf[DIRSIZ + 1];
  char *p;

  for (p = begin + strlen(begin); p >= begin && *p != '/'; p--)
    ;
  p++;

  if (strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
  return buf;
}

void find(char *begin, char *target)
{
  // 对于当前目录的所有内容，如果是文件，判断，如果是目录，递归find

  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(begin, 0)) < 0)
  {
    fprintf(2, "find: cannot open %s\n", begin);
    return;
  }

  if (fstat(fd, &st) < 0)
  {
    fprintf(2, "find: cannot stat %s\n", begin);
    close(fd);
    return;
  }

  switch (st.type)
  {
  case T_FILE:
    // 取出begin文件的文件名，进行比较。
    // find ./b c 取出文件名b
    if (strcmp(fmtname(begin), target) == 0)
    {
      printf("%s\n", fmtname(begin));
    }
    break;

  case T_DIR:
    // 取出目录中所有文件或目录，递归find
    // find ./b c 对于目录b的所有文件或目录，作为新的begin递归find
    if (strlen(begin) + 1 + DIRSIZ + 1 > sizeof buf)
    {
      printf("find: begin too long\n");
      break;
    }
    strcpy(buf, begin);
    p = buf + strlen(buf);
    *p++ = '/';
    while (read(fd, &de, sizeof(de)) == sizeof(de))
    {
      if (de.inum == 0)
        continue;
      if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if (stat(buf, &st) < 0)
      {
        printf("find: cannot stat %s\n", buf);
        continue;
      }
      switch (st.type)
      {
      case T_FILE:
        if (strcmp(de.name, target) == 0)
        {
          printf("%s\n", buf);
        }
        break;

      case T_DIR:
        find(buf, target);
        break;
      }
    }
    break;
  }
  close(fd);
}

int main(int argc, char *argv[])
{
  if (argc != 3)
  {
    printf("Usage: find [begin] [target]");
    exit(0);
  }

  for (int i = 1; i < argc - 1; i++)
    find(argv[i], argv[argc - 1]);
  exit(0);
}