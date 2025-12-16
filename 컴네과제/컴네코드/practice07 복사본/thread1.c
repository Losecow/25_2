#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

int sum;         
void *runner(void *param); 	

int main(int argc, char *argv[])
{
    pthread_t tid; 

    // create the thread
    pthread_create(&tid, NULL, runner, argv[1]);

    // now wait for the thread to exit
    pthread_join(tid, NULL);

    printf("sum = %d\n",sum);

    return 0;
}

// thread will execute in this function 
void *runner(void *param) 
{
    int i, upper = atoi(param);
    sum = 0;

    for (i = 1; i <= upper; i++)
        sum += i;
    
    pthread_exit(0); // or return NULL
}
