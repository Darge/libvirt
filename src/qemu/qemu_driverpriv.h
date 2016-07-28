/*
 * qemu_driverpriv.h: private declarations for managing qemu guests
 *
 * Copyright (C) 2016 Tomasz Flendrich
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
 * Author: Tomasz Flendrich
 */

#ifndef __QEMU_DRIVERPRIV_H__
# define __QEMU_DRIVERPRIV_H__

# include "domain_conf.h"
# include "qemu_conf.h"

int qemuDomainAttachDeviceLiveAndConfig(virConnectPtr conn,
                                        virDomainObjPtr vm,
                                        virQEMUDriverPtr driver,
                                        const char *xml,
                                        unsigned int flags);

int qemuDomainDetachDeviceLiveAndConfig(virQEMUDriverPtr driver,
                                        virDomainObjPtr vm,
                                        const char *xml,
                                        unsigned int flags);

int
qemuDomainUpdateDeviceLiveAndConfig(virConnectPtr conn,
                                    virDomainObjPtr vm,
                                    virQEMUDriverPtr driver,
                                    const char *xml,
                                    unsigned int flags);

#endif /* __QEMU_DRIVERPRIV_H__ */
