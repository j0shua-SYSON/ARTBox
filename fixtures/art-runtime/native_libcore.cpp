// Original ARTBox dependency tests. MIT. No Java VM is started here.
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <string>
#include <sys/capability.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <expat.h>
#include <fdlibm.h>
#include <jvm.h>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <unicode/ustring.h>
#include <unicode/uversion.h>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d failed: %s\n", __LINE__, #value); return 1; } } while (0)

struct XmlState {
  int starts = 0;
  int ends = 0;
  std::string text;
};
static void XMLCALL start_element(void* opaque, const XML_Char*, const XML_Char**) {
  ++static_cast<XmlState*>(opaque)->starts;
}
static void XMLCALL end_element(void* opaque, const XML_Char*) {
  ++static_cast<XmlState*>(opaque)->ends;
}
static void XMLCALL characters(void* opaque, const XML_Char* text, int size) {
  static_cast<XmlState*>(opaque)->text.append(text, size);
}

int main(int argc, char** argv) {
  if (argc != 3) return 64;  // writable scratch directory, library directory
  int cases = 0;
  unsigned char hash[SHA_DIGEST_LENGTH];
  const unsigned char expected[] = {0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
                                    0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d};
  CHECK(SHA1(reinterpret_cast<const unsigned char*>("abc"), 3, hash) == hash);
  CHECK(std::memcmp(hash, expected, sizeof(hash)) == 0);
  ++cases;

  UVersionInfo version;
  u_getVersion(version);
  UErrorCode status = U_ZERO_ERROR;
  UChar replacement[2]{};
  int32_t length = 0, substitutions = 0;
  u_strFromUTF8WithSub(replacement, 2, &length, "\xff", 1, 0xfffd, &substitutions, &status);
  CHECK(version[0] == 75 && U_SUCCESS(status) && length == 1 && substitutions == 1 && replacement[0] == 0xfffd);
  ++cases;

  BN_CTX* context = BN_CTX_new();
  BIGNUM *base = BN_new(), *power = BN_new(), *modulus = BN_new(), *result = BN_new();
  CHECK(context && base && power && modulus && result);
  CHECK(BN_set_word(base, 2) && BN_set_word(power, 10) && BN_set_word(modulus, 17));
  CHECK(BN_mod_exp(result, base, power, modulus, context) && BN_get_word(result) == 4);
  BN_free(base); BN_free(power); BN_free(modulus); BN_free(result); BN_CTX_free(context);
  ++cases;

  CHECK(ieee_sqrt(4.0) == 2.0 && ieee_pow(2.0, 10.0) == 1024.0);
  ++cases;
  CHECK(ieee_sin(-0.0) == 0.0 && std::signbit(ieee_sin(-0.0)));
  ++cases;
  CHECK(std::isnan(ieee_sqrt(-1.0)));
  ++cases;
  CHECK(std::isinf(ieee_log(0.0)) && std::signbit(ieee_log(0.0)));
  ++cases;
  CHECK(JVM_IsNaN(ieee_sqrt(-1.0)) && !JVM_IsNaN(1.0) && !JVM_IsNaN(ieee_log(0.0)));
  ++cases;

  __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0}, raw_header = header;
  __user_cap_data_struct capabilities[2]{}, raw_capabilities[2]{};
  CHECK(capget(&header, capabilities) == 0);
  CHECK(syscall(SYS_capget, &raw_header, raw_capabilities) == 0);
  CHECK(std::memcmp(capabilities, raw_capabilities, sizeof(capabilities)) == 0);
  ++cases;
  // Every capset request below is invalid; no test changes process privileges.
  for (bool set : {false, true}) {
    for (bool null_header : {false, true}) {
      header = {0, 0}; raw_header = header;
      errno = 0;
      const int forwarded = set ? capset(null_header ? nullptr : &header, capabilities)
                                : capget(null_header ? nullptr : &header, capabilities);
      const int forwarded_errno = errno;
      errno = 0;
      const long raw = syscall(set ? SYS_capset : SYS_capget,
                               null_header ? nullptr : &raw_header, raw_capabilities);
      CHECK(forwarded == -1 && raw == -1 && errno == forwarded_errno);
      CHECK(forwarded_errno == (null_header ? EFAULT : EINVAL));
      CHECK(header.version == raw_header.version && header.pid == raw_header.pid);
      ++cases;
    }
  }

  XmlState xml;
  XML_Parser parser = XML_ParserCreate(nullptr);
  CHECK(parser);
  XML_SetUserData(parser, &xml);
  XML_SetElementHandler(parser, start_element, end_element);
  XML_SetCharacterDataHandler(parser, characters);
  const char first[] = "<root><child>A&amp;";
  const char second[] = "B</child></root>";
  CHECK(XML_Parse(parser, first, sizeof(first) - 1, XML_FALSE) == XML_STATUS_OK);
  CHECK(XML_Parse(parser, second, sizeof(second) - 1, XML_TRUE) == XML_STATUS_OK);
  CHECK(xml.starts == 2 && xml.ends == 2 && xml.text == "A&B");
  XML_ParserFree(parser);
  ++cases;
  parser = XML_ParserCreate(nullptr);
  CHECK(parser);
  const char invalid[] = "<a></b>";
  CHECK(XML_Parse(parser, invalid, sizeof(invalid) - 1, XML_TRUE) == XML_STATUS_ERROR);
  CHECK(XML_GetErrorCode(parser) == XML_ERROR_TAG_MISMATCH);
  XML_ParserFree(parser);
  ++cases;

  std::string path = std::string(argv[1]) + "/jvm-file-XXXXXX";
  int temporary = mkstemp(path.data());
  CHECK(temporary >= 0 && close(temporary) == 0);
  CHECK(JVM_Open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600) == JVM_EEXIST);
  ++cases;
  int fd = JVM_Open(path.c_str(), O_RDWR, 0600);
  CHECK(fd >= 0);
  char message[] = "native libcore";
  CHECK(JVM_Write(fd, message, sizeof(message)) == sizeof(message));
  CHECK(JVM_Lseek(fd, 0, SEEK_SET) == 0);
  char received[sizeof(message)] = {};
  CHECK(JVM_Read(fd, received, sizeof(received)) == sizeof(received));
  CHECK(std::memcmp(message, received, sizeof(message)) == 0);
  CHECK(JVM_Close(fd) == 0 && unlink(path.c_str()) == 0);
  ++cases;

  void* monitor = JVM_RawMonitorCreate();
  CHECK(monitor);
  int total = 0;
  std::atomic<bool> lock_failed{false};
  auto increment = [&] {
    for (int i = 0; i < 1000; ++i) {
      if (JVM_RawMonitorEnter(monitor) != 0) { lock_failed = true; return; }
      ++total;
      JVM_RawMonitorExit(monitor);
    }
  };
  std::thread worker(increment);
  increment();
  worker.join();
  CHECK(!lock_failed && total == 2000);
  JVM_RawMonitorDestroy(monitor);
  ++cases;

  for (const char* name : {"libjavacore.so", "libopenjdk.so"}) {
    const std::string library_path = std::string(argv[2]) + "/" + name;
    void* library = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) std::fprintf(stderr, "%s\n", dlerror());
    CHECK(library && dlsym(library, "JNI_OnLoad"));
    if (std::strcmp(name, "libjavacore.so") == 0) {
      // Both JNI libraries define this cache with different class sets.
      // Upstream javacore's export map keeps its copy local to that library.
      CHECK(dlsym(library, "_ZN12JniConstants10InitializeEP7_JNIEnv") == nullptr);
      ++cases;
    }
    CHECK(dlclose(library) == 0);
    ++cases;
  }
  CHECK(cases == 21);
  std::printf("ARTBox native libcore dependencies: %d cases passed; no Java VM started\n", cases);
  return 0;
}
