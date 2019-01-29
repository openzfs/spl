dnl #
dnl # 4.20: Kernel removes current_kernel_time()
dnl #
AC_DEFUN([SPL_AC_KERNEL_CURRENT_KERNEL_TIME],
	[AC_MSG_CHECKING([whether current_kernel_time() exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/ktime.h>
	], [
		struct timespec t __attribute__ ((unused)) = current_kernel_time();
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_CURRENT_TIME, 1, [current_kernel_time() exists])
	], [
		AC_MSG_RESULT(no)
	])
])
