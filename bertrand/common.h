#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H


/* std::format is part of the C++20 standard, but was not fully implemented until GCC 
13+, clang 18+, or MSVC 19.29+. */
#if defined(__GNUC__)
    #if defined(__clang__) && __clang_major__ >= 19
        #define BERTRAND_HAS_STD_FORMAT
    #elif (__GNUC__ >= 13)
        #define BERTRAND_HAS_STD_FORMAT
    #endif
#elif defined(_MSC_VER)
    #if (_MSC_VER >= 1929)
        #define BERTRAND_HAS_STD_FORMAT
    #endif
#endif


#if defined(_MSC_VER)
    #define BERTRAND_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define BERTRAND_NOINLINE __attribute__((noinline))
#else
    #define BERTRAND_NOINLINE
#endif


#define BERTRAND_CONCAT_PAIR_DETAIL(x, y) x##y
#define BERTRAND_CONCAT_PAIR(x, y) BERTRAND_CONCAT_PAIR_DETAIL(x, y)


#endif  // BERTRAND_COMMON_H
