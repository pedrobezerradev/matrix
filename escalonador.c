//Pedro Bezerra
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// bibliotecas para o bonus 1
#include <syslog.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DAEMON 1 // define se o programa se iniciara em plano de fundo
#define WAITTOBEKILLED 0 // se esta opção estiver ativa o daemon vai ficar recebendo sinais depois do final da execução do programa até ser morto
#define SYSLOG 1 // define se o programa fara log de sistema ou em arquivo
#define FREE_MEM 0 // 0 para nao liberar a memoria em tempo real (em alguns testes deu core dump)
#define DEBUGLOG 1 // 0 para nao fazer debug
#define BUFF_SIZE 5	/* total number of slots */
#define SLEEP_BURST 1 // tempo do ciclo de sleep para espera do fim das threads
#define LOG_FILE "local.log" // define arquivo de log local

#define LC_N 1
#define LA_N 3
#define MM_N 4
#define DM_N 5
#define EA_N 3

typedef struct {
    char Nome[100];
    int Ordem;
    double **A,**B,**C, E;
} sbuf_matriz;

typedef struct {
    sbuf_matriz buf[BUFF_SIZE];   /* shared var */
    int in;         	  /* buf[in%BUFF_SIZE] is the first empty slot */
    int out;        	  /* buf[out%BUFF_SIZE] is the first full slot */
    sem_t full;     	  /* keep track of the number of full spots */
    sem_t empty;    	  /* keep track of the number of empty spots */
    sem_t mutex;    	  /* enforce mutual exclusion to shared data */
} sbuf_t;

typedef struct {
    int aL;         	  /* numero de arquivos lidos */
    int lC;         	  /* parametro de controle de leitura */
    sem_t mutex;    	  /* enforce mutual exclusion to shared data */
} sbuf_c;

typedef struct {
    sem_t mutex;    	  /* enforce mutual exclusion to shared data */
} sbuf_l;


sbuf_t shared[4];
sbuf_c m_control[5];
sbuf_l log_control, kill_control, term_control;
int term_hdl = 0, kill_hdl = 0;

void sendLog(char *msg){
    FILE *log;
    log=fopen(LOG_FILE,"ab+");
    if(!log) return;
    fprintf(log,"%s\n",msg);
    fclose(log);
}

void *LC_thread(void *arg) {
    int nit = 0;
    FILE *C;
    sbuf_matriz item;
    char str[100];

    C = fopen("entrada.in", "r");
    if (C == NULL) {
        sprintf(str,"Problemas ao tentar abrir o arquivo de catalogo.");
        #if SYSLOG
            syslog (LOG_NOTICE, str);
        #else
            #if DAEMON
                sem_wait(&log_control.mutex);
                    sendLog(str);
                sem_post(&log_control.mutex);
            #else
                printf(str);
            #endif
        #endif
        return NULL;
    } else {
        rewind(C);
        while (fscanf (C, "%s", item.Nome) != EOF) {
            // espera se nao houver slots vazios
            sem_wait(&shared[0].empty);
            // espera a liberacao do buffer
            sem_wait(&shared[0].mutex);

            //coloca um item no buffer
            shared[0].buf[shared[0].in] = item;
            shared[0].in = (shared[0].in+1)%BUFF_SIZE;

            sem_wait(&m_control[0].mutex);
            m_control[0].aL++;
            sem_post(&m_control[0].mutex);

            // libera o buffer
            sem_post(&shared[0].mutex);
            // aumenta o numero de slots disponiveis
            sem_post(&shared[0].full);

            #if DEBUGLOG
                sprintf(str,"[LC]: O arquivo '%s' foi lido do catalogo.\n", item.Nome);
                #if SYSLOG
                syslog (LOG_NOTICE, str);
                #else
                    #if DAEMON
                        sem_wait(&log_control.mutex);
                            sendLog(str);
                        sem_post(&log_control.mutex);
                    #else
                        printf(str);
                    #endif
                #endif
            #endif
            #if DAEMON
            // recebimento de SIGTERM - o programa não monitorará novos arquivos e terminará somente os que ja foram adicionados a memória.
                sem_wait(&term_control.mutex);
                    if (term_hdl) {
                        sem_post(&term_control.mutex);
                        break;
                    }
                sem_post(&term_control.mutex);
            // recebimento de SIGKILL - o programa não monitorará novos arquivos e fechara somente os que esta escrevendo para não corromper os arquivos
                sem_wait(&kill_control.mutex);
                    if (kill_hdl) {
                        sem_post(&kill_control.mutex);
                        break;
                    }
                sem_post(&kill_control.mutex);
            #endif
        }
        fclose(C);
    }
        //posta o termino das atividades de LC para a proxima thread
        sem_wait(&m_control[0].mutex);
            m_control[0].lC = 0;
        sem_post(&m_control[0].mutex);

        sprintf(str,"*[LC]*- THREAD LC have finished successfully.\n");
        #if SYSLOG
            syslog (LOG_NOTICE, str);
        #else
            #if DAEMON
                sem_wait(&log_control.mutex);
                    sendLog(str);
                sem_post(&log_control.mutex);
            #else
                printf(str);
            #endif
        #endif
    return NULL;
}

