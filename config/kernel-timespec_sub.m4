dnl #
dnl # 5.0 API change
dnl #
dnl # Does timespec_sub() exists?  If not, use timespec64_sub().
dnl #
AC_DEFUN([SPL_AC_KERNEL_TIMESPEC_SUB], [
	AC_MSG_CHECKING([whether timespec_sub() exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/time.h>
	],[
		struct timespec a = {0}, b = {0};
		timespec_sub(a, b);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_TIMESPEC_SUB, 1,
		    [kernel has timespec_sub])
	],[
		AC_MSG_RESULT(no)
	])
])
