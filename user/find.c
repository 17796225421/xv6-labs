#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"
/*
找到所有targetFileName

对于当前path，如果是文件，pathToFileName后和targetFileName比较。
如果是目录，对于每一个pathDeeper，都调用find。
*/
char *pathToFileName(char *path)
{
	static char fileName[DIRSIZ + 1];
	char *p = path + strlen(path);
	while (p >= path && *p != '/')
		p--;
	p++;
	memmove(fileName, p, strlen(p) + 1);
	return fileName;
}

void find(char *path, char *targetFileName)
{
	int fd;
	struct stat st;
	if ((fd = open(path, O_RDONLY)) < 0)
	{
		fprintf(2, "find: cannot open %s\n", path);
		return;
	}
	if (fstat(fd, &st) < 0)
	{
		fprintf(2, "find: cannot stat %s\n", path);
		close(fd);
		return;
	}
	char pathDeeper[512], *p;
	struct dirent de;
	switch (st.type)
	{
	case T_FILE:
		if (strcmp(pathToFileName(path), targetFileName) == 0)
		{
			printf("%s\n", path);
		}
		break;
	case T_DIR:
		if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(pathDeeper))
		{
			printf("find: path too long\n");
			break;
		}
		strcpy(pathDeeper, path);
		p = pathDeeper + strlen(pathDeeper);
		*p++ = '/';
		while (read(fd, &de, sizeof(de)) == sizeof(de))
		{
			if (de.inum == 0 || de.inum == 1 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
				continue;
			memmove(p, de.name, strlen(de.name));
			p[strlen(de.name)] = '\0';
			find(pathDeeper, targetFileName);
		}
		break;
	}
	close(fd);
}

int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		printf("Usage: find <path> <path>\n");
		exit(0);
	}
	find(argv[1], argv[2]);
	exit(0);
}
