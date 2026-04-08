#include <stdio.h>

int main() {
    // Test special characters: @email, #issue, ?query, &param
    char *email = "user@example.com";
    char *url = "https://api.com/users?id=123&sort=name";
    char *issue = "See issue #456 for details";
    char *format = "Value: 100%";
    char *placeholder = "%user%";
    char *regex = "^[a-z]+$";
    char *template = "<div>Hello ${name}!</div>";

    return 0;
}
