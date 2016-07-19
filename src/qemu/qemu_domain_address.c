/*
 * qemu_domain_address.c: QEMU domain address
 *
 * Copyright (C) 2006-2016 Red Hat, Inc.
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

#include <config.h>

#include "qemu_domain_address.h"
#include "qemu_domain.h"
#include "viralloc.h"
#include "virerror.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_QEMU

VIR_LOG_INIT("qemu.qemu_domain_address");

#define VIO_ADDR_NET 0x1000ul
#define VIO_ADDR_SCSI 0x2000ul
#define VIO_ADDR_SERIAL 0x30000000ul
#define VIO_ADDR_NVRAM 0x3000ul


int
qemuDomainSetSCSIControllerModel(const virDomainDef *def,
                                 virQEMUCapsPtr qemuCaps,
                                 int *model)
{
    if (*model > 0) {
        switch (*model) {
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LSILOGIC:
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SCSI_LSI)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("This QEMU doesn't support "
                                 "the LSI 53C895A SCSI controller"));
                return -1;
            }
            break;
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_SCSI:
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_SCSI)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("This QEMU doesn't support "
                                 "virtio scsi controller"));
                return -1;
            }
            break;
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_IBMVSCSI:
            /*TODO: need checking work here if necessary */
            break;
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LSISAS1068:
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SCSI_MPTSAS1068)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("This QEMU doesn't support "
                                 "the LSI SAS1068 (MPT Fusion) controller"));
                return -1;
            }
            break;
        case VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LSISAS1078:
            if (!virQEMUCapsGet(qemuCaps, QEMU_CAPS_SCSI_MEGASAS)) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("This QEMU doesn't support "
                                 "the LSI SAS1078 (MegaRAID) controller"));
                return -1;
            }
            break;
        default:
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Unsupported controller model: %s"),
                           virDomainControllerModelSCSITypeToString(*model));
            return -1;
        }
    } else {
        if (qemuDomainMachineIsPSeries(def)) {
            *model = VIR_DOMAIN_CONTROLLER_MODEL_SCSI_IBMVSCSI;
        } else if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_SCSI_LSI)) {
            *model = VIR_DOMAIN_CONTROLLER_MODEL_SCSI_LSILOGIC;
        } else if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_SCSI)) {
            *model = VIR_DOMAIN_CONTROLLER_MODEL_SCSI_VIRTIO_SCSI;
        } else {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Unable to determine model for scsi controller"));
            return -1;
        }
    }

    return 0;
}


static int
qemuDomainSpaprVIOFindByReg(virDomainDefPtr def ATTRIBUTE_UNUSED,
                            virDomainDeviceDefPtr device ATTRIBUTE_UNUSED,
                            virDomainDeviceInfoPtr info, void *opaque)
{
    virDomainDeviceInfoPtr target = opaque;

    if (info->type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO)
        return 0;

    /* Match a dev that has a reg, is not us, and has a matching reg */
    if (info->addr.spaprvio.has_reg && info != target &&
        info->addr.spaprvio.reg == target->addr.spaprvio.reg)
        /* Has to be < 0 so virDomainDeviceInfoIterate() will exit */
        return -1;

    return 0;
}


static int
qemuDomainAssignSpaprVIOAddress(virDomainDefPtr def,
                                virDomainDeviceInfoPtr info,
                                unsigned long long default_reg)
{
    bool user_reg;
    int ret;

    if (info->type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO)
        return 0;

    /* Check if the user has assigned the reg already, if so use it */
    user_reg = info->addr.spaprvio.has_reg;
    if (!user_reg) {
        info->addr.spaprvio.reg = default_reg;
        info->addr.spaprvio.has_reg = true;
    }

    ret = virDomainDeviceInfoIterate(def, qemuDomainSpaprVIOFindByReg, info);
    while (ret != 0) {
        if (user_reg) {
            virReportError(VIR_ERR_XML_ERROR,
                           _("spapr-vio address %#llx already in use"),
                           info->addr.spaprvio.reg);
            return -EEXIST;
        }

        /* We assigned the reg, so try a new value */
        info->addr.spaprvio.reg += 0x1000;
        ret = virDomainDeviceInfoIterate(def, qemuDomainSpaprVIOFindByReg,
                                         info);
    }

    return 0;
}


