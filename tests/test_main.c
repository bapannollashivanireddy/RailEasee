#include <stdio.h>

int test_hashtable(void);
int test_heap(void);
int test_avl(void);
int test_engine(void);
int test_auxiliary_structures(void);
int test_storage(void);

/* Run each test and print its result. O(number of test groups). */
int main(void)
{
    int failed = 0;
#define RUN_TEST(name, function) do { \
    int passed = function(); \
    (void)printf("%s: %s\n", name, passed ? "PASS" : "FAIL"); \
    if (!passed) { ++failed; } \
} while (0)
    RUN_TEST("Hash table", test_hashtable);
    RUN_TEST("Min heap", test_heap);
    RUN_TEST("AVL tree", test_avl);
    RUN_TEST("Circular queue, DLL, bitmap, stack", test_auxiliary_structures);
    RUN_TEST("Persistence round-trip and corruption handling", test_storage);
    RUN_TEST("Booking/reallocation + 10,000-operation stress", test_engine);
#undef RUN_TEST
    return failed == 0 ? 0 : 1;
}
