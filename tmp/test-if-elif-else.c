#if defined(DEBUG)
    int debug_level = 5;
#elif defined(VERBOSE)
    int debug_level = 3;
#elif PRODUCTION
    int debug_level = 0;
#else
    int debug_level = 1;
#endif

#if DEBUG > 2
    int extra_logging = 1;
#else
    int extra_logging = 0;
#endif

#if MAX_SIZE
    char buffer[MAX_SIZE];
#endif