static int
qemuDomainAssignSpaprVIOAddresses(virDomainDefPtr def,
                                  virQEMUCapsPtr qemuCaps)
{
    size_t i;
    int ret = -1;
    int model;

    /* Default values match QEMU. See spapr_(llan|vscsi|vty).c */

    for (i = 0; i < def->nnets; i++) {
        if (def->nets[i]->model &&
            STREQ(def->nets[i]->model, "spapr-vlan"))
            def->nets[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO;
        if (qemuDomainAssignSpaprVIOAddress(def, &def->nets[i]->info,
                                            VIO_ADDR_NET) < 0)
            goto cleanup;
    }

    for (i = 0; i < def->ncontrollers; i++) {
        model = def->controllers[i]->model;
        if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_SCSI) {
            if (qemuDomainSetSCSIControllerModel(def, qemuCaps, &model) < 0)
                goto cleanup;
        }

        if (model == VIR_DOMAIN_CONTROLLER_MODEL_SCSI_IBMVSCSI &&
            def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_SCSI)
            def->controllers[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO;
        if (qemuDomainAssignSpaprVIOAddress(def, &def->controllers[i]->info,
                                            VIO_ADDR_SCSI) < 0)
            goto cleanup;
    }

    for (i = 0; i < def->nserials; i++) {
        if (def->serials[i]->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_SERIAL &&
            qemuDomainMachineIsPSeries(def))
            def->serials[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO;
        if (qemuDomainAssignSpaprVIOAddress(def, &def->serials[i]->info,
                                            VIO_ADDR_SERIAL) < 0)
            goto cleanup;
    }

    if (def->nvram) {
        if (qemuDomainMachineIsPSeries(def))
            def->nvram->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO;
        if (qemuDomainAssignSpaprVIOAddress(def, &def->nvram->info,
                                            VIO_ADDR_NVRAM) < 0)
            goto cleanup;
    }

    /* No other devices are currently supported on spapr-vio */

    ret = 0;

 cleanup:
    return ret;
}


static void
qemuDomainPrimeVirtioDeviceAddresses(virDomainDefPtr def,
                                     virDomainDeviceAddressType type)
{
    /*
       declare address-less virtio devices to be of address type 'type'
       disks, networks, consoles, controllers, memballoon and rng in this
       order
       if type is ccw filesystem devices are declared to be of address type ccw
    */
    size_t i;

    for (i = 0; i < def->ndisks; i++) {
        if (def->disks[i]->bus == VIR_DOMAIN_DISK_BUS_VIRTIO &&
            def->disks[i]->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE)
            def->disks[i]->info.type = type;
    }

    for (i = 0; i < def->nnets; i++) {
        if (STREQ(def->nets[i]->model, "virtio") &&
            def->nets[i]->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE) {
            def->nets[i]->info.type = type;
        }
    }

    for (i = 0; i < def->ninputs; i++) {
        if (def->inputs[i]->bus == VIR_DOMAIN_DISK_BUS_VIRTIO &&
            def->inputs[i]->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE)
            def->inputs[i]->info.type = type;
    }

    for (i = 0; i < def->ncontrollers; i++) {
        if ((def->controllers[i]->type ==
             VIR_DOMAIN_CONTROLLER_TYPE_VIRTIO_SERIAL ||
             def->controllers[i]->type ==
             VIR_DOMAIN_CONTROLLER_TYPE_SCSI) &&
            def->controllers[i]->info.type ==
            VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE)
            def->controllers[i]->info.type = type;
    }

    if (def->memballoon &&
        def->memballoon->model == VIR_DOMAIN_MEMBALLOON_MODEL_VIRTIO &&
        def->memballoon->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE)
        def->memballoon->info.type = type;

    for (i = 0; i < def->nrngs; i++) {
        if (def->rngs[i]->model == VIR_DOMAIN_RNG_MODEL_VIRTIO &&
            def->rngs[i]->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE)
            def->rngs[i]->info.type = type;
    }

    if (type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCW) {
        for (i = 0; i < def->nfss; i++) {
            if (def->fss[i]->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE)
                def->fss[i]->info.type = type;
        }
    }
}

virDomainCCWAddressSetPtr
qemuDomainCCWAddrSetCreateFromDomain(virDomainDefPtr def)
{
    virDomainCCWAddressSetPtr addrs = NULL;

    if (!(addrs = virDomainCCWAddressSetCreate()))
        goto error;

    if (virDomainDeviceInfoIterate(def, virDomainCCWAddressValidate,
                                   addrs) < 0)
        goto error;

    if (virDomainDeviceInfoIterate(def, virDomainCCWAddressAllocate,
                                   addrs) < 0)
        goto error;

    return addrs;

 error:
    virDomainCCWAddressSetFree(addrs);
    return NULL;
}

/*
 * Three steps populating CCW devnos
 * 1. Allocate empty address set
 * 2. Gather addresses with explicit devno
 * 3. Assign defaults to the rest
 */
static int
qemuDomainAssignS390Addresses(virDomainDefPtr def,
                              virQEMUCapsPtr qemuCaps)
{
    int ret = -1;
    virDomainCCWAddressSetPtr addrs = NULL;

    if (qemuDomainMachineIsS390CCW(def) &&
        virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_CCW)) {
        qemuDomainPrimeVirtioDeviceAddresses(
            def, VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCW);

        if (!(addrs = qemuDomainCCWAddrSetCreateFromDomain(def)))
            goto cleanup;

    } else if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_S390)) {
        /* deal with legacy virtio-s390 */
        qemuDomainPrimeVirtioDeviceAddresses(
            def, VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_S390);
    }

    ret = 0;

 cleanup:
    virDomainCCWAddressSetFree(addrs);

    return ret;
}


