/*
 * qemu_driver.h: core driver methods for managing qemu guests
 *
 * Copyright (C) 2006, 2007 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#ifndef __QEMU_DRIVER_H__
# define __QEMU_DRIVER_H__
 #include <config.h>

 #include <sys/types.h>
 #include <sys/poll.h>
 #include <sys/time.h>
 #include <dirent.h>
 #include <limits.h>
 #include <string.h>
 #include <stdio.h>
 #include <stdarg.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <errno.h>
 #include <sys/stat.h>
 #include <fcntl.h>
 #include <signal.h>
 #include <sys/wait.h>
 #include <sys/ioctl.h>
 #include <sys/un.h>
 #include <byteswap.h>


 #include "qemu_driver.h"
 #include "qemu_agent.h"
 #include "qemu_alias.h"
 #include "qemu_conf.h"
 #include "qemu_capabilities.h"
 #include "qemu_command.h"
 #include "qemu_parse_command.h"
 #include "qemu_cgroup.h"
 #include "qemu_hostdev.h"
 #include "qemu_hotplug.h"
 #include "qemu_monitor.h"
 #include "qemu_process.h"
 #include "qemu_migration.h"
 #include "qemu_blockjob.h"

 #include "virerror.h"
 #include "virlog.h"
 #include "datatypes.h"
 #include "virbuffer.h"
 #include "nodeinfo.h"
 #include "virhostcpu.h"
 #include "virhostmem.h"
 #include "virstats.h"
 #include "capabilities.h"
 #include "viralloc.h"
 #include "viruuid.h"
 #include "domain_conf.h"
 #include "domain_audit.h"
 #include "node_device_conf.h"
 #include "virpci.h"
 #include "virusb.h"
 #include "virprocess.h"
 #include "libvirt_internal.h"
 #include "virxml.h"
 #include "cpu/cpu.h"
 #include "virsysinfo.h"
 #include "domain_nwfilter.h"
 #include "nwfilter_conf.h"
 #include "virhook.h"
 #include "virstoragefile.h"
 #include "virfile.h"
 #include "fdstream.h"
 #include "configmake.h"
 #include "virthreadpool.h"
 #include "locking/lock_manager.h"
 #include "locking/domain_lock.h"
 #include "virkeycode.h"
 #include "virnodesuspend.h"
 #include "virtime.h"
 #include "virtypedparam.h"
 #include "virbitmap.h"
 #include "virstring.h"
 #include "storage/storage_driver.h"
 #include "virhostdev.h"
 #include "domain_capabilities.h"
 #include "vircgroup.h"
 #include "virperf.h"
 #include "virnuma.h"
 #include "dirname.h"
 #include "network/bridge_driver.h"

int qemuRegister(void);
/* TODO: non-NULL? */
int qemuDomainAttachDeviceLiveAndConfig(virConnectPtr conn,
                                        virDomainObjPtr vm,
                                       const char *xml,
                                       virQEMUDriverPtr driver,
                                       unsigned int flags);

int qemuDomainDetachDeviceliveAndConfig(virDomainPtr dom,
                                        virQEMUDriverPtr driver,
                                        virDomainObjPtr vm,
                                        const char *xml,
                                        unsigned int flags);

#endif /* __QEMU_DRIVER_H__ */
