// Java 21 class declarations and features

public class Classes {

    // Basic class with fields
    public static class User {
        private String name;
        private String email;
        private int age;

        public User(String name, String email, int age) {
            this.name = name;
            this.email = email;
            this.age = age;
        }

        public String getName() {
            return name;
        }

        public void setName(String name) {
            this.name = name;
        }
    }

    // Class with static members
    public static class Counter {
        private static int totalCount = 0;
        private int count;

        public Counter() {
            count = 0;
            totalCount++;
        }

        public static int getTotalCount() {
            return totalCount;
        }

        public void increment() {
            count++;
        }
    }

    // Inheritance
    public static abstract class Animal {
        protected String name;

        public Animal(String name) {
            this.name = name;
        }

        public abstract String speak();
    }

    public static class Dog extends Animal {
        public Dog(String name) {
            super(name);
        }

        @Override
        public String speak() {
            return name + " says Woof!";
        }
    }

    // Inner class
    public static class Outer {
        private int value = 10;

        public class Inner {
            public int getValue() {
                return value;
            }
        }
    }

    // Static nested class
    public static class Container {
        public static class Node {
            private int data;

            public Node(int data) {
                this.data = data;
            }
        }
    }
}
