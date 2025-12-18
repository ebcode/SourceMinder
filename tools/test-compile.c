#include <stdio.h>
#include <regex.h>
#include <string.h>

int main() {
    FILE *f = fopen("data/regex-patterns.txt", "r");
    if (!f) {
        printf("Can't open file\n");
        return 1;
    }
    
    char line[256];
    regex_t regex;
    int pattern_num = 0;
    
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        pattern_num++;
        int ret = regcomp(&regex, line, REG_EXTENDED | REG_NOSUB);
        if (ret != 0) {
            char error_buf[256];
            regerror(ret, &regex, error_buf, sizeof(error_buf));
            printf("Pattern %d FAILED: '%s' -> %s\n", pattern_num, line, error_buf);
        } else {
            printf("Pattern %d OK: '%s'\n", pattern_num, line);
            
            // Test against 'foo'
            if (regexec(&regex, "foo", 0, NULL, 0) == 0) {
                printf("  *** MATCHES 'foo' ***\n");
            }
            
            regfree(&regex);
        }
    }
    
    fclose(f);
    return 0;
}
