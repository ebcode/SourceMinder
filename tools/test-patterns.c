#include <stdio.h>
#include <regex.h>

int main() {
    regex_t regex;
    const char *patterns[] = {
        "^[0-9]+px$",
        "^#[0-9a-fA-F]{3}([0-9a-fA-F]{3})?$",
        "^0x[0-9a-fA-F]+$",
        "^0[0-9a-fA-F]*[a-fA-F][0-9a-fA-F]*$",
        "^[0-9][0-9a-fA-F]*[a-fA-F][0-9a-fA-F]*$",
        "^[0-9]+(st|nd|rd|th)$",
        "^[0-9]+[smhdywµun]s?$",
        "^[0-9]+[kmg]?[hH][zZ]$",
        "^[0-9]+[kmgtpKMGTP][bB]$",
        "^[0-9]+x[0-9]+$",
        "^[0-9]+(\\.[0-9]+)?(em|rem|vh|vw|vmin|vmax|ch|ex|cm|mm|in|pt|pc)$",
        "^[0-9]+(\\.[0-9]+)?%$",
        "^v?[0-9]+(\\.[0-9]+){0,3}$",
        "^[0-9]{2,5}$",
        "^rgba?\\([0-9,.\\s]+\\)$",
        "^hsla?\\([0-9,%.\\s]+\\)$",
        NULL
    };
    
    const char *test_words[] = {
        "testFunction", "userName", "width", "10px", "myVariable", "hello", 
        "test", "const", "function", "100", "v1", "8080", NULL
    };
    
    for (int i = 0; patterns[i] != NULL; i++) {
        if (regcomp(&regex, patterns[i], REG_EXTENDED | REG_NOSUB) != 0) {
            printf("Pattern %d failed to compile\n", i);
            continue;
        }
        
        for (int j = 0; test_words[j] != NULL; j++) {
            if (regexec(&regex, test_words[j], 0, NULL, 0) == 0) {
                printf("Pattern %d (%s) matches: %s\n", i, patterns[i], test_words[j]);
            }
        }
        regfree(&regex);
    }
    
    return 0;
}
