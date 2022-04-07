#include "kernel/types.h"
#include "user/user.h"

#define MAXLINE 1024

int main(int argc, char *argv[])
{
    char extraParams[MAXLINE];
    char *newParams[32];
    int n, newParamsIdx = 0;

    char *newCmd = argv[1];
    for (int i = 1; i < argc; i++)
        newParams[newParamsIdx++] = argv[i];

    while ((n = read(0, extraParams, MAXLINE)) > 0)
    {
        if (fork() == 0)
        {
            char *arg = (char *)malloc(sizeof(extraParams));
            int index = 0;
            for (int i = 0; i < n; i++)
            {
                if (extraParams[i] == ' ' || extraParams[i] == '\n')
                {
                    arg[index] = '\0';
                    newParams[newParamsIdx++] = arg;
                    index = 0;
                    arg = (char *)malloc(sizeof(extraParams));
                }
                else
                    arg[index++] = extraParams[i];
            }
            arg[index] = 0;
            newParams[newParamsIdx] = 0;
            exec(newCmd, newParams);
        }
        else
            wait(0);
    }
    exit(0);
}
