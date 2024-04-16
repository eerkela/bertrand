#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H


////////////////////////
////    INCLUDES    ////
////////////////////////


/* std::format is part of the C++20 standard, but was not fully implemented until GCC 
13+, clang 18+, or MSVC 19.29+. */
#if defined(__GNUC__)
    #if defined(__clang__) && __clang_major__ >= 18
        #ifndef LINTER
            #define BERTRAND_HAS_STD_FORMAT
        #endif
    #elif (__GNUC__ >= 13)
        #define BERTRAND_HAS_STD_FORMAT
    #endif
#elif defined(_MSC_VER)
    #if (_MSC_VER >= 1929)
        #define BERTRAND_HAS_STD_FORMAT
    #endif
#endif


/////////////////////////
////    MODIFIERS    ////
/////////////////////////


#if defined(_MSC_VER)
    #define BERTRAND_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define BERTRAND_NOINLINE __attribute__((noinline))
#else
    #define BERTRAND_NOINLINE
#endif


#endif  // BERTRAND_COMMON_H
