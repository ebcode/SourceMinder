// Java 21 sealed classes (Java 17+)

public class SealedClasses {

    // Sealed class with permits clause
    public sealed class Shape permits Circle, Rectangle, Triangle {}

    public final class Circle extends Shape {
        private double radius;

        public Circle(double radius) {
            this.radius = radius;
        }

        public double area() {
            return Math.PI * radius * radius;
        }
    }

    public final class Rectangle extends Shape {
        private double width;
        private double height;

        public Rectangle(double width, double height) {
            this.width = width;
            this.height = height;
        }

        public double area() {
            return width * height;
        }
    }

    public non-sealed class Triangle extends Shape {
        private double base;
        private double height;

        public Triangle(double base, double height) {
            this.base = base;
            this.height = height;
        }

        public double area() {
            return 0.5 * base * height;
        }
    }

    // Sealed interface
    public sealed interface Vehicle permits Car, Truck, Motorcycle {}

    public final class Car implements Vehicle {
        private String model;

        public Car(String model) {
            this.model = model;
        }
    }

    public final class Truck implements Vehicle {
        private int capacity;

        public Truck(int capacity) {
            this.capacity = capacity;
        }
    }

    public final class Motorcycle implements Vehicle {
        private String type;

        public Motorcycle(String type) {
            this.type = type;
        }
    }

    // Sealed with records
    public sealed interface Result<T> permits Success, Failure {}

    public record Success<T>(T value) implements Result<T> {}

    public record Failure<T>(String error) implements Result<T> {}

    // Nested sealed hierarchy
    public sealed class Animal permits Mammal, Bird {}

    public sealed class Mammal extends Animal permits Dog, Cat {}

    public final class Dog extends Mammal {
        public void bark() {
            System.out.println("Woof!");
        }
    }

    public final class Cat extends Mammal {
        public void meow() {
            System.out.println("Meow!");
        }
    }

    public final class Bird extends Animal {
        public void chirp() {
            System.out.println("Chirp!");
        }
    }

    public static void useSealed(Shape shape) {
        // Sealed classes enable exhaustive pattern matching
        double area = switch (shape) {
            case Circle c -> c.area();
            case Rectangle r -> r.area();
            case Triangle t -> t.area();
            // No default needed - all cases covered
        };
    }
}
