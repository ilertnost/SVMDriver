#define SVM_DEBUG 1

#if SVM_DEBUG
#define DBGLOG(...)  IOLog("SVM: " __VA_ARGS__)
#else
#define DBGLOG(...)
#endif

#define ERRLOG(...)  IOLog("SVM: ERROR: " __VA_ARGS__)
