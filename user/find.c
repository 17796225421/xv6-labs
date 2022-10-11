#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

int matchhere(char *, char *);
int matchstar(int, char *, char *);

int match(char *re, char *text) {
  if (re[0] == '^') return matchhere(re + 1, text);
  do {  // must look at empty string
    if (matchhere(re, text)) return 1;
  } while (*text++ != '\0');
  return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char *re, char *text) {
  if (re[0] == '\0') return 1;
  if (re[1] == '*') return matchstar(re[0], re + 2, text);
  if (re[0] == '$' && re[1] == '\0') return *text == '\0';
  if (*text != '\0' && (re[0] == '.' || re[0] == *text))
    return matchhere(re + 1, text + 1);
  return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text) {
  do {  // a * matches zero or more instances
    if (matchhere(re, text)) return 1;
  } while (*text != '\0' && (*text++ == c || c == '.'));
  return 0;
}

void find(char *path, char *target) {
  // 1. open获取文件描述符
  // 2.
  // 判断文件信息标记是目录还是文件，如果是文件，如果同名则打印并结束，否则直接结束。文件信息来自fstat函数
  // 3. 如果是目录，循环读取目录的目录项，目录项是dirent
  // 4. 当前目录项的文件名称如果是/. /..，跳过
  // 5. 将当前路径和当前目录项的文件名称组合作为新的路径。递归

  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch (st.type) {
    case T_FILE:
      if (match(target, path)) {
        printf("%s\n", path);
      }
      break;
    case T_DIR:
      // printf("%s\n", path);
      if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
        break;
      }
      strcpy(buf, path);
      p = buf + strlen(buf);
      *p++ = '/';
      while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if (stat(buf, &st) < 0) {
          printf("find: cannot stat %s\n", buf);
          continue;
        }
        //   printf("%s\n", buf);
        if (strcmp(buf + strlen(buf) - 2, "/.") != 0 &&
            strcmp(buf + strlen(buf) - 3, "/..") != 0) {
          find(buf, target);
        }
      }
      break;
  }
  close(fd);
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    exit(0);
  }
  char target[512];
  target[0] = '/';
  strcpy(target + 1, argv[2]);
  find(argv[1], target);
  exit(0);
}