static void
qemuDomainAssignARMVirtioMMIOAddresses(virDomainDefPtr def,
                                       virQEMUCapsPtr qemuCaps)
{
    if (def->os.arch != VIR_ARCH_ARMV7L &&
        def->os.arch != VIR_ARCH_AARCH64)
        return;

    if (!(STRPREFIX(def->os.machine, "vexpress-") ||
          virDomainMachineIsVirt(def)))
        return;

    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIRTIO_MMIO)) {
        qemuDomainPrimeVirtioDeviceAddresses(
            def, VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_MMIO);
    }
}


int
qemuDomainAssignAddresses(virDomainDefPtr def,
                          virQEMUCapsPtr qemuCaps,
                          virDomainObjPtr obj ATTRIBUTE_UNUSED,
                          bool newDomain ATTRIBUTE_UNUSED)
{
    bool pciBridgeEnabled = virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_PCI_BRIDGE);
    bool virtioMMIOEnabled = virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIRTIO_MMIO);
    bool videoPrimaryEnabled = virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIDEO_PRIMARY);
    bool gpexEnabled = virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_GPEX);

    if (virDomainAssignVirtioSerialAddresses(def) < 0)
        return -1;

    if (qemuDomainAssignSpaprVIOAddresses(def, qemuCaps) < 0)
        return -1;

    if (qemuDomainAssignS390Addresses(def, qemuCaps) < 0)
        return -1;

    qemuDomainAssignARMVirtioMMIOAddresses(def, qemuCaps);

    if (virDomainAssignPCIAddresses(def, pciBridgeEnabled, virtioMMIOEnabled,
                                     videoPrimaryEnabled, gpexEnabled) < 0)
        return -1;

    return 0;
}