void *LA_thread(void *arg) {
    int i,j,tam, nit;
    FILE *Matriz;
    char tmp[50], str[100];
    sbuf_matriz item;

    while (m_control[0].lC || (m_control[0].aL > m_control[1].aL)) {
        // espera se nao houver slots vazios
        sem_wait(&shared[0].full);
        // espera a liberacao do buffer
        sem_wait(&shared[0].mutex);

        // procura itens ainda pendentes
        if (m_control[1].aL == m_control[0].aL) {
            sem_post(&shared[0].mutex);
            sem_post(&shared[0].empty);
            if(!m_control[0].lC) return NULL;
            else continue;
        }

        item = shared[0].buf[shared[0].out];
        shared[0].out = (shared[0].out+1)%BUFF_SIZE;

        //decrementa o numero de arquivos a serem processados
        sem_wait(&m_control[1].mutex);
        m_control[1].aL++;
        sem_post(&m_control[1].mutex);

        // libera o buffer
        sem_post(&shared[0].mutex);
        // aumenta o numero de slots vazios
        sem_post(&shared[0].empty);

        Matriz = fopen(item.Nome, "r");
        if (Matriz == NULL) {
            sprintf(str,"Problemas na abertura do arquivo '%s' contendo uma das estruturas de matriz.\n", item.Nome);
            #if SYSLOG
                syslog (LOG_NOTICE, str);
            #else
                #if DAEMON
                    sem_wait(&log_control.mutex);
                        sendLog(str);
                    sem_post(&log_control.mutex);
                #else
                    printf(str);
                #endif
            #endif
            return NULL;
        } else {
            rewind(Matriz);
            fscanf (Matriz, "%s", tmp);
            tam = atoi(tmp);
            // define a ordem da matriz na estrutura
            item.Ordem = tam;
            // aloca a memoria para as estruturas das matrizes
            item.A = (double**) malloc(tam*sizeof(double*));
            item.B = (double**) malloc(tam*sizeof(double*));
            item.C = (double**) malloc(tam*sizeof(double*));
            for (i=0; i<tam; i++) {
                item.A[i] = (double*) malloc(tam*sizeof(double));
                item.B[i] = (double*) malloc(tam*sizeof(double));
                item.C[i] = (double*) malloc(tam*sizeof(double));
            }
            //preenche a matriz A
            for (i=0; i< tam; i++)
                for (j=0; j< tam; j++)
                    item.A[i][j] = (fscanf (Matriz, "%s", tmp),atof(tmp));
            //preenche a matriz B
            for (i=0; i< tam; i++)
                for (j=0; j< tam; j++)
                    item.B[i][j] = (fscanf (Matriz, "%s", tmp),atof(tmp));

            fclose(Matriz);
            #if DEBUGLOG
                sprintf(str,"[LA]: As matrizes do arquivo '%s' foram preenchidas.\n", item.Nome);
                #if SYSLOG
                syslog (LOG_NOTICE, str);
                #else
                    #if DAEMON
                        sem_wait(&log_control.mutex);
                            sendLog(str);
                        sem_post(&log_control.mutex);
                    #else
                        printf(str);
                    #endif
                #endif
            #endif
        }

        // praparando para escrever item no buffer
        // espera se nao houver slots vazios
        sem_wait(&shared[1].empty);
        // espera a liberacao do buffer
        sem_wait(&shared[1].mutex);

        shared[1].buf[shared[1].in] = item;
        shared[1].in = (shared[1].in+1)%BUFF_SIZE;

        // libera o buffer
        sem_post(&shared[1].mutex);
        // aumenta o numero de slots vazios
        sem_post(&shared[1].full);

        #if DAEMON
        // recebimento de SIGKILL - o programa não monitorará novos arquivos e fechara somente os que esta escrevendo para não corromper os arquivos
            sem_wait(&kill_control.mutex);
            if (kill_hdl) {
                sem_post(&kill_control.mutex);
                break;
            }
            sem_post(&kill_control.mutex);
        #endif
    }

        //posta o termino das atividades de LA para a proxima thread
        sem_wait(&m_control[1].mutex);
        if (m_control[1].lC && !m_control[0].lC && (m_control[0].aL == m_control[1].aL)) {
            m_control[1].lC = 0;
        sprintf(str,"*[LA]*- THREAD LA have finished successfully.\n", item.Nome);
        #if SYSLOG
            syslog (LOG_NOTICE, str);
        #else
            #if DAEMON
                sem_wait(&log_control.mutex);
                    sendLog(str);
                sem_post(&log_control.mutex);
            #else
                printf(str);
            #endif
        #endif
        } sem_post(&m_control[1].mutex);
    return NULL;
}


