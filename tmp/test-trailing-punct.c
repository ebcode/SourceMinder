/* Test file for trailing punctuation in comments and strings.
 * This comment has words: Error! Warning? Info: Debug. Notice, Alert;
 * URLs should work: https://example.com and emails user@example.com
 * Ellipsis test... and multiple!!! punctuation???
 */

int main() {
    printf("Error: database connection failed");
    printf("Warning! Invalid input detected");
    printf("Question? Are you sure?");
    printf("Multiple punctuation...");
    printf("URL test: https://example.com works fine!");

    // Single line comment with Error: and Warning! and Info?
    // Test ellipsis... and semicolon; and comma,

    return 0;
}
