#include "../shared/filter.h"
#include <stdio.h>

int main() {
    SymbolFilter filter;
    if (filter_init(&filter, "data") != 0) {
        printf("Filter init failed\n");
        return 1;
    }

    printf("Loaded %d regex patterns\n", filter.regex_patterns.count);
    printf("Loaded %d stopwords\n", filter.stopwords.count);
    printf("Loaded %d keywords\n", filter.ts_keywords.count);

    const char *test_words[] = {"foo", "bar", "testFunction", "userName", "10px", "const", NULL};

    for (int i = 0; test_words[i] != NULL; i++) {
        int should_index = filter_should_index(&filter, test_words[i]);
        printf("'%s' -> %s\n", test_words[i], should_index ? "INDEX" : "FILTER");
    }

    filter_free(&filter);
    return 0;
}