void *MM_thread(void *arg) {
    int i, j ,k, nit;
    sbuf_matriz item;
    char str[100];
    while (m_control[1].lC || (m_control[1].aL > m_control[2].aL)) {
	// Preparando para ler o buffer
        // espera se nao houver slots vazios
        sem_wait(&shared[1].full);
        // espera a liberacao do buffer
        sem_wait(&shared[1].mutex);

        // procura itens ainda pendentes
        if (m_control[2].aL == m_control[1].aL) {
            sem_post(&shared[1].mutex);
            sem_post(&shared[1].empty);
            if(!m_control[1].lC) return NULL;
            else continue;
        }

        item = shared[1].buf[shared[1].out];
        shared[1].out = (shared[1].out+1)%BUFF_SIZE;

        //incrementa o numero de arquivos processados por MM
        sem_wait(&m_control[2].mutex);
        m_control[2].aL++;
        sem_post(&m_control[2].mutex);

        // libera o buffer
        sem_post(&shared[1].mutex);
        // aumenta o numero de slots vazios
        sem_post(&shared[1].empty);

        // BONUS 3 - Primeira Parte (multiplicação das matrizes em paralelo em duas subthreads)
        #pragma omp parallel num_threads(2)
        #pragma omp for
        for(i=0;i<item.Ordem ;i++) {
            for(j=0;j<item.Ordem ;j++) {
                item.C[i][j] = 0;
                for(k=0;k<item.Ordem;k++)
                    item.C[i][j] += item.A[i][k]*item.B[k][j];
            }
        }

        #if DEBUGLOG
            sprintf(str,"[MM]: Matriz C do arquivo '%s' (C=AxB) foi gerada com sucesso.\n", item.Nome);
            #if SYSLOG
                syslog (LOG_NOTICE, str);
            #else
                #if DAEMON
                    sem_wait(&log_control.mutex);
                        sendLog(str);
                    sem_post(&log_control.mutex);
                #else
                    printf(str);
                #endif
            #endif
        #endif

        // Preparando para escrever no buffer
        // espera se nao houver slots vazios
        sem_wait(&shared[2].empty);
        // espera a liberacao do buffer
        sem_wait(&shared[2].mutex);

        shared[2].buf[shared[2].in] = item;
        shared[2].in = (shared[2].in+1)%BUFF_SIZE;

        // libera o buffer
        sem_post(&shared[2].mutex);
        // aumenta o numero de slots vazios
        sem_post(&shared[2].full);

         #if DAEMON
        // recebimento de SIGKILL - o programa não monitorará novos arquivos e fechara somente os que esta escrevendo para não corromper os arquivos
            sem_wait(&kill_control.mutex);
            if (kill_hdl) {
                sem_post(&kill_control.mutex);
                break;
            }
            sem_post(&kill_control.mutex);
        #endif
    }

        //posta o termino das atividades de MM para a proxima thread
        sem_wait(&m_control[2].mutex);
        if (m_control[2].lC && !m_control[1].lC && (m_control[1].aL == m_control[2].aL)) {
            m_control[2].lC = 0;
            sprintf(str,"*[MM]*- THREAD MM have finished successfully.\n");
            #if SYSLOG
                syslog (LOG_NOTICE, str);
            #else
                #if DAEMON
                    sem_wait(&log_control.mutex);
                        sendLog(str);
                    sem_post(&log_control.mutex);
                #else
                    printf(str);
                #endif
            #endif
        } sem_post(&m_control[2].mutex);
    return NULL;
}

