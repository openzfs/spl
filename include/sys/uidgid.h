/*
 * Someone put a copyright here I guess.
 *
 *
 */

#ifndef _SPL_UIDGID_H
#define _SPL_UIDGID_H



#ifdef HAVE_KUIDGID_T

 /* Linux still defines uid_t and gid_t as typedefs so we have to
  * go with #define directives, or else convert to k[ug]id_t everywhere
  * with that being the new typedef when this conversion is not in use.
  * We stick with the classic uid/gid names and #define where needed.
  */
 #include <linux/uidgid.h>
 #define uid_t kuid_t
 #define gid_t kgid_t

#endif



#endif
