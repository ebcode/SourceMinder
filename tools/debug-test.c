#include <stdio.h>
#include <regex.h>
#include <string.h>

int main() {
    regex_t regex;
    // Test the version pattern specifically
    const char *pattern = "^v?[0-9]+(\\.[0-9]+){0,3}$";
    
    const char *words[] = {
        "testFunction", "userName", "width", "myVariable", 
        "test", "hello", "v1", "1", "12", "123", NULL
    };
    
    if (regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
        printf("Failed to compile\n");
        return 1;
    }
    
    for (int i = 0; words[i] != NULL; i++) {
        int match = regexec(&regex, words[i], 0, NULL, 0);
        printf("'%s' -> %s\n", words[i], match == 0 ? "FILTERED" : "ok");
    }
    
    regfree(&regex);
    return 0;
}