void crout(double **A, double **L, double **U, int n) {
	int i, j, k;
	double sum = 0;

	// cria as matrizes L e U (lower e uper triangular)
	for (i = 0; i < n; i++) {
		U[i][i] = 1;
	}

	for (j = 0; j < n; j++) {
		for (i = j; i < n; i++) {
			sum = 0;
			for (k = 0; k < j; k++) {
				sum = sum + L[i][k] * U[k][j];
			}
			L[i][j] = A[i][j] - sum;
		}

		for (i = j; i < n; i++) {
			sum = 0;
			for(k = 0; k < j; k++) {
				sum = sum + L[j][k] * U[k][i];
			}
			if (L[j][j] == 0) {
				printf("det(L) close to 0!\n Can't divide by 0...\n");
				exit(EXIT_FAILURE);
			}
			U[j][i] = (A[j][i] - sum) / L[j][j];
		}
	}
}

void *DM_thread(void *arg) {
    int i, j, nit;
    sbuf_matriz item;
    double **L, **U, detL, detU;
    char str[100];
    while (m_control[2].lC || (m_control[2].aL > m_control[3].aL)) {
	// Preparando para ler o buffer
        // espera se nao houver slots vazios
        sem_wait(&shared[2].full);
        // espera a liberacao do buffer
        sem_wait(&shared[2].mutex);

        // procura itens ainda pendentes
        if (m_control[3].aL == m_control[2].aL) {
            sem_post(&shared[2].mutex);
            sem_post(&shared[2].empty);
            if(!m_control[2].lC) return NULL;
            else continue;
        }

        item = shared[2].buf[shared[2].out];
        shared[2].out = (shared[2].out+1)%BUFF_SIZE;

        //incrementa o numero de arquivos processados por DA
        sem_wait(&m_control[3].mutex);
        m_control[3].aL++;
        sem_post(&m_control[3].mutex);

        // libera o buffer
        sem_post(&shared[2].mutex);
        // aumenta o numero de slots vazios
        sem_post(&shared[2].empty);

        // aloca a memoria para as matrizes triangulares
        L = (double**) malloc(item.Ordem*sizeof(double*));
            U = (double**) malloc(item.Ordem*sizeof(double*));
            for (i=0; i<item.Ordem; i++) {
                L[i] = (double*) malloc(item.Ordem*sizeof(double));
                U[i] = (double*) malloc(item.Ordem*sizeof(double));
            }

        detL = 0; detU = 0;
        // cria as matrizes triangulares
        crout(item.C,L,U,item.Ordem);
        // BONUS 3 - Segunda Parte (obter o determinante de U e L em paralelo em duas subthreads)
        #pragma omp parallel num_threads(2)
        #pragma omp for
        for(i=0; i<item.Ordem ;i++) {
            detL += L[i][i];
            detU += U[i][i];
        } item.E = detL*detU;

        // libera a memoria alocada para as matrizes triangulares
        for (i=0; i<item.Ordem; i++) {
            free(L[i]);
            free(U[i]);
        }
        free(L); free(U);

        #if DEBUGLOG
            sprintf(str,"[DM]: O determinante de C foi calculado para o arquivo '%s'\n", item.Nome);
            #if SYSLOG
                syslog (LOG_NOTICE, str);
            #else
                #if DAEMON
                    sem_wait(&log_control.mutex);
                        sendLog(str);
                    sem_post(&log_control.mutex);
                #else
                    printf(str);
                #endif
            #endif
        #endif
        // Preparando para escrever no buffer
        // espera se nao houver slots vazios
        sem_wait(&shared[3].empty);
        // espera a liberacao do buffer
        sem_wait(&shared[3].mutex);

        shared[3].buf[shared[3].in] = item;
        shared[3].in = (shared[3].in+1)%BUFF_SIZE;

        // libera o buffer
        sem_post(&shared[3].mutex);
        // aumenta o numero de slots vazios
        sem_post(&shared[3].full);
        #if DAEMON
        // recebimento de SIGKILL - o programa não monitorará novos arquivos e fechara somente os que esta escrevendo para não corromper os arquivos
            sem_wait(&kill_control.mutex);
            if (kill_hdl) {
                sem_post(&kill_control.mutex);
                break;
            }
            sem_post(&kill_control.mutex);
        #endif
    }

        //posta o termino das atividades de DM para a proxima thread
        sem_wait(&m_control[3].mutex);
        if (m_control[3].lC && !m_control[2].lC && (m_control[2].aL == m_control[3].aL)) {
            m_control[3].lC = 0;
            sprintf(str,"*[DM]*- THREAD DM have finished successfully.\n");
            #if SYSLOG
                syslog (LOG_NOTICE, str);
            #else
                #if DAEMON
                    sem_wait(&log_control.mutex);
                        sendLog(str);
                    sem_post(&log_control.mutex);
                #else
                    printf(str);
                #endif
            #endif
        } sem_post(&m_control[3].mutex);
    return NULL;
}


