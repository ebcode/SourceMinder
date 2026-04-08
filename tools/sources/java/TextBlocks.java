// Java 21 text blocks (Java 15+)

public class TextBlocks {

    public static void basicTextBlocks() {
        // Traditional string
        String traditional = "Line 1\nLine 2\nLine 3";

        // Text block
        String textBlock = """
            Line 1
            Line 2
            Line 3
            """;

        // HTML example
        String html = """
            <html>
                <body>
                    <p>Hello, World!</p>
                </body>
            </html>
            """;

        // JSON example
        String json = """
            {
                "name": "Alice",
                "age": 30,
                "city": "New York"
            }
            """;

        // SQL example
        String sql = """
            SELECT id, name, email
            FROM users
            WHERE age > 18
            ORDER BY name
            """;
    }

    public static void textBlockWithInterpolation() {
        String name = "Alice";
        int age = 30;

        // Text block with string formatting
        String message = """
            Name: %s
            Age: %d
            Status: Active
            """.formatted(name, age);

        // Alternative with String.format
        String formatted = String.format("""
            User: %s
            Age: %d
            """, name, age);
    }

    public static void indentation() {
        // Indentation is automatically managed
        String code = """
            public class Example {
                public void method() {
                    System.out.println("Hello");
                }
            }
            """;

        // Explicit indentation control with \s
        String spaced = """
            Line 1\s
            Line 2\s
            Line 3\s
            """;
    }

    public static void escapeSequences() {
        // Line continuation with \
        String continued = """
            This is a \
            single line
            """;

        // Quotes don't need escaping
        String quotes = """
            She said "Hello" to him.
            """;

        // But triple quotes do
        String tripleQuotes = """
            Use \"\"\" for text blocks
            """;
    }

    public static void emptyLines() {
        // Preserving empty lines
        String withEmpty = """
            First line

            Third line (second was empty)
            """;
    }

    public static void regularExpressions() {
        // Regex patterns are clearer in text blocks
        String regex = """
            ^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$
            """;
    }

    public static void multilineComments() {
        // Text block for documentation
        String doc = """
            /**
             * This is a documentation example
             * @param name the user's name
             * @return greeting message
             */
            """;
    }
}
