// Java 21 Stream API

import java.util.*;
import java.util.stream.*;

public class Streams {

    public static void basicStreams() {
        List<Integer> numbers = Arrays.asList(1, 2, 3, 4, 5);

        // Filter
        List<Integer> evens = numbers.stream()
            .filter(n -> n % 2 == 0)
            .collect(Collectors.toList());

        // Map
        List<Integer> squared = numbers.stream()
            .map(n -> n * n)
            .collect(Collectors.toList());

        // Reduce
        int sum = numbers.stream()
            .reduce(0, (a, b) -> a + b);

        // forEach
        numbers.stream().forEach(System.out::println);
    }

    public static void intermediateOperations() {
        List<String> names = Arrays.asList("Alice", "Bob", "Charlie", "David");

        // filter, map, sorted
        List<String> result = names.stream()
            .filter(name -> name.length() > 3)
            .map(String::toUpperCase)
            .sorted()
            .collect(Collectors.toList());

        // distinct
        List<Integer> unique = Arrays.asList(1, 2, 2, 3, 3, 4).stream()
            .distinct()
            .collect(Collectors.toList());

        // limit and skip
        List<Integer> limited = Stream.iterate(0, n -> n + 1)
            .limit(10)
            .skip(5)
            .collect(Collectors.toList());
    }

    public static void terminalOperations() {
        List<Integer> numbers = Arrays.asList(1, 2, 3, 4, 5);

        // count
        long count = numbers.stream().count();

        // anyMatch, allMatch, noneMatch
        boolean hasEven = numbers.stream().anyMatch(n -> n % 2 == 0);
        boolean allPositive = numbers.stream().allMatch(n -> n > 0);
        boolean noneNegative = numbers.stream().noneMatch(n -> n < 0);

        // findFirst, findAny
        Optional<Integer> first = numbers.stream().findFirst();
        Optional<Integer> any = numbers.stream().findAny();

        // min, max
        Optional<Integer> min = numbers.stream().min(Integer::compareTo);
        Optional<Integer> max = numbers.stream().max(Integer::compareTo);
    }

    public static void collectors() {
        List<String> names = Arrays.asList("Alice", "Bob", "Charlie");

        // toList
        List<String> list = names.stream().collect(Collectors.toList());

        // toSet
        Set<String> set = names.stream().collect(Collectors.toSet());

        // joining
        String joined = names.stream().collect(Collectors.joining(", "));

        // groupingBy
        Map<Integer, List<String>> byLength = names.stream()
            .collect(Collectors.groupingBy(String::length));

        // partitioningBy
        Map<Boolean, List<String>> partitioned = names.stream()
            .collect(Collectors.partitioningBy(s -> s.length() > 3));
    }

    public static void parallelStreams() {
        List<Integer> numbers = Arrays.asList(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);

        // Parallel stream
        int sum = numbers.parallelStream()
            .mapToInt(Integer::intValue)
            .sum();
    }

    public static void primitiveStreams() {
        // IntStream
        IntStream.range(1, 10)
            .forEach(System.out::println);

        // LongStream
        LongStream.of(1L, 2L, 3L, 4L, 5L)
            .sum();

        // DoubleStream
        double avg = DoubleStream.of(1.0, 2.0, 3.0)
            .average()
            .orElse(0.0);
    }

    public static void flatMap() {
        List<List<Integer>> nested = Arrays.asList(
            Arrays.asList(1, 2),
            Arrays.asList(3, 4),
            Arrays.asList(5, 6)
        );

        // Flatten nested lists
        List<Integer> flattened = nested.stream()
            .flatMap(List::stream)
            .collect(Collectors.toList());
    }
}
