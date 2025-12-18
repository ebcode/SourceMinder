#include <stdio.h>
#include <string.h>

#define MAX_WORD_LENGTH 64

int main() {
    FILE *f = fopen("data/regex-patterns.txt", "r");
    if (!f) {
        printf("Can't open\n");
        return 1;
    }
    
    char line[MAX_WORD_LENGTH];
    int count = 0;
    
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        count++;
        printf("%d: '%s'\n", count, line);
    }
    
    printf("\nTotal: %d\n", count);
    fclose(f);
    return 0;
}
