#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    // 管道AB，父用管道A传给子，子用管道B传给父
    int pipeA[2],pipeB[2];
    pipe(pipeA);
    pipe(pipeB);
    
    if(fork()!=0){
        // 父只需要A管道输出，B管道输入
        close(pipeA[0]);
        close(pipeB[1]);

        write(pipeA[1],"0",1);
        char buf[1];
        read(pipeB[0],buf,1);
        printf("%d: received pong\n",getpid());

        close(pipeA[1]);
        close(pipeB[0]);
    }else {
        // 子只需要A管道输入，B管道输出
        close(pipeA[1]);
        close(pipeB[0]);

        char buf[1];
        read(pipeA[0],buf,1);
        printf("%d: received ping\n",getpid());
        write(pipeB[1],"0",1);
        
        close(pipeA[0]);
        close(pipeB[1]);
    }
    exit(0);
}