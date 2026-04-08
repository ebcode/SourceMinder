// Java 21 var keyword (Java 10+)

import java.util.*;

public class VarKeyword {

    public static void basicVar() {
        // Local variable type inference
        var message = "Hello, World!"; // String
        var count = 42; // int
        var price = 19.99; // double
        var active = true; // boolean
    }

    public static void collections() {
        // With collections
        var names = new ArrayList<String>(); // ArrayList<String>
        var ages = new HashMap<String, Integer>(); // HashMap<String, Integer>
        var numbers = List.of(1, 2, 3, 4, 5); // List<Integer>
    }

    public static void loops() {
        var numbers = List.of(1, 2, 3, 4, 5);

        // Enhanced for loop
        for (var num : numbers) {
            System.out.println(num);
        }

        // Traditional for loop
        for (var i = 0; i < 10; i++) {
            System.out.println(i);
        }

        // Iterator
        for (var it = numbers.iterator(); it.hasNext(); ) {
            var value = it.next();
            System.out.println(value);
        }
    }

    public static void tryWithResources() {
        // Try-with-resources
        try (var reader = new java.io.BufferedReader(
                new java.io.FileReader("file.txt"))) {
            var line = reader.readLine();
        } catch (java.io.IOException e) {
            e.printStackTrace();
        }
    }

    public static void lambdas() {
        // Cannot use var for lambda parameters (Java 10)
        // But can use in Java 11+ with annotations
        var list = List.of(1, 2, 3, 4, 5);

        // Var in lambda (Java 11+)
        list.forEach((var item) -> System.out.println(item));

        // Multiple parameters
        var map = Map.of("a", 1, "b", 2);
        map.forEach((var key, var value) ->
            System.out.println(key + ": " + value)
        );
    }

    public static void generics() {
        // With generics
        var map = new HashMap<String, List<Integer>>();
        map.put("numbers", List.of(1, 2, 3));

        // Diamond operator
        var list = new ArrayList<String>();
    }

    public static void limitations() {
        // Cannot use var without initializer
        // var x; // Compile error

        // Cannot use var with null
        // var y = null; // Compile error

        // Cannot use var for fields
        // var field = 10; // Compile error in class body

        // Cannot use var for method parameters
        // public void method(var param) {} // Compile error

        // Cannot use var for method return type
        // public var method() { return 10; } // Compile error

        // Must have initializer
        var name = "Alice"; // OK
    }

    public static void arrays() {
        // Array with var
        var numbers = new int[]{1, 2, 3, 4, 5}; // int[]
        var matrix = new int[][]{{1, 2}, {3, 4}}; // int[][]

        // Array iteration
        for (var num : numbers) {
            System.out.println(num);
        }
    }

    public static void methodChaining() {
        // With method chaining
        var result = "hello"
            .toUpperCase()
            .replace("H", "J")
            .substring(0, 3);
    }

    public static void anonymousClasses() {
        // With anonymous classes
        var runnable = new Runnable() {
            @Override
            public void run() {
                System.out.println("Running");
            }
        };
    }

    public static void casting() {
        // Explicit cast when needed
        Object obj = "hello";
        var str = (String) obj;

        // Pattern matching for instanceof (Java 16+)
        if (obj instanceof String s) {
            var length = s.length();
        }
    }

    // Practical example
    public static void practicalExample() {
        var users = new HashMap<String, Integer>();
        users.put("Alice", 30);
        users.put("Bob", 25);
        users.put("Charlie", 35);

        for (var entry : users.entrySet()) {
            var name = entry.getKey();
            var age = entry.getValue();
            System.out.println(name + " is " + age + " years old");
        }

        var names = users.keySet().stream()
            .filter(name -> users.get(name) > 26)
            .toList();
    }
}
