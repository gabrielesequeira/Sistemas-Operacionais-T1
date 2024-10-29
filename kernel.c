#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <time.h>

#define numProcessos 5
#define maximo 10

pid_t processos[numProcessos];
int *pc;                   
int *bloqueado;             
int *finalizado;             
int *bloqueado_dispositivo;    
int atual = -1;
int fd[2];                 
int fila_dispositivos[2][numProcessos];
int time_slice = 3;        

void tratar_time_slice(int sig);
void tratar_interrupcao(int sig);
void tratar_sigstp(int sig); 
void inicia_processos();
void inicia_kernel();
void inter_controller_sim();
void memoria_compartilhada();
void print_info();

void memoria_compartilhada() {
    int shmid_pc = shmget(IPC_PRIVATE, numProcessos * sizeof(int), IPC_CREAT | 0666);
    pc = shmat(shmid_pc, NULL, 0);

    int shmid_bloqueado = shmget(IPC_PRIVATE, numProcessos * sizeof(int), IPC_CREAT | 0666);
    bloqueado = shmat(shmid_bloqueado, NULL, 0);

    int shmid_finalizado = shmget(IPC_PRIVATE, numProcessos * sizeof(int), IPC_CREAT | 0666);
    finalizado = shmat(shmid_finalizado, NULL, 0);

    int shmid_bloqueado_dispositivo = shmget(IPC_PRIVATE, numProcessos * sizeof(int), IPC_CREAT | 0666);
    bloqueado_dispositivo = shmat(shmid_bloqueado_dispositivo, NULL, 0);

    for (int i = 0; i < numProcessos; i++) {
        pc[i] = 0;
        bloqueado[i] = 0;
        finalizado[i] = 0;
        bloqueado_dispositivo[i] = 0;
    }
}

void print_info() {
    printf("\nEstado dos processos:\n");
    for (int i = 0; i < numProcessos; i++) {
        printf("Processo %d:\n", i + 1);
        printf("  PC: %d\n", pc[i]);

        if (finalizado[i]) {
            printf("  Estado: Terminado\n");
        } else if (bloqueado[i]) {
            printf("  Estado: Bloqueado no dispositivo D%d\n", bloqueado_dispositivo[i]);
        } else if (i == atual) {
            printf("  Estado: Executando\n");
        } else {
            printf("  Estado: Pronto para execução\n");
        }
        printf("\n");
    }
}

void tratar_time_slice(int sig) {
    if (atual != -1 && !finalizado[atual]) {
        kill(processos[atual], SIGSTOP);
        printf("Processo %d interrompido.\n", atual + 1);
    }

    do {
        atual = (atual + 1) % numProcessos;
    } while (bloqueado[atual] || finalizado[atual]);

    if (!finalizado[atual]) {
        kill(processos[atual], SIGCONT);
        printf("Processo %d retomado.\n", atual + 1);
    }

    alarm(time_slice);
}

void tratar_interrupcao(int sig) {
    int dispositivo = (sig == SIGUSR1) ? 1 : 2;  
    printf("Gerando interrupção para D%d (SIGUSR%d)\n", dispositivo, sig == SIGUSR1 ? 1 : 2);

    for (int i = 0; i < numProcessos; i++) {
        if (bloqueado[i] && bloqueado_dispositivo[i] == dispositivo) {
            bloqueado[i] = 0;  
            bloqueado_dispositivo[i] = 0;
            printf("Processo %d liberado do dispositivo D%d.\n", i + 1, dispositivo);
            break;  
        }
    }
}

void tratar_sigstp(int sig) {
    printf("\n  Sinal SIGTSTP recebido (CTRL + Z):\n");


    for (int i = 0; i < numProcessos; i++) {
        if (!finalizado[i]) {
            kill(processos[i], SIGSTOP);
        }
    }


    print_info();

    printf("\nExecução finalizada após SIGTSTP.\n");
    exit(0);  
}

void inicia_processos() {
    for (int i = 0; i < numProcessos; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            while (pc[i] < maximo) {
                kill(getpid(), SIGSTOP); 

                while (1) {
                    if (pc[i] >= maximo) break;  
                    pc[i]++;
                    printf("Processo %d executando, PC: %d\n", i + 1, pc[i]);
                    sleep(1); 

                    if (rand() % 100 < 30) {
                        int dispositivo = rand() % 2 + 1; 
                        printf("Processo %d solicitou I/O no dispositivo D%d.\n", i + 1, dispositivo);
                        bloqueado[i] = 1;
                        bloqueado_dispositivo[i] = dispositivo;
                        printf("Processo %d foi bloqueado no dispositivo D%d.\n", i + 1, dispositivo);

                        kill(getppid(), SIGALRM);  
                        kill(getpid(), SIGSTOP);    
                    }
                }
            }
            printf("Processo %d terminou.\n", i + 1);
            finalizado[i] = 1;
            exit(0);
        } else {
            processos[i] = pid;
            pc[i] = 0;
        }
    }
}

void inicia_kernel() {
    signal(SIGALRM, tratar_time_slice); 
    signal(SIGTSTP, tratar_sigstp);     
    signal(SIGUSR1, tratar_interrupcao); 
    signal(SIGUSR2, tratar_interrupcao); 

    alarm(time_slice);  

    atual = 0;
    kill(processos[atual], SIGCONT);
    printf("Processo %d iniciado.\n", atual + 1);

    while (1) {
        pause(); 
    }
}

void inter_controller_sim() {
    while (1) {
        sleep(2);  
        if (rand() % 2 == 0) {
            kill(getppid(), SIGUSR1);  
        } else {
            kill(getppid(), SIGUSR2); 
        }
    }
}

int main() {
    printf("Iniciando KernelSim...\n");

    memoria_compartilhada(); 

    if (pipe(fd) < 0) {
        perror("Erro ao criar o pipe");
        exit(1);
    }

    if (fork() == 0) {
        inter_controller_sim();
        exit(0);
    }

    inicia_processos();
    inicia_kernel();

    for (int i = 0; i < numProcessos; i++) {
        wait(NULL);
        finalizado[i] = 1;
    }

    return 0;
}
