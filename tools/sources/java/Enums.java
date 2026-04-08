// Java 21 enum types

public class Enums {

    // Basic enum
    public enum Day {
        MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY, SUNDAY
    }

    // Enum with constructor
    public enum Size {
        SMALL(1),
        MEDIUM(2),
        LARGE(3);

        private final int value;

        Size(int value) {
            this.value = value;
        }

        public int getValue() {
            return value;
        }
    }

    // Enum with multiple fields
    public enum Planet {
        MERCURY(3.303e+23, 2.4397e6),
        VENUS(4.869e+24, 6.0518e6),
        EARTH(5.976e+24, 6.37814e6),
        MARS(6.421e+23, 3.3972e6);

        private final double mass;
        private final double radius;

        Planet(double mass, double radius) {
            this.mass = mass;
            this.radius = radius;
        }

        public double getMass() {
            return mass;
        }

        public double getRadius() {
            return radius;
        }

        public double surfaceGravity() {
            return 6.67300E-11 * mass / (radius * radius);
        }
    }

    // Enum with methods
    public enum Operation {
        PLUS {
            @Override
            public double apply(double x, double y) {
                return x + y;
            }
        },
        MINUS {
            @Override
            public double apply(double x, double y) {
                return x - y;
            }
        },
        TIMES {
            @Override
            public double apply(double x, double y) {
                return x * y;
            }
        },
        DIVIDE {
            @Override
            public double apply(double x, double y) {
                return x / y;
            }
        };

        public abstract double apply(double x, double y);
    }

    // Enum implementing interface
    public interface Describable {
        String describe();
    }

    public enum Status implements Describable {
        ACTIVE {
            @Override
            public String describe() {
                return "The item is active";
            }
        },
        INACTIVE {
            @Override
            public String describe() {
                return "The item is inactive";
            }
        },
        PENDING {
            @Override
            public String describe() {
                return "The item is pending";
            }
        }
    }

    // Enum with static methods
    public enum Color {
        RED, GREEN, BLUE;

        public static Color fromString(String name) {
            return valueOf(name.toUpperCase());
        }

        public static boolean isValidColor(String name) {
            try {
                valueOf(name.toUpperCase());
                return true;
            } catch (IllegalArgumentException e) {
                return false;
            }
        }
    }

    public static void useEnums() {
        // Basic enum usage
        Day today = Day.MONDAY;

        // Switch with enum
        String message = switch (today) {
            case MONDAY -> "Start of week";
            case FRIDAY -> "End of week";
            case SATURDAY, SUNDAY -> "Weekend";
            default -> "Weekday";
        };

        // Enum methods
        Size size = Size.MEDIUM;
        int value = size.getValue();

        // Enum iteration
        for (Day day : Day.values()) {
            System.out.println(day);
        }

        // Enum comparison
        if (today == Day.MONDAY) {
            System.out.println("It's Monday");
        }

        // Enum ordinal
        int ordinal = today.ordinal();

        // Enum name
        String name = today.name();
    }
}
