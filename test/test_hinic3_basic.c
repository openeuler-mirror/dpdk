#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>

#include "../test.h"
#include <rte_test.h>

static int
test_framework_malloc(void)
{
    void *ptr = rte_zmalloc("test_malloc", 1024, 0);
    RTE_TEST_ASSERT(ptr != NULL, "rte_zmalloc failed");
    
    rte_free(ptr);
    
    return TEST_SUCCESS;
}

static int
test_framework_assertion(void)
{
    int value = 42;
    RTE_TEST_ASSERT(value == 42, "Basic assertion failed");
    RTE_TEST_ASSERT(value > 0, "Comparison assertion failed");
    RTE_TEST_ASSERT(value < 100, "Comparison assertion failed");
    
    return TEST_SUCCESS;
}

static struct unit_test_suite hinic3_basic_test_suite = {
    .suite_name = "HINIC3 Framework Tests",
    .setup = NULL,
    .teardown = NULL,
    .unit_test_cases = {
        TEST_CASE(test_framework_malloc),
        TEST_CASE(test_framework_assertion),
        TEST_CASES_END()
    }
};

static int
test_hinic3_basic(void)
{
    return unit_test_suite_runner(&hinic3_basic_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_basic_autotest, test_hinic3_basic);
