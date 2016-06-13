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
        if (ARCH_IS_PPC64(def->os.arch) &&
            STRPREFIX(def->os.machine, "pseries")) {
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
qemuDomainAssignPCIAddresses(virDomainDefPtr def,
                             virQEMUCapsPtr qemuCaps,
                             virDomainObjPtr obj)
{
    int ret = -1;
    virDomainPCIAddressSetPtr addrs = NULL;
    int max_idx = -1;
    int nbuses = 0;
    size_t i;
    int rv;
    bool buses_reserved = true;
    bool deviceVideoUsable = virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIDEO_PRIMARY);
    bool virtio_mmio_capability = virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIRTIO_MMIO);
    bool object_gpex_capability = virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_GPEX);

    virDomainPCIConnectFlags flags = VIR_PCI_CONNECT_TYPE_PCI_DEVICE;

    for (i = 0; i < def->ncontrollers; i++) {
        if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_PCI) {
            if ((int) def->controllers[i]->idx > max_idx)
                max_idx = def->controllers[i]->idx;
        }
    }

    nbuses = max_idx + 1;

    if (nbuses > 0 &&
        virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_PCI_BRIDGE)) {
        virDomainDeviceInfo info;

        /* 1st pass to figure out how many PCI bridges we need */
        if (!(addrs = virDomainPCIAddressSetCreate(def, nbuses, true)))
            goto cleanup;

        if (virDomainValidateDevicePCISlotsChipsets(def, addrs,
                                                     deviceVideoUsable) < 0)
            goto cleanup;

        for (i = 0; i < addrs->nbuses; i++) {
            if (!virDomainPCIBusFullyReserved(&addrs->buses[i]))
                buses_reserved = false;
        }

        /* Reserve 1 extra slot for a (potential) bridge only if buses
         * are not fully reserved yet
         */
        if (!buses_reserved &&
            virDomainPCIAddressReserveNextSlot(addrs, &info, flags) < 0)
            goto cleanup;

        if (virDomainAssignDevicePCISlots(def, addrs, virtio_mmio_capability) < 0)
            goto cleanup;

        for (i = 1; i < addrs->nbuses; i++) {
            virDomainPCIAddressBusPtr bus = &addrs->buses[i];

            if ((rv = virDomainDefMaybeAddController(
                     def, VIR_DOMAIN_CONTROLLER_TYPE_PCI,
                     i, bus->model)) < 0)
                goto cleanup;
            /* If we added a new bridge, we will need one more address */
            if (rv > 0 &&
                virDomainPCIAddressReserveNextSlot(addrs, &info, flags) < 0)
                goto cleanup;
        }
        nbuses = addrs->nbuses;
        virDomainPCIAddressSetFree(addrs);
        addrs = NULL;

    } else if (max_idx > 0) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("PCI bridges are not supported "
                         "by this QEMU binary"));
        goto cleanup;
    }

    if (!(addrs = virDomainPCIAddressSetCreate(def, nbuses, false)))
        goto cleanup;

    if (virDomainSupportsPCI(def, object_gpex_capability)) {
        if (virDomainValidateDevicePCISlotsChipsets(def, addrs,
                                                     deviceVideoUsable) < 0)
            goto cleanup;

        if (virDomainAssignDevicePCISlots(def, addrs,
                                           virtio_mmio_capability) < 0)
            goto cleanup;

        for (i = 0; i < def->ncontrollers; i++) {
            virDomainControllerDefPtr cont = def->controllers[i];
            int idx = cont->idx;
            virPCIDeviceAddressPtr addr;
            virDomainPCIControllerOptsPtr options;

            if (cont->type != VIR_DOMAIN_CONTROLLER_TYPE_PCI)
                continue;

            addr = &cont->info.addr.pci;
            options = &cont->opts.pciopts;

            /* set default model name (the actual name of the
             * device in qemu) for any controller that doesn't yet
             * have it set.
             */
            virDomainPCIControllerSetDefaultModelName(cont);

            /* set defaults for any other auto-generated config
             * options for this controller that haven't been
             * specified in config.
             */
            switch ((virDomainControllerModelPCI)cont->model) {
            case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
                if (options->chassisNr == -1)
                    options->chassisNr = cont->idx;
                break;
            case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
                if (options->chassis == -1)
                   options->chassis = cont->idx;
                if (options->port == -1)
                   options->port = (addr->slot << 3) + addr->function;
                break;
            case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
                if (options->chassis == -1)
                   options->chassis = cont->idx;
                if (options->port == -1)
                   options->port = addr->slot;
                break;
            case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
            case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
                if (options->busNr == -1)
                    options->busNr = virDomainAddressFindNewBusNr(def);
                if (options->busNr == -1) {
                    virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                                   _("No free busNr lower than current "
                                     "lowest busNr is available to "
                                     "auto-assign to bus %d. Must be "
                                     "manually assigned"),
                                   addr->bus);
                    goto cleanup;
                }
                break;
            case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
            case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
            case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
            case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
            case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
                break;
            }

            /* check if every PCI bridge controller's index is larger than
             * the bus it is placed onto
             */
            if (cont->model == VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE &&
                idx <= addr->bus) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                               _("PCI controller at index %d (0x%02x) has "
                                 "bus='0x%02x', but index must be "
                                 "larger than bus"),
                               idx, idx, addr->bus);
                goto cleanup;
            }
        }
    }