void *EA_thread(void *arg) {
    int i, j, nit = 0;
    sbuf_matriz item;
    FILE *output;
    char tmp[50],str[100];

     while (m_control[3].lC || (m_control[3].aL > m_control[4].aL)) {
	// Preparando para ler o buffer
        // espera se nao houver slots vazios
        sem_wait(&shared[3].full);
        // espera a liberacao do buffer
        sem_wait(&shared[3].mutex);

        // procura itens ainda pendentes
        if (m_control[4].aL == m_control[3].aL) {
            sem_post(&shared[3].mutex);
            sem_post(&shared[3].empty);
            if(!m_control[3].lC) return NULL;
            else continue;
        }

        item = shared[3].buf[shared[3].out];
        shared[3].out = (shared[3].out+1)%BUFF_SIZE;

        //incrementa o numero de arquivos processados por EA
        sem_wait(&m_control[4].mutex);
        m_control[4].aL++;
        sem_post(&m_control[4].mutex);

        sprintf(tmp, "%s.out", item.Nome);
        output = fopen(tmp, "w");
        if (output == NULL) {
            printf("Problemas na criação do arquivo de saida para a entrada %s.\n", item.Nome);
            return NULL;
        } else {
            fprintf(output, "================================================================\n");
            fprintf(output, "<%s> - <%i>\n", item.Nome,item.Ordem);
            fprintf(output, "----------------------------------------------------------------\n");
            // passando a matriz A
            for (i=0; i<item.Ordem; i++)
                for (j=0; j<item.Ordem; j++)
                    if ((item.Ordem-j) > 1 ) fprintf(output,"%.2f ",item.A[i][j]);
                    else fprintf(output,"%.2f\n",item.A[i][j]);
            fprintf(output, "----------------------------------------------------------------\n");
            // passando a matriz B
            for (i=0; i<item.Ordem; i++)
                for (j=0; j<item.Ordem; j++)
                    if ((item.Ordem-j) > 1 ) fprintf(output,"%.2f ",item.B[i][j]);
                    else fprintf(output,"%.2f\n",item.B[i][j]);
            fprintf(output, "----------------------------------------------------------------\n");
            // passando a matriz C
            for (i=0; i<item.Ordem; i++)
                for (j=0; j<item.Ordem; j++)
                    if ((item.Ordem-j) > 1 ) fprintf(output,"%.2f ",item.C[i][j]);
                    else fprintf(output,"%.2f\n",item.C[i][j]);
            fprintf(output, "----------------------------------------------------------------\n");
            fprintf(output,"%.2f\n",item.E);
            fprintf(output, "================================================================\n");

            #if DEBUGLOG
                sprintf(str,"[EA]: Arquivo de saida referente a '%s' gerado com sucesso!\n",item.Nome);
                #if SYSLOG
                    syslog (LOG_NOTICE, str);
                #else
                    #if DAEMON
                        sem_wait(&log_control.mutex);
                            sendLog(str);
                        sem_post(&log_control.mutex);
                    #else
                        printf(str);
                    #endif
                #endif
            #endif
        fclose(output);


        #if FREE_MEM
        // libera a memoria alocada para as matrizes
        for (i=0; i<item.Ordem; i++) {
            free(item.A[i]);
            free(item.B[i]);
            free(item.C[i]);
        }
        free(item.A); free(item.B); free(item.C);
            #if DEBUGLOG
                sprintf(str,"[EA]: Memoria referente a '%s' liberada com sucesso!\n",item.Nome);
                #if SYSLOG
                syslog (LOG_NOTICE, str);
                #else
                    #if DAEMON
                        sem_wait(&log_control.mutex);
                            sendLog(str);
                        sem_post(&log_control.mutex);
                    #else
                        printf(str);
                    #endif
                #endif
            #endif
        #endif
    }

        // libera o buffer
        sem_post(&shared[3].mutex);
        // aumenta o numero de slots vazios
        sem_post(&shared[3].full);

        #if DAEMON
        // recebimento de SIGKILL - o programa não monitorará novos arquivos e fechara somente os que esta escrevendo para não corromper os arquivos
            sem_wait(&kill_control.mutex);
            if (kill_hdl) {
                sem_post(&kill_control.mutex);
                break;
            }
            sem_post(&kill_control.mutex);
        #endif
    }
        //posta o termino das atividades de LA para a proxima thread
        sem_wait(&m_control[4].mutex);
        if (m_control[4].lC && !m_control[3].lC && (m_control[3].aL == m_control[4].aL)) {
            m_control[4].lC = 0;
            sprintf(str,"*[EA]*- THREAD EA have finished successfully.\n");
            #if SYSLOG
                syslog (LOG_NOTICE, str);
            #else
                #if DAEMON
                    sem_wait(&log_control.mutex);
                        sendLog(str);
                    sem_post(&log_control.mutex);
                #else
                    printf(str);
                #endif
            #endif
        } sem_post(&m_control[4].mutex);
    return NULL;
}

