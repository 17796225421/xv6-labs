#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int pipeFds[2];
    pipe(pipeFds);
    char buf[1];
    if(fork()==0){
        // 子进程read pipe阻塞，write pipe
        read(pipeFds[0],buf,sizeof(buf));
        printf("%d: received ping\n",getpid());
        write(pipeFds[1],buf,sizeof(buf));
    }else {
        // 父进程write pipe，等1ms让子进程read pipe，父进程read pipe阻塞
        write(pipeFds[1],"0",1);
        sleep(1);// TODO 使用条件变量让子进程read pipe在父进程read pipe之前。
        read(pipeFds[0],buf,sizeof(buf));
        printf("%d: received pong\n",getpid());
        close(pipeFds[0]);
        close(pipeFds[1]);
    }
    exit(0);
}