// Original ARTBox native dependency check. MIT; no Java VM is started here.
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <unicode/uclean.h>
#include <unicode/ucol.h>
#include <unicode/uregex.h>
#include <unicode/ustring.h>
#include <unicode/uversion.h>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d failed: %s\n", __LINE__, #value); return 1; } } while (0)

int main(int argc, char** argv) {
  if (argc != 2) return 64;
  UVersionInfo version;
  u_getVersion(version);
  CHECK(version[0] == 75);
  UErrorCode status = U_ZERO_ERROR;
  u_init(&status);
  CHECK(U_SUCCESS(status));

  UChar text[16];
  int32_t length = 0;
  u_strFromUTF8(text, 16, &length, "A\xc3\xa9\xf0\x9f\x98\x80", -1, &status);
  CHECK(U_SUCCESS(status) && length == 4 && text[1] == 0x00e9 &&
        text[2] == 0xd83d && text[3] == 0xde00);
  char bytes[32];
  u_strToUTF8(bytes, 32, &length, text, -1, &status);
  CHECK(U_SUCCESS(status) && std::strcmp(bytes, "A\xc3\xa9\xf0\x9f\x98\x80") == 0);

  u_strFromUTF8(text, 16, &length, "\xff", 1, &status);
  CHECK(status == U_INVALID_CHAR_FOUND);
  status = U_ZERO_ERROR;
  const UChar lower[] = {'i', 0};
  length = u_strToUpper(text, 16, lower, -1, "tr", &status);
  CHECK(U_SUCCESS(status) && length == 1 && text[0] == 0x0130);

  UCollator* collator = ucol_open("en_US", &status);
  CHECK(U_SUCCESS(status) && collator != nullptr);
  CHECK(ucol_strcollUTF8(collator, "a", 1, "b", 1, &status) == UCOL_LESS && U_SUCCESS(status));
  ucol_close(collator);

  URegularExpression* expression = uregex_openC("a+b", 0, nullptr, &status);
  CHECK(U_SUCCESS(status) && expression != nullptr);
  const UChar input[] = {'a', 'a', 'a', 'b', 0};
  uregex_setText(expression, input, -1, &status);
  CHECK(uregex_matches(expression, 0, &status) && U_SUCCESS(status));
  uregex_close(expression);

  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (library == nullptr) std::fprintf(stderr, "%s\n", dlerror());
  CHECK(library != nullptr && dlsym(library, "JNI_OnLoad") != nullptr);
  CHECK(dlclose(library) == 0);
  std::puts("ARTBox native ICU and JNI dependencies: 8 cases passed; no ART execution");
  return 0;
}
