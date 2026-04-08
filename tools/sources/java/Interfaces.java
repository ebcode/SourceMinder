// Java 21 interface declarations and features

public class Interfaces {

    // Basic interface
    public interface Readable {
        String read();
    }

    // Interface with multiple methods
    public interface Storage {
        void save(String key, Object value);
        Object load(String key);
        void delete(String key);
    }

    // Interface with default methods
    public interface Logger {
        void log(String message);

        default void logError(String message) {
            log("ERROR: " + message);
        }

        default void logInfo(String message) {
            log("INFO: " + message);
        }
    }

    // Interface with static methods
    public interface MathUtils {
        static int add(int a, int b) {
            return a + b;
        }

        static int multiply(int a, int b) {
            return a * b;
        }
    }

    // Interface extending other interfaces
    public interface ReadWriteable extends Readable {
        void write(String data);
    }

    // Functional interface
    @FunctionalInterface
    public interface Calculator {
        int calculate(int a, int b);
    }

    // Private methods in interfaces (Java 9+)
    public interface Validator {
        default boolean isValid(String input) {
            return checkNotNull(input) && checkLength(input);
        }

        private boolean checkNotNull(String input) {
            return input != null;
        }

        private boolean checkLength(String input) {
            return input.length() > 0;
        }
    }

    // Implementation
    public static class FileStorage implements Storage {
        @Override
        public void save(String key, Object value) {
            // Implementation
        }

        @Override
        public Object load(String key) {
            return null;
        }

        @Override
        public void delete(String key) {
            // Implementation
        }
    }
}
