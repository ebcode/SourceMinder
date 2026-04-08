// Java 21 collections framework

import java.util.*;

public class Collections {

    public static void lists() {
        // ArrayList
        List<String> arrayList = new ArrayList<>();
        arrayList.add("Alice");
        arrayList.add("Bob");
        arrayList.add("Charlie");

        // LinkedList
        List<Integer> linkedList = new LinkedList<>();
        linkedList.add(1);
        linkedList.add(2);

        // Immutable list (Java 9+)
        List<String> immutable = List.of("One", "Two", "Three");

        // List operations
        String first = arrayList.get(0);
        arrayList.set(1, "Bobby");
        arrayList.remove("Charlie");
        boolean contains = arrayList.contains("Alice");
        int size = arrayList.size();
    }

    public static void sets() {
        // HashSet
        Set<String> hashSet = new HashSet<>();
        hashSet.add("Apple");
        hashSet.add("Banana");
        hashSet.add("Cherry");

        // TreeSet (sorted)
        Set<Integer> treeSet = new TreeSet<>();
        treeSet.add(3);
        treeSet.add(1);
        treeSet.add(2);

        // LinkedHashSet (insertion order)
        Set<String> linkedHashSet = new LinkedHashSet<>();

        // Immutable set (Java 9+)
        Set<String> immutable = Set.of("A", "B", "C");

        // Set operations
        hashSet.remove("Banana");
        boolean contains = hashSet.contains("Apple");
    }

    public static void maps() {
        // HashMap
        Map<String, Integer> hashMap = new HashMap<>();
        hashMap.put("Alice", 30);
        hashMap.put("Bob", 25);
        hashMap.put("Charlie", 35);

        // TreeMap (sorted by key)
        Map<String, String> treeMap = new TreeMap<>();

        // LinkedHashMap (insertion order)
        Map<Integer, String> linkedHashMap = new LinkedHashMap<>();

        // Immutable map (Java 9+)
        Map<String, Integer> immutable = Map.of(
            "Alice", 30,
            "Bob", 25,
            "Charlie", 35
        );

        // Map operations
        Integer age = hashMap.get("Alice");
        hashMap.remove("Bob");
        boolean hasKey = hashMap.containsKey("Alice");
        boolean hasValue = hashMap.containsValue(30);

        // Iteration
        for (Map.Entry<String, Integer> entry : hashMap.entrySet()) {
            String key = entry.getKey();
            Integer value = entry.getValue();
        }

        // Java 8+ operations
        hashMap.forEach((k, v) -> System.out.println(k + ": " + v));
        hashMap.putIfAbsent("David", 40);
        hashMap.computeIfAbsent("Eve", k -> k.length());
        hashMap.merge("Alice", 1, Integer::sum);
    }

    public static void queues() {
        // LinkedList as Queue
        Queue<String> queue = new LinkedList<>();
        queue.offer("First");
        queue.offer("Second");
        queue.offer("Third");
        String head = queue.poll();

        // PriorityQueue
        Queue<Integer> priorityQueue = new PriorityQueue<>();
        priorityQueue.offer(3);
        priorityQueue.offer(1);
        priorityQueue.offer(2);

        // Deque (double-ended queue)
        Deque<String> deque = new ArrayDeque<>();
        deque.addFirst("First");
        deque.addLast("Last");
        String first = deque.removeFirst();
        String last = deque.removeLast();
    }

    public static void iteration() {
        List<String> names = Arrays.asList("Alice", "Bob", "Charlie");

        // For-each loop
        for (String name : names) {
            System.out.println(name);
        }

        // Iterator
        Iterator<String> iterator = names.iterator();
        while (iterator.hasNext()) {
            String name = iterator.next();
            System.out.println(name);
        }

        // ListIterator (bidirectional)
        ListIterator<String> listIterator = names.listIterator();
        while (listIterator.hasNext()) {
            String name = listIterator.next();
        }
    }

    public static void utilityMethods() {
        List<Integer> numbers = new ArrayList<>(Arrays.asList(3, 1, 4, 1, 5, 9));

        // Sort
        java.util.Collections.sort(numbers);

        // Reverse
        java.util.Collections.reverse(numbers);

        // Shuffle
        java.util.Collections.shuffle(numbers);

        // Binary search (requires sorted list)
        int index = java.util.Collections.binarySearch(numbers, 4);

        // Min and max
        Integer min = java.util.Collections.min(numbers);
        Integer max = java.util.Collections.max(numbers);

        // Frequency
        int freq = java.util.Collections.frequency(numbers, 1);
    }
}
