#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
/*
每个进程通过上一个管道从上一个进程拿一堆数字，只打印第一个数字，
把剩下的数字，如果不是第一个数字的倍数，就通过当前管道传给下一个进程。
*/

void sieve(int prePipe[2])
{
	int firstNum;
	read(prePipe[0], &firstNum, sizeof(firstNum));
	if (firstNum == -1)
	{
		exit(0);
	}
	printf("prime %d\n", firstNum);

	int curPipe[2];
	pipe(curPipe);

	if (fork() == 0)
	{
		close(curPipe[1]);
		close(prePipe[0]);
		sieve(curPipe);
	}
	else
	{
		close(curPipe[0]);
		int buf;
		while (read(prePipe[0], &buf, sizeof(buf)) && buf != -1)
		{
			if (buf % firstNum != 0)
			{
				write(curPipe[1], &buf, sizeof(buf));
			}
		}
		buf = -1;
		write(curPipe[1], &buf, sizeof(buf));
		wait(0);
		exit(0);
	}
}

int main(int argc, char **argv)
{
	int curPipe[2];
	pipe(curPipe);

	if (fork() == 0)
	{
		close(curPipe[1]);
		sieve(curPipe);
		exit(0);
	}
	else
	{
		close(curPipe[0]);
		for (int i = 2; i <= 35; i++)
		{
			write(curPipe[1], &i, sizeof(i));
		}
		int end = -1;
		write(curPipe[1], &end, sizeof(end)); // 末尾输入 -1，用于标识输入完成
	}
	wait(0);
	exit(0);
}