//    if (obj && obj->privateData) {
//        priv = obj->privateData;
        /* if this is the live domain object, we persist the PCI addresses */
        virDomainPCIAddressSetFree(def->pciaddrs);
        def->pciaddrs = addrs;
        addrs = NULL;
//    }

    ret = 0;

 cleanup:
    virDomainPCIAddressSetFree(addrs);

    return ret;
}


int
qemuDomainAssignAddresses(virDomainDefPtr def,
                          virQEMUCapsPtr qemuCaps,
                          virDomainObjPtr obj)
{
    size_t i;
    int model;
    bool virtio_ccw_capability = virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_CCW);
    bool virtio_s390_capability = virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_S390);
    bool virtio_mmio_capability = virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIRTIO_MMIO);

    if (virDomainAssignVirtioSerialAddresses(def, obj) < 0)
        return -1;

    /* Part of pre-address-assignment-stuff */
    for (i = 0; i < def->ncontrollers; i++) {
        model = def->controllers[i]->model;
        if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_SCSI) {
            if (qemuDomainSetSCSIControllerModel(def, qemuCaps, &model) < 0)
                return -1;
        }

        if (model == VIR_DOMAIN_CONTROLLER_MODEL_SCSI_IBMVSCSI &&
            def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_SCSI)
            def->controllers[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO;
    }

    if (virDomainAssignSpaprVIOAddresses(def) < 0)
        return -1;

    if (virDomainAssignS390Addresses(def, obj, virtio_ccw_capability,
        virtio_s390_capability) < 0)
        return -1;

    virDomainAssignARMVirtioMMIOAddresses(def, virtio_mmio_capability);

    if (qemuDomainAssignPCIAddresses(def, qemuCaps, obj) < 0)
        return -1;

    return 0;
}

static int
qemuAllocOptionsFill(virAllocOptionsPtr allocOpts,
                     virQEMUCapsPtr qemuCaps)
{
    virBitmapPtr flags = allocOpts->flags;

    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_CCW)
        && virBitmapSetBit(flags, ALLOC_VIRTIO_CCW) < 0)
            return -1;
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_S390)
        && virBitmapSetBit(flags, ALLOC_VIRTIO_S390) < 0)
            return -1;
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIRTIO_MMIO)
        && virBitmapSetBit(flags, ALLOC_DEVICE_VIRTIO_MMIO) < 0)
            return -1;
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_SCSI_LSI)
        && virBitmapSetBit(flags, ALLOC_SCSI_LSI) < 0)
            return -1;
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_VIRTIO_SCSI)
        && virBitmapSetBit(flags, ALLOC_VIRTIO_SCSI) < 0)
            return -1;
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_SCSI_MPTSAS1068)
        && virBitmapSetBit(flags, ALLOC_SCSI_MPTSAS1068) < 0)
            return -1;
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_SCSI_MEGASAS)
        && virBitmapSetBit(flags, ALLOC_SCSI_MEGASAS) < 0)
            return -1;
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIDEO_PRIMARY)
        && virBitmapSetBit(flags, ALLOC_DEVICE_VIDEO_PRIMARY) < 0)
            return -1;
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_GPEX)
        && virBitmapSetBit(flags, ALLOC_OBJECT_GPEX) < 0)
            return -1;
    if (virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_PCI_BRIDGE)
        && virBitmapSetBit(flags, ALLOC_DEVICE_PCI_BRIDGE) < 0)
            return -1;

  return 0;
}

int
qemuAllocOptionsSet(virDomainDefPtr def,
                    virQEMUCapsPtr qemuCaps)
{
    virAllocOptionsPtr allocOpts = NULL;

    if (!(allocOpts = virAllocOptionsCreate()))
        goto error;

    if (qemuAllocOptionsFill(allocOpts, qemuCaps) < 1)
        goto error;

    virAllocOptionsFree(def->allocOpts);
    def->allocOpts = allocOpts;

    return 0;

 error:
    virAllocOptionsFree(allocOpts);
    return -1;
}
