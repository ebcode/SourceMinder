// Java 21 annotations

import java.lang.annotation.*;

public class Annotations {

    // Basic annotation
    @interface MyAnnotation {}

    // Annotation with elements
    @interface Author {
        String name();
        String date();
    }

    // Annotation with default values
    @interface Version {
        int major() default 1;
        int minor() default 0;
        String description() default "";
    }

    // Marker annotation
    @interface Deprecated {}

    // Single-value annotation
    @interface Priority {
        int value();
    }

    // Retention policy
    @Retention(RetentionPolicy.RUNTIME)
    @interface RuntimeAnnotation {}

    @Retention(RetentionPolicy.CLASS)
    @interface ClassAnnotation {}

    @Retention(RetentionPolicy.SOURCE)
    @interface SourceAnnotation {}

    // Target specification
    @Target(ElementType.METHOD)
    @interface MethodOnly {}

    @Target(ElementType.FIELD)
    @interface FieldOnly {}

    @Target({ElementType.TYPE, ElementType.METHOD})
    @interface TypeAndMethod {}

    // Repeatable annotation (Java 8+)
    @Repeatable(Schedules.class)
    @interface Schedule {
        String day();
        String time();
    }

    @interface Schedules {
        Schedule[] value();
    }

    // Inherited annotation
    @Inherited
    @interface InheritedAnnotation {}

    // Documented annotation
    @Documented
    @interface DocumentedAnnotation {}

    // Using annotations on class
    @Author(name = "Alice", date = "2024-01-01")
    @Version(major = 2, minor = 1, description = "Updated version")
    public static class AnnotatedClass {

        // Field annotation
        @FieldOnly
        private String name;

        // Method annotation
        @MethodOnly
        public void doSomething() {}

        // Multiple annotations
        @Override
        @Deprecated
        public String toString() {
            return "AnnotatedClass";
        }

        // Annotation with single value
        @Priority(5)
        public void highPriorityTask() {}

        // Repeatable annotations
        @Schedule(day = "Monday", time = "9:00")
        @Schedule(day = "Wednesday", time = "14:00")
        @Schedule(day = "Friday", time = "16:00")
        public void meetingTimes() {}
    }

    // Type annotations (Java 8+)
    public static void typeAnnotations() {
        @interface NonNull {}
        @interface Readonly {}

        // On type use
        @NonNull String str = "hello";

        // On generic type arguments
        List<@NonNull String> names = new ArrayList<>();

        // On casts
        String s = (@NonNull String) obj;
    }

    // Standard Java annotations
    public static class StandardAnnotations {

        @Override
        public String toString() {
            return "example";
        }

        @Deprecated
        public void oldMethod() {}

        @SuppressWarnings("unchecked")
        public void unsafeOperation() {
            List raw = new ArrayList();
        }

        @FunctionalInterface
        interface Calculator {
            int calculate(int a, int b);
        }

        @SafeVarargs
        public final <T> void printAll(T... items) {
            for (T item : items) {
                System.out.println(item);
            }
        }
    }

    private static Object obj;
}
