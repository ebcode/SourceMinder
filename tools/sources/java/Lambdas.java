// Java 21 lambda expressions

import java.util.*;
import java.util.function.*;

public class Lambdas {

    public static void basicLambdas() {
        // Simple lambda
        Runnable task = () -> System.out.println("Running");

        // Lambda with parameter
        Consumer<String> printer = (s) -> System.out.println(s);

        // Lambda with multiple parameters
        BiFunction<Integer, Integer, Integer> add = (a, b) -> a + b;

        // Lambda with block body
        Function<String, String> toUpperCase = (str) -> {
            return str.toUpperCase();
        };

        // Lambda with explicit type
        BiPredicate<String, String> equals = (String s1, String s2) -> s1.equals(s2);
    }

    public static void functionalInterfaces() {
        // Predicate
        Predicate<Integer> isEven = n -> n % 2 == 0;

        // Function
        Function<String, Integer> length = s -> s.length();

        // Supplier
        Supplier<Double> random = () -> Math.random();

        // Consumer
        Consumer<String> print = s -> System.out.println(s);

        // BiFunction
        BiFunction<Integer, Integer, Integer> multiply = (a, b) -> a * b;
    }

    public static void methodReferences() {
        // Static method reference
        Function<String, Integer> parser = Integer::parseInt;

        // Instance method reference
        String str = "hello";
        Supplier<String> upper = str::toUpperCase;

        // Constructor reference
        Supplier<ArrayList<String>> listMaker = ArrayList::new;
        Function<Integer, int[]> arrayMaker = int[]::new;

        // Arbitrary object method reference
        Function<String, String> trim = String::trim;
    }

    @FunctionalInterface
    interface Calculator {
        int calculate(int a, int b);
    }

    public static void customFunctionalInterface() {
        // Using custom functional interface
        Calculator add = (a, b) -> a + b;
        Calculator subtract = (a, b) -> a - b;
        Calculator multiply = (a, b) -> a * b;
        Calculator divide = (a, b) -> a / b;
    }

    public static void lambdaInCollections() {
        List<String> names = Arrays.asList("Alice", "Bob", "Charlie");

        // forEach with lambda
        names.forEach(name -> System.out.println(name));

        // sort with lambda
        names.sort((a, b) -> a.compareTo(b));

        // removeIf with lambda
        names.removeIf(name -> name.length() < 5);
    }

    public static void closures() {
        int factor = 2;

        // Lambda capturing variable
        Function<Integer, Integer> multiplier = n -> n * factor;

        // Effectively final variable capture
        String prefix = "Result: ";
        Consumer<Integer> printer = n -> System.out.println(prefix + n);
    }
}
