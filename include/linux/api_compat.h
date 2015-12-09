#define LINUX_API_CALL_VOID(function, ...)			\
do {								\
	void *__journal_info = current->journal_info;		\
	current->journal_info = NULL;				\
	(void) (function)(__VA_ARGS__);				\
	current->journal_info = __journal_info;			\
} while(0)

#define LINUX_API_CALL_TYPED(rc, type, function, ...)		\
do {								\
	void *__journal_info = current->journal_info;		\
	current->journal_info = NULL;				\
	(rc) = (type)(function)(__VA_ARGS__);			\
	current->journal_info = __journal_info;			\
} while(0)

#define LINUX_API_CALL(rc, function, ...)			\
do {								\
	void *__journal_info = current->journal_info;		\
	current->journal_info = NULL;				\
	(rc) = (function)(__VA_ARGS__);				\
	current->journal_info = __journal_info;			\
} while(0)
