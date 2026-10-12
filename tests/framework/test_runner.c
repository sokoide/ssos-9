/* Native test entry point for the preemptive task scheduler. */
#include "ssos_test.h"

void run_scheduler_tests(void);
void run_cli_parse_tests(void);
void run_cli_background_tests(void);

int total_tests = 0;
int failed_tests = 0;
const char* current_test_name = NULL;

int main(void) {
    printf("=== SSOS Native Tests ===\n");
    run_scheduler_tests();
    run_cli_parse_tests();
    run_cli_background_tests();
    printf("\n--- Results: %d passed, %d failed, %d total ---\n",
           total_tests - failed_tests, failed_tests, total_tests);
    return failed_tests ? 1 : 0;
}
