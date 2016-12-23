/*
 * LSP Kursa projekts
 * Alberts Saulitis
 * Viesturs Ružāns
 *
 * 23.12.2016
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>


void *safe_malloc(size_t size) {
    void *p = malloc(size);
    if (!p) { //Ja alokācija neizdevās, izdrukājam iemeslu, kurš iegūts no errno
        fprintf(stderr, "%s\n", strerror(errno));

        exit(EXIT_FAILURE);
    }
    return p;
}

/*
 * Paligfunkcija kura palidz programmai iziet kludas gadijuma
 */
void exitWithMessage(char error[]) {
    printf("Error %s\n", error);
    exit(EXIT_FAILURE);
}

void processArgs(int argc, char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            //TODO
        }
        else if (strcmp(argv[i], "-s") == 0) {
            //TODO
        }
        else if (strcmp(argv[i], "-h") == 0) {
            //TODO
            exit(0);
        }
    }
}




int main(int argc, char *argv[]) {

    return 0;
}
