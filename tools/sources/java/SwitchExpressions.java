// Java 21 switch expressions (Java 14+)

public class SwitchExpressions {

    // Traditional switch statement
    public static void traditionalSwitch(int day) {
        switch (day) {
            case 1:
                System.out.println("Monday");
                break;
            case 2:
                System.out.println("Tuesday");
                break;
            case 3:
                System.out.println("Wednesday");
                break;
            default:
                System.out.println("Other day");
                break;
        }
    }

    // Switch expression with arrow syntax
    public static String switchExpression(int day) {
        return switch (day) {
            case 1 -> "Monday";
            case 2 -> "Tuesday";
            case 3 -> "Wednesday";
            case 4 -> "Thursday";
            case 5 -> "Friday";
            case 6 -> "Saturday";
            case 7 -> "Sunday";
            default -> "Invalid day";
        };
    }

    // Switch expression with multiple values
    public static String dayType(int day) {
        return switch (day) {
            case 1, 2, 3, 4, 5 -> "Weekday";
            case 6, 7 -> "Weekend";
            default -> "Invalid";
        };
    }

    // Switch expression with block
    public static String switchWithBlock(int day) {
        return switch (day) {
            case 1 -> {
                String message = "Start of week";
                yield message;
            }
            case 5 -> {
                String message = "End of week";
                yield message;
            }
            default -> "Regular day";
        };
    }

    // Switch with yield in traditional syntax
    public static String switchWithYield(int value) {
        return switch (value) {
            case 1:
                yield "One";
            case 2:
                yield "Two";
            default:
                yield "Other";
        };
    }

    // Switch on String
    public static int stringSwitch(String color) {
        return switch (color) {
            case "RED" -> 1;
            case "GREEN" -> 2;
            case "BLUE" -> 3;
            default -> 0;
        };
    }

    // Switch on enum
    enum Size { SMALL, MEDIUM, LARGE }

    public static int sizeToInt(Size size) {
        return switch (size) {
            case SMALL -> 1;
            case MEDIUM -> 2;
            case LARGE -> 3;
        };
    }

    // Exhaustive switch (no default needed for sealed types)
    sealed interface Shape permits Circle, Square {}
    record Circle(double radius) implements Shape {}
    record Square(double side) implements Shape {}

    public static double area(Shape shape) {
        return switch (shape) {
            case Circle c -> Math.PI * c.radius() * c.radius();
            case Square s -> s.side() * s.side();
            // No default needed - all cases covered
        };
    }

    // Pattern matching in switch (Java 21)
    public static String describe(Object obj) {
        return switch (obj) {
            case Integer i -> "Integer: " + i;
            case String s -> "String: " + s;
            case Double d -> "Double: " + d;
            case null -> "null value";
            default -> "Unknown type";
        };
    }

    // Guarded patterns in switch
    public static String classifyNumber(Object obj) {
        return switch (obj) {
            case Integer i when i > 0 -> "Positive integer";
            case Integer i when i < 0 -> "Negative integer";
            case Integer i -> "Zero";
            case Double d when d > 0 -> "Positive double";
            case Double d when d < 0 -> "Negative double";
            case Double d -> "Zero double";
            default -> "Not a number";
        };
    }

    // Record patterns in switch
    record Point(int x, int y) {}

    public static String describePoint(Point point) {
        return switch (point) {
            case Point(int x, int y) when x == 0 && y == 0 -> "Origin";
            case Point(int x, int y) when x == 0 -> "On Y-axis";
            case Point(int x, int y) when y == 0 -> "On X-axis";
            case Point(int x, int y) -> "Point at (" + x + ", " + y + ")";
        };
    }

    // Nested pattern matching
    record Name(String first, String last) {}
    record Person(Name name, int age) {}

    public static String describePerson(Person person) {
        return switch (person) {
            case Person(Name(String f, String l), int a)
                when a < 18 -> f + " " + l + " (minor)";
            case Person(Name(String f, String l), int a)
                -> f + " " + l + " (" + a + " years old)";
        };
    }

    // Using switch result directly
    public static void usingResult() {
        int day = 3;
        System.out.println(switch (day) {
            case 1, 2, 3, 4, 5 -> "Workday";
            case 6, 7 -> "Weekend";
            default -> "Invalid";
        });
    }
}
