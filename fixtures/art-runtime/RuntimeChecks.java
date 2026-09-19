// SPDX-License-Identifier: MIT
package artbox;

public final class RuntimeChecks {
    private static int threadCalls;

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static class Base {
        int value() { return -1; }
    }

    private static final class Node extends Base {
        final int id;
        final byte[] bytes;
        Node next;
        Node(int value) {
            id = value;
            bytes = new byte[256];
            bytes[0] = (byte) value;
            bytes[255] = (byte) (value ^ 0x5a);
        }
        @Override int value() { return id * 3 + 7; }
    }

    public static int heapAndDispatch() {
        Node[] roots = new Node[64];
        for (int i = 0; i < roots.length; ++i) roots[i] = new Node(i);
        for (int i = 0; i < roots.length; ++i) roots[i].next = roots[(i + 1) % roots.length];
        Node saved = roots[17];
        // System.gc() may defer on Android target SDK <= 34.
        Runtime.getRuntime().gc();
        int total = 0;
        for (int i = 0; i < roots.length; ++i) {
            Node node = roots[i];
            require(node.id == i, "GC integer field");
            require(node.bytes.length == 256 && node.bytes[0] == (byte) i &&
                    node.bytes[255] == (byte) (i ^ 0x5a), "GC array contents");
            require(node.next == roots[(i + 1) % roots.length], "GC cyclic references");
            Base base = node;
            total += base.value();
        }
        require(saved == roots[17], "GC retained identity");
        require(total == 6496, "virtual method dispatch");
        return total;
    }

    public static int exceptions() {
        int caught = 0;
        try {
            Object missing = null;
            missing.toString();
        } catch (NullPointerException expected) {
            caught |= 1;
        }
        try {
            int[] values = new int[2];
            int ignored = values[values.length];
            throw new AssertionError(ignored);
        } catch (ArrayIndexOutOfBoundsException expected) {
            caught |= 2;
        }
        require(caught == 3, "null and bounds exceptions");
        return caught;
    }

    public static synchronized int attachedThread(String expectedName) {
        require(Thread.currentThread().getName().equals(expectedName), "attached thread name");
        return ++threadCalls;
    }

    public static synchronized int threadCalls() { return threadCalls; }
}