void sig_handler(int s) {
    switch (s) {
        case SIGKILL:
            sem_wait(&kill_control.mutex);
                kill_hdl = 1;
            sem_post(&kill_control.mutex);
        break;
        case SIGTERM:
            sem_wait(&term_control.mutex);
                term_hdl = 1;
            sem_post(&term_control.mutex);
        break;
    }
}

// sislog para o bonus 1
static void daemonize() {
    pid_t pid;
    int x;
    char str[100];

    // cria um processo filho
    pid = fork();

    // aborta se algum erro ocorrer
    if (pid < 0) exit(EXIT_FAILURE);
    // O Pai é morto
    if (pid > 0) exit(EXIT_SUCCESS);
    // o filho se transforma em lider da sessão
    if (setsid() < 0) exit(EXIT_FAILURE);

    //try catch de sinais (sigkill e sigterm)
    if (signal(SIGKILL, sig_handler) == SIG_ERR) {
        sprintf(str,"SIGKILL recebido com erro...\n");
        #if SYSLOG
            syslog (LOG_NOTICE, str);
        #else
            sendLog(str);
        #endif
    }
    if (signal(SIGTERM, sig_handler) == SIG_ERR) {
        sprintf(str,"SIGTERM recebido com erro...\n");
        #if SYSLOG
            syslog (LOG_NOTICE, str);
        #else
            sendLog(str);
        #endif
    }

    // muda as permissões do filho
    umask(0);

    // mata os descriptors
    for (x = sysconf(_SC_OPEN_MAX); x>0; x--) close (x);

    sprintf(str,"Starting the application as a Daemon. > PID = %i.\n", getpid());
    #if SYSLOG
        // cria arquivo de log do sistema
        openlog("daemon", LOG_CONS | LOG_PID | LOG_NDELAY | LOG_LOCAL1);
        syslog (LOG_NOTICE, str);
    #else
        sendLog(str);
    #endif
}

