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
\*****************************************************************************/
#ifndef _SPL_POLICY_H
#define _SPL_POLICY_H
#include <sys/types.h>
#include <sys/xvattr.h>
#include <sys/zfs_znode.h>
#include "cred.h"
#include "vnode.h"
#include <linux/kmod.h>
#include <linux/security.h>

/*
* Possible problem:
* I'm not using passed credentials for two reasons:
* Linux kernel only exposes interfaces to check for credentials of CURRENT user
* In ZFS, credentials are almost always obtained by calling CRED() which is defined in SPL as current_cred(), so it is the same credentials set.
* Is this correct? There are some exceptions to this? (example: ZIL replay?)
*/
static inline int spl_capable(cred_t *c, int capability) {
    return capable(capability)?0:EACCES;
}

static inline int secpolicy_fs_unmount(cred_t *c, struct vfsmount* mnt) {
    return spl_capable(c, CAP_SYS_ADMIN);
}

static inline int secpolicy_sys_config(cred_t *c, int checkonly) {
    return spl_capable(c, CAP_SYS_ADMIN);
}

static inline int secpolicy_nfs(cred_t *c) {
    return spl_capable(c, CAP_SYS_ADMIN);
}

static inline int secpolicy_zfs(cred_t *c) {
    return spl_capable(c, CAP_SYS_ADMIN);
}

static inline int secpolicy_zinject(cred_t *c) {
    return spl_capable(c, CAP_SYS_ADMIN);
}

static inline int secpolicy_vnode_setids_setgids(cred_t *c, gid_t gid) {
    if(in_group_p(gid))
        return 0;

    return spl_capable(c, CAP_FSETID);
}

static inline int secpolicy_vnode_setid_retain(cred_t *c, int is_setuid_root) {
    return spl_capable(c, CAP_FSETID);
}

static inline int secpolicy_setid_setsticky_clear(struct inode *ip, vattr_t *attr, vattr_t *oldattr, cred_t *c) {
    int requires_extrapriv=B_FALSE;

    if((attr->va_mode & S_ISGID) && !in_group_p(oldattr->va_gid))
        requires_extrapriv=B_TRUE;

    if((attr->va_mode & S_ISUID) && !(oldattr->va_uid==crgetuid(c)))
        requires_extrapriv=B_TRUE;

    if(requires_extrapriv == B_FALSE)
        return 0;


    return spl_capable(c, CAP_FSETID);
}

static inline int secpolicy_setid_clear(vattr_t *v, cred_t *c) {

    if(spl_capable(c, CAP_FSETID))
        return 0;

    if(v->va_mode & (S_ISUID|S_ISGID)) {
        v->va_mask |=AT_MODE;
        v->va_mode &= ~ (S_ISUID|S_ISGID);
    }
    return 0;
}

static inline int secpolicy_vnode_any_access(cred_t *c , struct inode *ip, uid_t owner) {
    if(crgetuid(c)==owner)
        return 0;

    if(spl_capable(c, CAP_DAC_OVERRIDE))
        return 0;

    if(spl_capable(c, CAP_DAC_READ_SEARCH))
        return 0;

    if(spl_capable(c, CAP_FOWNER))
        return 0;

    return EACCES;
}

static inline int secpolicy_vnode_access2(cred_t *c, struct inode *ip, uid_t owner, mode_t curmode, mode_t wantedmode) {
    mode_t missing = ~curmode & wantedmode;
    if(missing==0)
        return 0;

    if((missing & ~(S_IRUSR | S_IXUSR))==0)
    {   //needs only DAC_READ_SEARCH
        if(spl_capable(c, CAP_DAC_READ_SEARCH))
            return 0;
    }
    return spl_capable(c, CAP_DAC_OVERRIDE);
}

static inline int secpolicy_vnode_chown(cred_t *c, uid_t owner) {
    if(crgetuid(c)==owner)
        return 0;

    return spl_capable(c, CAP_FOWNER);
}

static inline int secpolicy_vnode_setdac(cred_t *c, uid_t owner) {
    if(crgetuid(c)==owner)
        return 0;

    return spl_capable(c, CAP_DAC_OVERRIDE);
}

static inline int secpolicy_vnode_remove(cred_t *c) {
    return spl_capable(c, CAP_FOWNER);
}

static inline int secpolicy_vnode_setattr(cred_t *c, struct inode *ip, vattr_t *vap, vattr_t *oldvap, int flags, int (*zaccess)(void *, int, cred_t *), znode_t *znode) {
    int mask = vap->va_mask;
    int err;
    if (mask & AT_MODE) {
        if((err=secpolicy_vnode_setdac(c, oldvap->va_uid)))   //Assignment here and in similar conditions is intentional.
            return err;

        if((err=secpolicy_setid_setsticky_clear(ip, vap, oldvap, c)))
            return err;

    } else {
        vap->va_mode = oldvap->va_mode;
    }
    if (mask & AT_SIZE) {
        if (S_ISDIR(ip->i_mode))
            return (EISDIR);

        if((err=zaccess(znode, S_IWUSR, c)))
            return err;

    }
    if (mask & (AT_UID | AT_GID)) {
        if (((mask & AT_UID) && vap->va_uid != oldvap->va_uid) ||
                ((mask & AT_GID) && vap->va_gid != oldvap->va_gid )) {
            secpolicy_setid_clear(vap, c);
            if((err=secpolicy_vnode_setdac(c, oldvap->va_uid)))
                return err;
        }

    }
    if (mask & (AT_ATIME | AT_MTIME)) {
        if((secpolicy_vnode_setdac( c, oldvap->va_uid) != 0) && // If the caller is the owner or has appropriate capabilities, allow to set every time.
                (mask & (ATTR_ATIME_SET | ATTR_MTIME_SET)))  { //Linux-specific constants meaning: the user has requested to write specific times instead of current time.
            if((err=zaccess(znode, S_IWUSR, c)))  //should have write permission.
                return err;
        }
    }
    return 0;
}

static inline int secpolicy_vnode_stky_modify(cred_t *c) { //NOT USED!
    return EACCES;
}

static inline int secpolicy_basic_link(cred_t *c) {
    return spl_capable(c, CAP_FOWNER);
}

static inline int secpolicy_vnode_create_gid(cred_t *c) { //USED ONLY WITH KSID!!
    return EACCES;
}

static inline int secpolicy_xvattr(xvattr_t *xv, uid_t owner, cred_t *c, mode_t mode) {
    return secpolicy_vnode_chown(c, owner);
}



#endif /* SPL_POLICY_H */
