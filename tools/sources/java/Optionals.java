// Java 21 Optional class (Java 8+)

import java.util.Optional;

public class Optionals {

    // Creating Optionals
    public static void creatingOptionals() {
        // Empty Optional
        Optional<String> empty = Optional.empty();

        // Optional with value
        Optional<String> opt = Optional.of("Hello");

        // Optional with nullable value
        String nullable = null;
        Optional<String> optNullable = Optional.ofNullable(nullable);
    }

    // Checking presence
    public static void checkingPresence() {
        Optional<String> opt = Optional.of("Hello");

        // isPresent
        if (opt.isPresent()) {
            System.out.println("Value present");
        }

        // isEmpty (Java 11+)
        if (opt.isEmpty()) {
            System.out.println("Value absent");
        }
    }

    // Getting values
    public static void gettingValues() {
        Optional<String> opt = Optional.of("Hello");

        // get (throws if empty)
        String value = opt.get();

        // orElse (default value)
        String result = opt.orElse("default");

        // orElseGet (lazy default)
        String lazy = opt.orElseGet(() -> computeDefault());

        // orElseThrow
        String required = opt.orElseThrow(() -> new IllegalStateException("Missing"));
    }

    // Transforming values
    public static void transformingValues() {
        Optional<String> opt = Optional.of("hello");

        // map
        Optional<Integer> length = opt.map(String::length);
        Optional<String> upper = opt.map(String::toUpperCase);

        // flatMap
        Optional<String> result = opt.flatMap(s -> Optional.of(s.toUpperCase()));
    }

    // Filtering
    public static void filtering() {
        Optional<String> opt = Optional.of("hello");

        // filter
        Optional<String> filtered = opt.filter(s -> s.length() > 3);
        Optional<String> empty = opt.filter(s -> s.length() > 10);
    }

    // Consuming values
    public static void consumingValues() {
        Optional<String> opt = Optional.of("Hello");

        // ifPresent
        opt.ifPresent(s -> System.out.println(s));

        // ifPresentOrElse (Java 9+)
        opt.ifPresentOrElse(
            s -> System.out.println("Present: " + s),
            () -> System.out.println("Absent")
        );
    }

    // Chaining operations
    public static Optional<String> findUser(int id) {
        return id > 0 ? Optional.of("User" + id) : Optional.empty();
    }

    public static void chainingOperations() {
        Optional<String> result = findUser(1)
            .map(String::toUpperCase)
            .filter(s -> s.length() > 5)
            .map(s -> "Found: " + s);
    }

    // Optional with streams (Java 9+)
    public static void optionalStreams() {
        Optional<String> opt = Optional.of("hello");

        // stream
        opt.stream()
            .map(String::toUpperCase)
            .forEach(System.out::println);
    }

    // or() method (Java 9+)
    public static void orMethod() {
        Optional<String> opt = Optional.empty();

        // or (lazy alternative)
        Optional<String> alternative = opt.or(() -> Optional.of("alternative"));
    }

    // Practical example
    public static class User {
        private String name;
        private Optional<String> email;
        private Optional<String> phone;

        public User(String name) {
            this.name = name;
            this.email = Optional.empty();
            this.phone = Optional.empty();
        }

        public Optional<String> getEmail() {
            return email;
        }

        public void setEmail(String email) {
            this.email = Optional.ofNullable(email);
        }

        public Optional<String> getPhone() {
            return phone;
        }
    }

    public static void practicalExample() {
        User user = new User("Alice");

        // Get email or default
        String email = user.getEmail().orElse("no-email@example.com");

        // Transform if present
        user.getEmail()
            .map(String::toUpperCase)
            .ifPresent(e -> System.out.println("Email: " + e));

        // Chain operations
        String contact = user.getEmail()
            .or(() -> user.getPhone())
            .orElse("No contact information");
    }

    private static String computeDefault() {
        return "computed default";
    }
}
