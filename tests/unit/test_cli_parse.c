#include "../../ssos/os/app/cli_parse.h"
#include "ssos_test.h"

TEST(cli_background_spacing) {
    const char* forms[] = {"run counter&", "run counter &", "run counter&   ",
                           "\t run\tcounter\t&\t"};
    for (unsigned i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
        char line[64];
        strcpy(line, forms[i]);
        char* tokens[4];
        int background = 0;
        ASSERT_EQ(ss_cli_parse_line(line, tokens, 4, &background), 2);
        ASSERT_TRUE(background);
        ASSERT_STR_EQ(tokens[0], "run");
        ASSERT_STR_EQ(tokens[1], "counter");
    }
}

TEST(cli_background_all_commands) {
    const char* forms[] = {"ps&",     "modules&", "mem&",  "help&",
                           "wait 2&", "kill 2&",  "exit&", "other arg&"};
    for (unsigned i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
        char line[64];
        strcpy(line, forms[i]);
        char* tokens[4];
        int background = 0;
        int16_t count = ss_cli_parse_line(line, tokens, 4, &background);
        ASSERT_TRUE(count > 0);
        ASSERT_TRUE(background);
        for (int16_t n = 0; n < count; n++) ASSERT_NULL(strchr(tokens[n], '&'));
    }
}

TEST(cli_foreground_and_empty) {
    char line[] = "  run counter\t ";
    char* tokens[4];
    int background = 1;
    ASSERT_EQ(ss_cli_parse_line(line, tokens, 4, &background), 2);
    ASSERT_FALSE(background);
    ASSERT_STR_EQ(tokens[0], "run");
    ASSERT_STR_EQ(tokens[1], "counter");
    char empty[] = " \t ";
    ASSERT_EQ(ss_cli_parse_line(empty, tokens, 4, &background), 0);
    ASSERT_FALSE(background);
}

TEST(cli_invalid_background_syntax) {
    const char* forms[] = {"&",
                           " &  ",
                           "run counter&&",
                           "run counter & &",
                           "run & counter",
                           "run counter& ps",
                           "help&exit"};
    for (unsigned i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
        char line[64];
        strcpy(line, forms[i]);
        char* tokens[4];
        int background;
        ASSERT_EQ(ss_cli_parse_line(line, tokens, 4, &background),
                  SS_CLI_PARSE_SYNTAX);
    }
}

TEST(cli_token_capacity_excludes_background) {
    char line[] = "a b c d&";
    char* tokens[4];
    int background;
    ASSERT_EQ(ss_cli_parse_line(line, tokens, 4, &background), 4);
    ASSERT_TRUE(background);
    ASSERT_STR_EQ(tokens[3], "d");
    char overflow[] = "a b c d e&";
    ASSERT_EQ(ss_cli_parse_line(overflow, tokens, 4, &background),
              SS_CLI_PARSE_LIMIT);
}

void run_cli_parse_tests(void) {
    RUN_TEST(cli_background_spacing);
    RUN_TEST(cli_background_all_commands);
    RUN_TEST(cli_foreground_and_empty);
    RUN_TEST(cli_invalid_background_syntax);
    RUN_TEST(cli_token_capacity_excludes_background);
}
