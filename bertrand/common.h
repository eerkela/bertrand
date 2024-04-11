#ifndef BERTRAND_COMMON_H
#define BERTRAND_COMMON_H


#if defined(_MSC_VER)
    #define BERTRAND_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define BERTRAND_NOINLINE __attribute__((noinline))
#else
    #define BERTRAND_NOINLINE
#endif


#endif  // BERTRAND_COMMON_H