int main(int argc, char** args) {
#if DAEMON
    daemonize();
#endif

    pthread_t iLC, iLA, iMM, iDM, iEA;
    int LC[LC_N], LA[LA_N], MM[MM_N], DM[DM_N], EA[EA_N], i;
    int problem = 0;
    char str[100];

    sprintf(str,"[INIT] Configurando semaforos e parametros inciais.\n");
    #if SYSLOG
        syslog (LOG_NOTICE, str);
    #else
        #if DAEMON
            sendLog(str);
        #else
            printf(str);
        #endif
    #endif

    // inicia o controle dos semáforos
    for (i = 0; i < 4; ++i) {
    	sem_init(&shared[i].full, 0, 0);
    	sem_init(&shared[i].empty, 0, BUFF_SIZE);
    	sem_init(&shared[i].mutex, 0, 1);
    	sem_init(&m_control[i].mutex, 0, 1);
    	m_control[i].aL = 0;
    	m_control[i].lC = 1;
    }
    sem_init(&m_control[4].mutex, 0, 1);
    m_control[4].aL = 0;
    m_control[4].lC = 1;
    sem_init(&log_control.mutex, 0, 1);
    sem_init(&term_control.mutex, 0, 1);
    sem_init(&kill_control.mutex, 0, 1);

    sprintf(str,"[INIT] -Iniciando a execucao do programa.\n\n");
    #if SYSLOG
        syslog (LOG_NOTICE, str);
    #else
        #if DAEMON
            sendLog(str);
        #else
            printf(str);
        #endif
    #endif

    for (i = 0; i < LC_N; ++i) {
       LC[i] = i;
       pthread_create(&iLC, NULL, LC_thread, (void*) &LC[i]);
    }

    for (i = 0; i < LA_N; ++i) {
       LA[i]=i;
       pthread_create(&iLA, NULL, LA_thread, (void*) &LA[i]);
    }

    for (i = 0; i < MM_N; ++i) {
        MM[i]=i;
        pthread_create(&iMM, NULL, MM_thread, (void*) &MM[i]);
    }

    for (i = 0; i < DM_N; ++i) {
        DM[i]=i;
        pthread_create(&iDM, NULL, DM_thread, (void*) &DM[i]);
    }

    for (i = 0; i < EA_N; ++i) {
        EA[i]=i;
        pthread_create(&iEA, NULL, EA_thread, (void*) &EA[i]);
    }

    // Espera a primera finalizacao da thread iEA (decrementando assim o valor das matrizes que ela espera para 0
    while (m_control[4].lC) sleep(SLEEP_BURST);
    // mata as threads em aberto ja que a execucao terminou
    pthread_cancel(iEA);
    pthread_cancel(iDM);
    pthread_cancel(iMM);
    pthread_cancel(iLA);
    pthread_cancel(iLC);

    sprintf(str,"Execucao terminada com sucesso\n");
    #if SYSLOG
        syslog (LOG_NOTICE, str);
    #else
        #if DAEMON
            sendLog(str);
        #else
            printf(str);
        #endif
    #endif

    #if WAITTOBEKILLED
        while (!kill_hdl && !term_hdl) {
            sprintf(str,"O daemon acabou as atividades e esta esperando um sinal (sleep burst 5x - flood :D)\n");
            printf(str);
            #if SYSLOG
                syslog (LOG_NOTICE, str);
            #else
                sendLog(str);
            #endif
            sleep(5*SLEEP_BURST);
        }
    #endif

    closelog();
    exit(EXIT_SUCCESS);
}
