// Java 21 pattern matching features

public class PatternMatching {

    // Pattern matching for instanceof (Java 16+)
    public static void instanceofPatterns(Object obj) {
        // Traditional instanceof
        if (obj instanceof String) {
            String str = (String) obj;
            System.out.println(str.toUpperCase());
        }

        // Pattern matching instanceof
        if (obj instanceof String s) {
            System.out.println(s.toUpperCase());
        }

        // With additional condition
        if (obj instanceof String s && s.length() > 5) {
            System.out.println("Long string: " + s);
        }
    }

    // Pattern matching in switch (Java 21)
    public static String formatValue(Object obj) {
        return switch (obj) {
            case Integer i -> "int: " + i;
            case Long l -> "long: " + l;
            case Double d -> "double: " + d;
            case String s -> "string: " + s;
            case null -> "null value";
            default -> "unknown: " + obj;
        };
    }

    // Guarded patterns
    public static String describeNumber(Object obj) {
        return switch (obj) {
            case Integer i when i > 0 -> "positive";
            case Integer i when i < 0 -> "negative";
            case Integer i -> "zero";
            case null -> "null";
            default -> "not a number";
        };
    }

    // Record patterns (Java 21)
    record Point(int x, int y) {}

    public static void recordPatterns(Object obj) {
        if (obj instanceof Point(int x, int y)) {
            System.out.println("Point at (" + x + ", " + y + ")");
        }
    }

    public static String describePoint(Object obj) {
        return switch (obj) {
            case Point(int x, int y) when x == 0 && y == 0 -> "origin";
            case Point(int x, int y) when x == 0 -> "on y-axis";
            case Point(int x, int y) when y == 0 -> "on x-axis";
            case Point(int x, int y) -> "point at (" + x + ", " + y + ")";
            default -> "not a point";
        };
    }

    // Nested record patterns
    record Person(String name, Address address) {}
    record Address(String city, String country) {}

    public static void nestedPatterns(Object obj) {
        if (obj instanceof Person(String name, Address(String city, String country))) {
            System.out.println(name + " from " + city + ", " + country);
        }
    }

    // Sealed class patterns
    sealed interface Shape permits Circle, Rectangle {}
    record Circle(double radius) implements Shape {}
    record Rectangle(double width, double height) implements Shape {}

    public static double calculateArea(Shape shape) {
        return switch (shape) {
            case Circle(double r) -> Math.PI * r * r;
            case Rectangle(double w, double h) -> w * h;
        };
    }

    // Pattern matching with null
    public static String handleNull(String str) {
        return switch (str) {
            case null -> "null string";
            case String s when s.isEmpty() -> "empty string";
            case String s -> "string: " + s;
        };
    }

    // Multiple patterns
    public static String classifyValue(Object obj) {
        return switch (obj) {
            case Integer i, Long l -> "integral number";
            case Float f, Double d -> "floating-point number";
            case String s -> "string";
            default -> "other";
        };
    }
}
