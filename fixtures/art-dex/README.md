# Original DEX loader fixture

`scripts/dex_fixture.py` emits one fixed, original DEX 035 class with a public
static method equivalent to:

```java
package artbox;
public class Hello {
    public static String message() { return "hello from ARTBox ART"; }
}
```

This is hand-encoded test data, not javac output or a general DEX compiler.
The encoded class has only that static method, with `const-string` and
`return-object` instructions. The generator emits sorted identifier tables,
class data, a map list, SHA-1 signature and Adler-32 checksum. AOSP's verifier
and accessors provide the independent format check.

`check.cpp` uses the normal pinned AOSP `DexFileLoader` API with both structural
and checksum verification enabled, then checks the class, method, signature,
instruction data and string constant. It reports `dex_executed: false`:
reading a constant from a verified file does not execute its method. The shared
entry is a diagnostic intended for one invocation per process. ART runtime
startup, class loading, invocation, collection and JNI remain later work.

The fixture and generator are original MIT code. Compiled AOSP components keep
their own reviewed notices as documented in `THIRD_PARTY.md`.
