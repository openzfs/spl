/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Debug Implementation.
\*****************************************************************************/

#include <linux/kmod.h>
#include "sys/policy.h"
#include <linux/security.h>
/*
* Possible problem:
* I'm not using passed credentials for two reasons:
* Linux kernel only exposes interfaces to check for credentials of CURRENT user
* In ZFS, credentials are almost always obtained by calling CRED() which is defined in SPL as current_cred(), so it is the same credentials set.
* Is this correct? There are some exceptions to this? (example: ZIL replay?)
*/
static boolean_t spl_capable(cred_t* c, int capability) {
    return capable(capability)?0:EACCES;
}

boolean_t secpolicy_sys_config(cred_t* c,boolean_t checkonly) {
    return spl_capable(c,CAP_SYS_ADMIN);
}
EXPORT_SYMBOL(secpolicy_sys_config);
boolean_t secpolicy_nfs(cred_t* c) {
    return spl_capable(c,CAP_SYS_ADMIN);
}
EXPORT_SYMBOL(secpolicy_nfs);
boolean_t secpolicy_zfs(cred_t* c) {
    return spl_capable(c,CAP_SYS_ADMIN);
}
EXPORT_SYMBOL(secpolicy_zfs);
boolean_t secpolicy_zinject(cred_t* c) {
    return spl_capable(c,CAP_SYS_ADMIN);
}
EXPORT_SYMBOL(secpolicy_zinject);
boolean_t secpolicy_vnode_setids_setgids(cred_t* c,gid_t gid) {
    if(in_group_p(gid)) return 0;
    return spl_capable(c,CAP_FSETID);
}
EXPORT_SYMBOL(secpolicy_vnode_setids_setgids);
boolean_t secpolicy_vnode_setid_retain(cred_t* c,boolean_t is_setuid_root) {
    return spl_capable(c,CAP_FSETID);
}
EXPORT_SYMBOL(secpolicy_vnode_setid_retain);
boolean_t secpolicy_setid_clear(vattr_t* v,cred_t* c) {

    if(spl_capable(c,CAP_FSETID)) return 0;
    if(v->va_mode & (S_ISUID|S_ISGID)) {
        v->va_mask |=AT_MODE;
        v->va_mode &= ~ (S_ISUID|S_ISGID);
    }
    return 0;
}
EXPORT_SYMBOL(secpolicy_setid_clear);
boolean_t secpolicy_vnode_any_access(cred_t* c ,struct inode* ip,uid_t owner) {
    if(crgetuid(c)==owner) return 0;
    if(spl_capable(c,CAP_DAC_OVERRIDE)) return 0;
    if(spl_capable(c,CAP_DAC_READ_SEARCH)) return 0;
    if(spl_capable(c,CAP_FOWNER)) return 0;
    return EACCES;
}
EXPORT_SYMBOL(secpolicy_vnode_any_access);
boolean_t secpolicy_vnode_access2(cred_t* c,struct inode* ip,uid_t owner,mode_t curmode,mode_t wantedmode) {
    mode_t missing = ~curmode & wantedmode;
    if(missing==0) return 0;

    if((missing & (~ 4 | 1 ))==0) //What are the right constants?
    {   //needs only DAC_READ_SEARCH
        if(spl_capable(c,CAP_DAC_READ_SEARCH)) return 0;
    }
    return spl_capable(c,CAP_DAC_OVERRIDE);
}
EXPORT_SYMBOL(secpolicy_vnode_access2);
boolean_t secpolicy_vnode_chown(cred_t* c,uid_t owner) {
    if(crgetuid(c)==owner) return 0;
    return spl_capable(c,CAP_FOWNER);
}
EXPORT_SYMBOL(secpolicy_vnode_chown);
boolean_t secpolicy_vnode_setdac(cred_t* c,uid_t owner) {
    if(crgetuid(c)==owner) return 0;
    return spl_capable(c,CAP_DAC_OVERRIDE);
}
EXPORT_SYMBOL(secpolicy_vnode_setdac);
boolean_t secpolicy_vnode_remove(cred_t* c) {
    return spl_capable(c,CAP_FOWNER);
}
EXPORT_SYMBOL(secpolicy_vnode_remove);
//znode_t is defined in ZFS, not in SPL, so it is a void*. But that's not a problem as we need it only to pass it to the zaccess function, not to work with the structure itself.
boolean_t secpolicy_vnode_setattr(cred_t* c, struct inode* ip,vattr_t* vap,vattr_t* oldvap,int flags,int (*zaccess)(void *, int, cred_t *),void* znode) {
    int mask = vap->va_mask;
    int err;
    //Tentative to make this function more readable.
#define CHECK(arg) err=arg; if(err) return err
    if (mask & AT_MODE) {
        CHECK(secpolicy_vnode_setdac(c, oldvap->va_uid));
        CHECK(secpolicy_setid_setsticky_clear(ip, vap, oldvap, c));
    } else {
        vap->va_mode = oldvap->va_mode;
    }
    if (mask & AT_SIZE) {
        if (S_ISDIR(ip->i_mode))  return (EISDIR);
        CHECK(zaccess(znode, S_IWUSR, c));
    }
    if (mask & (AT_UID | AT_GID)) {
        if (((mask & AT_UID) && vap->va_uid != oldvap->va_uid) ||
                ((mask & AT_GID) && vap->va_gid != oldvap->va_gid )) {
            secpolicy_setid_clear(vap, c);
            CHECK(secpolicy_vnode_setdac(c,oldvap->va_uid));
        }

    }
    if (mask & (AT_ATIME | AT_MTIME)) {
        if((secpolicy_vnode_setdac( c, oldvap->va_uid) != 0) && // If the caller is the owner or has appropriate capabilities, allow to set every time.
                (mask & (ATTR_ATIME_SET | ATTR_MTIME_SET)))  { //Linux-specific constants meaning: the user has requested to write specific times instead of current time.
            CHECK(zaccess(znode, S_IWUSR, c));  //should have write permission.
        }
    }
    return 0;
}
EXPORT_SYMBOL(secpolicy_vnode_setattr);
boolean_t secpolicy_vnode_stky_modify(cred_t* c) { //NOT USED!
    return EACCES;
}
EXPORT_SYMBOL(secpolicy_vnode_stky_modify);
boolean_t secpolicy_setid_setsticky_clear(struct inode* ip,vattr_t* attr,vattr_t* oldattr,cred_t* c) {
    boolean_t requires_extrapriv=B_FALSE;
    if((attr->va_mode & S_ISGID) && !in_group_p(oldattr->va_gid)) {
        requires_extrapriv=B_TRUE;
    }
    if((attr->va_mode & S_ISUID) && !(oldattr->va_uid==crgetuid(c))) {
        requires_extrapriv=B_TRUE;
    }
    if(requires_extrapriv == B_FALSE) {
        return 0;
    }

    return spl_capable(c,CAP_FSETID);
}
EXPORT_SYMBOL(secpolicy_setid_setsticky_clear);
boolean_t secpolicy_basic_link(cred_t* c) {
    return spl_capable(c,CAP_FOWNER);
}
EXPORT_SYMBOL(secpolicy_basic_link);
boolean_t secpolicy_vnode_create_gid(cred_t* c) { //USED ONLY WITH KSID!!
    return EACCES;
}
EXPORT_SYMBOL(secpolicy_vnode_create_gid);
boolean_t secpolicy_xvattr(void* xv,uid_t owner,cred_t* c,mode_t mode) {
    return secpolicy_vnode_chown(c,owner);
}
EXPORT_SYMBOL(secpolicy_xvattr);
