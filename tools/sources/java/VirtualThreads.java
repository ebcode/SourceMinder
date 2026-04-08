// Java 21 virtual threads (Project Loom)

import java.util.concurrent.*;
import java.time.Duration;

public class VirtualThreads {

    public static void basicVirtualThread() throws InterruptedException {
        // Create and start a virtual thread
        Thread vThread = Thread.ofVirtual().start(() -> {
            System.out.println("Hello from virtual thread");
        });

        vThread.join();
    }

    public static void virtualThreadFactory() {
        // Create virtual thread factory
        ThreadFactory factory = Thread.ofVirtual().factory();

        Thread thread = factory.newThread(() -> {
            System.out.println("Running in virtual thread");
        });

        thread.start();
    }

    public static void virtualThreadBuilder() throws InterruptedException {
        // Build virtual thread with name
        Thread thread = Thread.ofVirtual()
            .name("worker-virtual")
            .start(() -> {
                System.out.println("Named virtual thread");
            });

        thread.join();
    }

    public static void executorService() throws InterruptedException {
        // Virtual thread executor
        try (ExecutorService executor = Executors.newVirtualThreadPerTaskExecutor()) {
            // Submit tasks
            Future<String> future = executor.submit(() -> {
                Thread.sleep(1000);
                return "Task result";
            });

            // Multiple tasks
            for (int i = 0; i < 100; i++) {
                int taskNum = i;
                executor.submit(() -> {
                    System.out.println("Task " + taskNum);
                });
            }
        }
    }

    public static void structuredConcurrency() throws InterruptedException, ExecutionException {
        // Structured task scope (preview feature)
        try (var scope = new StructuredTaskScope.ShutdownOnFailure()) {
            Future<String> task1 = scope.fork(() -> fetchUser());
            Future<String> task2 = scope.fork(() -> fetchOrders());

            scope.join();
            scope.throwIfFailed();

            String user = task1.resultNow();
            String orders = task2.resultNow();
        }
    }

    private static String fetchUser() throws InterruptedException {
        Thread.sleep(100);
        return "User data";
    }

    private static String fetchOrders() throws InterruptedException {
        Thread.sleep(200);
        return "Order data";
    }

    public static void scopedValues() {
        // Scoped values (preview feature)
        ScopedValue<String> USER = ScopedValue.newInstance();

        ScopedValue.where(USER, "Alice").run(() -> {
            String user = USER.get();
            System.out.println("User: " + user);
        });
    }

    public static void platformVsVirtual() {
        // Platform thread (traditional)
        Thread platformThread = Thread.ofPlatform().start(() -> {
            System.out.println("Platform thread");
        });

        // Virtual thread (lightweight)
        Thread virtualThread = Thread.ofVirtual().start(() -> {
            System.out.println("Virtual thread");
        });
    }

    public static void massiveConcurrency() {
        // Create millions of virtual threads easily
        try (ExecutorService executor = Executors.newVirtualThreadPerTaskExecutor()) {
            for (int i = 0; i < 1_000_000; i++) {
                int taskId = i;
                executor.submit(() -> {
                    try {
                        Thread.sleep(Duration.ofSeconds(1));
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                    }
                });
            }
        }
    }
}
