// SPDX-License-Identifier: MIT
package artbox;

// Test-input logic check on the host JDK. This is not ART execution evidence.
public final class RuntimeChecksHost {
    public static void main(String[] args) throws Exception {
        if (RuntimeChecks.heapAndDispatch() != 6496 || RuntimeChecks.exceptions() != 3)
            throw new AssertionError("managed fixture");
        Thread worker = new Thread(() -> {
            for (int i = 0; i < 10; ++i) RuntimeChecks.attachedThread("ARTBox-worker");
        }, "ARTBox-worker");
        worker.start();
        worker.join();
        if (RuntimeChecks.threadCalls() != 10) throw new AssertionError("thread fixture");
        System.out.println("Host JDK fixture logic passed; ART execution unverified");
    }
}
