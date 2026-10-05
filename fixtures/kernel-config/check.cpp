// Original ARTBox caller for the pinned AOSP parser. SPDX-License-Identifier: MIT
#include <vintf/KernelConfigParser.h>
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int32_t artbox_kernel_config_check(unsigned mutation) {
    if (mutation > 2) return -1;
    int32_t cases = 0;
#define CHECK(n, condition) do { if (!(condition)) return -(100 + (n)); ++cases; } while (0)
    using android::vintf::KernelConfigParser;
    KernelConfigParser relaxed(mutation != 1, mutation != 2);
    const std::string config = "CONFIG_ALPHA=y\n# CONFIG_BETA is not set\n  CONFIG_TEXT = value  # note\n";
    for (size_t i = 0; i < config.size(); ++i) CHECK(1, relaxed.process(config.data() + i, 1) == 0);
    CHECK(2, relaxed.finish() == 0);
    const auto &values = relaxed.configs();
    CHECK(3, values.count("CONFIG_ALPHA") == 1 && values.at("CONFIG_ALPHA") == "y");
    CHECK(4, values.count("CONFIG_BETA") == 1 && values.at("CONFIG_BETA") == "n");
    CHECK(5, values.count("CONFIG_TEXT") == 1 && values.at("CONFIG_TEXT") == "value");
    CHECK(6, values.size() == 3);
    CHECK(7, relaxed.error()->str().empty());
    KernelConfigParser strict;
    CHECK(8, strict.processAndFinish("CONFIG_NO_NEWLINE=17") == 0);
    CHECK(9, strict.configs().at("CONFIG_NO_NEWLINE") == "17");
    CHECK(10, strict.processAndFinish("CONFIG_NO_NEWLINE=18\n") != 0);
    CHECK(11, strict.configs().at("CONFIG_NO_NEWLINE") == "17");
    CHECK(12, strict.error()->str().find("Duplicated key in configs: CONFIG_NO_NEWLINE") != std::string::npos);
    KernelConfigParser recover(true, true);
    CHECK(13, recover.processAndFinish("bad line\nCONFIG_AFTER=m\n") != 0);
    CHECK(14, recover.configs().at("CONFIG_AFTER") == "m");
    CHECK(15, recover.error()->str().find("Unrecognized line in configs: bad line") != std::string::npos);
    KernelConfigParser ignore_comments;
    CHECK(16, ignore_comments.processAndFinish("# CONFIG_ABSENT is not set\n") == 0);
    CHECK(17, ignore_comments.configs().empty());
    KernelConfigParser contradiction(true, true);
    CHECK(18, contradiction.processAndFinish("CONFIG_ON=y\n# CONFIG_ON is not set\n") != 0);
    CHECK(19, contradiction.configs().at("CONFIG_ON") == "y");
    CHECK(20, contradiction.error()->str().find("is set but commented as not set") != std::string::npos);
    return cases;
#undef CHECK
}
