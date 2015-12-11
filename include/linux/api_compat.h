typedef struct {
	struct task_struct *lac_thread;
	void *lac_journal_info;
} api_cookie_t;

static inline api_cookie_t
spl_api_mark(void)
{
	api_cookie_t cookie;

	cookie.lac_thread = current;
	cookie.lac_journal_info = current->journal_info;
	current->journal_info = NULL;

	return (cookie);
}

static inline void
spl_api_unmark(api_cookie_t cookie)
{
	ASSERT3P(cookie.lac_thread, ==, current);
	ASSERT(current->journal_info == NULL);

	current->journal_info = cookie.lac_journal_info;
}

#define LINUX_API_CALL_VOID(function, ...)			\
do {								\
	api_cookie_t cookie = spl_api_mark();		\
	(void) (function)(__VA_ARGS__);				\
	spl_api_unmark(cookie);					\
} while(0)

#define LINUX_API_CALL_TYPED(rc, type, function, ...)		\
do {								\
	api_cookie_t cookie = spl_api_mark();		\
	(rc) = (type)(function)(__VA_ARGS__);			\
	spl_api_unmark(cookie);					\
} while(0)

#define LINUX_API_CALL(rc, function, ...)			\
do {								\
	api_cookie_t cookie = spl_api_mark();		\
	(rc) = (function)(__VA_ARGS__);				\
	spl_api_unmark(cookie);					\
} while(0)
