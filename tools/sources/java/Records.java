// Java 21 record types (Java 14+)

public class Records {

    // Basic record
    public record Point(int x, int y) {}

    // Record with multiple components
    public record User(String name, String email, int age) {}

    // Record with validation in compact constructor
    public record Person(String name, int age) {
        public Person {
            if (age < 0) {
                throw new IllegalArgumentException("Age cannot be negative");
            }
            if (name == null || name.isBlank()) {
                throw new IllegalArgumentException("Name cannot be blank");
            }
        }
    }

    // Record with custom methods
    public record Rectangle(double width, double height) {
        public double area() {
            return width * height;
        }

        public double perimeter() {
            return 2 * (width + height);
        }
    }

    // Record implementing interface
    public interface Drawable {
        void draw();
    }

    public record Circle(double radius) implements Drawable {
        @Override
        public void draw() {
            System.out.println("Drawing circle with radius " + radius);
        }

        public double area() {
            return Math.PI * radius * radius;
        }
    }

    // Record with static members
    public record Config(String host, int port) {
        public static final int DEFAULT_PORT = 8080;

        public static Config createDefault(String host) {
            return new Config(host, DEFAULT_PORT);
        }
    }

    // Nested record
    public record Container(String name, Data data) {
        public record Data(int value, String label) {}
    }

    // Generic record
    public record Pair<T, U>(T first, U second) {
        public T getFirst() {
            return first;
        }

        public U getSecond() {
            return second;
        }
    }

    // Record with custom constructor
    public record Range(int min, int max) {
        public Range(int min, int max) {
            if (min > max) {
                throw new IllegalArgumentException("min must be <= max");
            }
            this.min = min;
            this.max = max;
        }

        public boolean contains(int value) {
            return value >= min && value <= max;
        }
    }

    public static void useRecords() {
        Point p = new Point(10, 20);
        User user = new User("Alice", "alice@example.com", 30);

        // Accessing components
        int x = p.x();
        String name = user.name();

        // Records are immutable
        // p.x = 15; // Compile error

        // Records have automatic equals, hashCode, toString
        Point p2 = new Point(10, 20);
        boolean equal = p.equals(p2); // true
        String str = p.toString(); // "Point[x=10, y=20]"
    }
}
