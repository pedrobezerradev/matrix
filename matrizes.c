//Pedro Eugenio Gomes Bezerra
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define Count 16 // quantidade de matrizes
#define OrdMAx 80 // ordem maxima das matrizes
#define OrdMIn 40 // ordem minima das matrizes

int main(int argc, char** args) {
    int sz, i, j, qt = Count; //numero aleatorio de linhas pre definido
    FILE *F, *M; // declaração dos arquivos de catalogo e matriz
    double **A, **B; // declaração das futuras matrizes
    char str[100];

    srand(time(NULL));
    printf("Criando Catalogo.\n");
    F = fopen("entrada.in", "w");
    if (F == NULL) {
        printf("Problema criando o arquivo de catalogo.\n Terminando o Programa.\n");
        return -1;
    }
    else {

        do {
            sprintf(str, "Mat[%d]", (1+(Count-qt)));
            printf("Criando %s.\n", str);
            M = fopen(str, "w");
            if (M == NULL) {
                printf("Problemas criando o arquivo da matriz %i.\n Terminando o Programa.\n", (1+(Count-qt)));
                return -2;
            }
            else {
				sz = (OrdMIn + rand()%(OrdMAx-OrdMIn+1)); // define a ordem da matriz
                fprintf(M, "%i\n", sz);

                // Cria a primera matriz com elementos de 500-1000
                for (i=0; i<sz; i++)
                    for (j=0; j<sz; j++)
                        fprintf(M,"%.2f ",(500.0 + (float) (rand()%501)));
				fprintf(M,"\n");

                // Cria a segunda matriz com elementos de 500-1000
                for (i=0; i<sz; i++)
                    for (j=0; j<sz; j++)
                        fprintf(M,"%.2f ",(500.0 + (float) (rand()%501)));
				fprintf(M,"\n");

                fclose(M);
                printf("Matriz %s Criado com Sucesso.\n", str);
            }

            fprintf(F, "%s\n", str);
        } while (--qt);

        fclose(F);
        printf("Arquivo de catalogo Criado com Sucesso. (entrada.in)\n", str);
    }

    return 0;
}
