// Java 21 generics

import java.util.*;

public class Generics {

    // Generic class
    public static class Box<T> {
        private T value;

        public Box(T value) {
            this.value = value;
        }

        public T getValue() {
            return value;
        }

        public void setValue(T value) {
            this.value = value;
        }
    }

    // Generic class with multiple type parameters
    public static class Pair<K, V> {
        private K key;
        private V value;

        public Pair(K key, V value) {
            this.key = key;
            this.value = value;
        }

        public K getKey() {
            return key;
        }

        public V getValue() {
            return value;
        }
    }

    // Generic method
    public static <T> T getFirst(List<T> list) {
        return list.isEmpty() ? null : list.get(0);
    }

    public static <T> void swap(T[] array, int i, int j) {
        T temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }

    // Bounded type parameters
    public static class NumberBox<T extends Number> {
        private T value;

        public NumberBox(T value) {
            this.value = value;
        }

        public double doubleValue() {
            return value.doubleValue();
        }
    }

    // Multiple bounds
    public static <T extends Comparable<T> & Cloneable> T max(T a, T b) {
        return a.compareTo(b) > 0 ? a : b;
    }

    // Wildcards
    public static void printList(List<?> list) {
        for (Object item : list) {
            System.out.println(item);
        }
    }

    // Upper bounded wildcard
    public static double sumNumbers(List<? extends Number> list) {
        double sum = 0.0;
        for (Number num : list) {
            sum += num.doubleValue();
        }
        return sum;
    }

    // Lower bounded wildcard
    public static void addIntegers(List<? super Integer> list) {
        list.add(1);
        list.add(2);
        list.add(3);
    }

    // Generic interface
    public interface Container<T> {
        void add(T item);
        T get(int index);
        int size();
    }

    // Implementation of generic interface
    public static class SimpleContainer<T> implements Container<T> {
        private List<T> items = new ArrayList<>();

        @Override
        public void add(T item) {
            items.add(item);
        }

        @Override
        public T get(int index) {
            return items.get(index);
        }

        @Override
        public int size() {
            return items.size();
        }
    }

    // Recursive type bound
    public static class Node<T extends Node<T>> {
        private T parent;

        public T getParent() {
            return parent;
        }

        public void setParent(T parent) {
            this.parent = parent;
        }
    }

    // Generic constructor
    public static class GenericConstructor {
        public <T> GenericConstructor(T value) {
            System.out.println("Value: " + value);
        }
    }

    public static void useGenerics() {
        // Using generic class
        Box<String> stringBox = new Box<>("Hello");
        Box<Integer> intBox = new Box<>(42);

        // Using generic method
        List<String> names = Arrays.asList("Alice", "Bob");
        String first = getFirst(names);

        // Using wildcards
        List<Integer> integers = Arrays.asList(1, 2, 3);
        printList(integers);

        double sum = sumNumbers(integers);
    }
}
