// Java 21 exception handling

import java.io.*;

public class Exceptions {

    // Basic try-catch
    public static void basicException() {
        try {
            int result = 10 / 0;
        } catch (ArithmeticException e) {
            System.out.println("Division by zero");
        }
    }

    // Multiple catch blocks
    public static void multipleCatch() {
        try {
            String str = null;
            int length = str.length();
        } catch (NullPointerException e) {
            System.out.println("Null pointer");
        } catch (IllegalArgumentException e) {
            System.out.println("Illegal argument");
        }
    }

    // Multi-catch (Java 7+)
    public static void multiCatch() {
        try {
            // Some code
        } catch (IOException | SQLException e) {
            System.out.println("IO or SQL exception");
        }
    }

    // Try-with-finally
    public static void withFinally() {
        try {
            riskyOperation();
        } catch (Exception e) {
            System.out.println("Error: " + e.getMessage());
        } finally {
            System.out.println("Always executed");
        }
    }

    // Try-with-resources (Java 7+)
    public static void tryWithResources() {
        try (BufferedReader reader = new BufferedReader(new FileReader("file.txt"))) {
            String line = reader.readLine();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    // Multiple resources in try-with-resources
    public static void multipleResources() {
        try (FileInputStream fis = new FileInputStream("input.txt");
             FileOutputStream fos = new FileOutputStream("output.txt")) {
            // Use streams
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    // Custom exception
    public static class ValidationException extends Exception {
        public ValidationException(String message) {
            super(message);
        }

        public ValidationException(String message, Throwable cause) {
            super(message, cause);
        }
    }

    // Custom runtime exception
    public static class ConfigurationException extends RuntimeException {
        public ConfigurationException(String message) {
            super(message);
        }
    }

    // Throwing exceptions
    public static void validateAge(int age) throws ValidationException {
        if (age < 0) {
            throw new ValidationException("Age cannot be negative");
        }
        if (age > 150) {
            throw new ValidationException("Age is too high");
        }
    }

    // Rethrowing exceptions
    public static void rethrowExample() throws IOException {
        try {
            readFile();
        } catch (IOException e) {
            System.out.println("Logging error");
            throw e; // Rethrow
        }
    }

    // Exception chaining
    public static void chainedException() {
        try {
            throw new IOException("Original error");
        } catch (IOException e) {
            throw new RuntimeException("Wrapped error", e);
        }
    }

    // Suppressed exceptions (try-with-resources)
    public static void suppressedExceptions() {
        try (Resource r = new Resource()) {
            throw new Exception("Primary exception");
        } catch (Exception e) {
            Throwable[] suppressed = e.getSuppressed();
        }
    }

    static class Resource implements AutoCloseable {
        @Override
        public void close() throws Exception {
            throw new Exception("Close exception");
        }
    }

    // Checked vs unchecked
    public static void checkedVsUnchecked() throws IOException {
        // Checked exception - must be declared or caught
        throw new IOException("Checked");

        // Unchecked exception - no declaration needed
        // throw new RuntimeException("Unchecked");
    }

    // Finally without catch
    public static void finallyWithoutCatch() {
        try {
            riskyOperation();
        } finally {
            cleanup();
        }
    }

    // Nested try-catch
    public static void nestedTryCatch() {
        try {
            try {
                int result = 10 / 0;
            } catch (ArithmeticException e) {
                throw new RuntimeException("Converted", e);
            }
        } catch (RuntimeException e) {
            System.out.println("Outer catch");
        }
    }

    private static void riskyOperation() throws Exception {
        throw new Exception("Something went wrong");
    }

    private static void readFile() throws IOException {
        throw new IOException("File not found");
    }

    private static void cleanup() {
        System.out.println("Cleaning up");
    }
}
