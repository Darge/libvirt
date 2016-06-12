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
qemuDomainValidateDevicePCISlotsPIIX3(virDomainDefPtr def,
                                      virQEMUCapsPtr qemuCaps,
                                      virDomainPCIAddressSetPtr addrs)
{
    int ret = -1;
    size_t i;
    virPCIDeviceAddress tmp_addr;
    bool qemuDeviceVideoUsable = virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIDEO_PRIMARY);
    char *addrStr = NULL;
    virDomainPCIConnectFlags flags = (VIR_PCI_CONNECT_HOTPLUGGABLE
                                      | VIR_PCI_CONNECT_TYPE_PCI_DEVICE);

    /* Verify that first IDE and USB controllers (if any) is on the PIIX3, fn 1 */
    for (i = 0; i < def->ncontrollers; i++) {
        /* First IDE controller lives on the PIIX3 at slot=1, function=1 */
        if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_IDE &&
            def->controllers[i]->idx == 0) {
            if (virDeviceInfoPCIAddressPresent(&def->controllers[i]->info)) {
                if (def->controllers[i]->info.addr.pci.domain != 0 ||
                    def->controllers[i]->info.addr.pci.bus != 0 ||
                    def->controllers[i]->info.addr.pci.slot != 1 ||
                    def->controllers[i]->info.addr.pci.function != 1) {
                    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                                   _("Primary IDE controller must have PCI address 0:0:1.1"));
                    goto cleanup;
                }
            } else {
                def->controllers[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI;
                def->controllers[i]->info.addr.pci.domain = 0;
                def->controllers[i]->info.addr.pci.bus = 0;
                def->controllers[i]->info.addr.pci.slot = 1;
                def->controllers[i]->info.addr.pci.function = 1;
            }
        } else if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_USB &&
                   def->controllers[i]->idx == 0 &&
                   (def->controllers[i]->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_PIIX3_UHCI ||
                    def->controllers[i]->model == -1)) {
            if (virDeviceInfoPCIAddressPresent(&def->controllers[i]->info)) {
                if (def->controllers[i]->info.addr.pci.domain != 0 ||
                    def->controllers[i]->info.addr.pci.bus != 0 ||
                    def->controllers[i]->info.addr.pci.slot != 1 ||
                    def->controllers[i]->info.addr.pci.function != 2) {
                    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                                   _("PIIX3 USB controller must have PCI address 0:0:1.2"));
                    goto cleanup;
                }
            } else {
                def->controllers[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI;
                def->controllers[i]->info.addr.pci.domain = 0;
                def->controllers[i]->info.addr.pci.bus = 0;
                def->controllers[i]->info.addr.pci.slot = 1;
                def->controllers[i]->info.addr.pci.function = 2;
            }
        }
    }

    /* PIIX3 (ISA bridge, IDE controller, something else unknown, USB controller)
     * hardcoded slot=1, multifunction device
     */
    if (addrs->nbuses) {
        memset(&tmp_addr, 0, sizeof(tmp_addr));
        tmp_addr.slot = 1;
        if (virDomainPCIAddressReserveSlot(addrs, &tmp_addr, flags) < 0)
            goto cleanup;
    }

    if (def->nvideos > 0) {
        /* Because the PIIX3 integrated IDE/USB controllers are
         * already at slot 1, when qemu looks for the first free slot
         * to place the VGA controller (which is always the first
         * device added after integrated devices), it *always* ends up
         * at slot 2.
         */
        virDomainVideoDefPtr primaryVideo = def->videos[0];
        if (virDeviceInfoPCIAddressWanted(&primaryVideo->info)) {
            memset(&tmp_addr, 0, sizeof(tmp_addr));
            tmp_addr.slot = 2;

            if (!(addrStr = virDomainPCIAddressAsString(&tmp_addr)))
                goto cleanup;
            if (!virDomainPCIAddressValidate(addrs, &tmp_addr,
                                             addrStr, flags, false))
                goto cleanup;

            if (virDomainPCIAddressSlotInUse(addrs, &tmp_addr)) {
                if (qemuDeviceVideoUsable) {
                    if (virDomainPCIAddressReserveNextSlot(addrs,
                                                           &primaryVideo->info,
                                                           flags) < 0)
                        goto cleanup;
                } else {
                    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                                   _("PCI address 0:0:2.0 is in use, "
                                     "QEMU needs it for primary video"));
                    goto cleanup;
                }
            } else {
                if (virDomainPCIAddressReserveSlot(addrs, &tmp_addr, flags) < 0)
                    goto cleanup;
                primaryVideo->info.addr.pci = tmp_addr;
                primaryVideo->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI;
            }
        } else if (!qemuDeviceVideoUsable) {
            if (primaryVideo->info.addr.pci.domain != 0 ||
                primaryVideo->info.addr.pci.bus != 0 ||
                primaryVideo->info.addr.pci.slot != 2 ||
                primaryVideo->info.addr.pci.function != 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("Primary video card must have PCI address 0:0:2.0"));
                goto cleanup;
            }
            /* If TYPE == PCI, then virDomainCollectPCIAddress() function
             * has already reserved the address, so we must skip */
        }
    } else if (addrs->nbuses && !qemuDeviceVideoUsable) {
        memset(&tmp_addr, 0, sizeof(tmp_addr));
        tmp_addr.slot = 2;

        if (virDomainPCIAddressSlotInUse(addrs, &tmp_addr)) {
            VIR_DEBUG("PCI address 0:0:2.0 in use, future addition of a video"
                      " device will not be possible without manual"
                      " intervention");
        } else if (virDomainPCIAddressReserveSlot(addrs, &tmp_addr, flags) < 0) {
            goto cleanup;
        }
    }
    ret = 0;
 cleanup:
    VIR_FREE(addrStr);
    return ret;
}


static int
qemuDomainValidateDevicePCISlotsQ35(virDomainDefPtr def,
                                    virQEMUCapsPtr qemuCaps,
                                    virDomainPCIAddressSetPtr addrs)
{
    int ret = -1;
    size_t i;
    virPCIDeviceAddress tmp_addr;
    bool qemuDeviceVideoUsable = virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIDEO_PRIMARY);
    char *addrStr = NULL;
    virDomainPCIConnectFlags flags = VIR_PCI_CONNECT_TYPE_PCIE_DEVICE;

    for (i = 0; i < def->ncontrollers; i++) {
        switch (def->controllers[i]->type) {
        case VIR_DOMAIN_CONTROLLER_TYPE_SATA:
            /* Verify that the first SATA controller is at 00:1F.2 the
             * q35 machine type *always* has a SATA controller at this
             * address.
             */
            if (def->controllers[i]->idx == 0) {
                if (virDeviceInfoPCIAddressPresent(&def->controllers[i]->info)) {
                    if (def->controllers[i]->info.addr.pci.domain != 0 ||
                        def->controllers[i]->info.addr.pci.bus != 0 ||
                        def->controllers[i]->info.addr.pci.slot != 0x1F ||
                        def->controllers[i]->info.addr.pci.function != 2) {
                        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                                       _("Primary SATA controller must have PCI address 0:0:1f.2"));
                        goto cleanup;
                    }
                } else {
                    def->controllers[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI;
                    def->controllers[i]->info.addr.pci.domain = 0;
                    def->controllers[i]->info.addr.pci.bus = 0;
                    def->controllers[i]->info.addr.pci.slot = 0x1F;
                    def->controllers[i]->info.addr.pci.function = 2;
                }
            }
            break;

        case VIR_DOMAIN_CONTROLLER_TYPE_USB:
            if ((def->controllers[i]->model
                 == VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_UHCI1) &&
                (def->controllers[i]->info.type
                 == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE)) {
                /* Try to assign the first found USB2 controller to
                 * 00:1D.0 and 2nd to 00:1A.0 (because that is their
                 * standard location on real Q35 hardware) unless they
                 * are already taken, but don't insist on it.
                 *
                 * (NB: all other controllers at the same index will
                 * get assigned to the same slot as the UHCI1 when
                 * addresses are later assigned to all devices.)
                 */
                bool assign = false;

                memset(&tmp_addr, 0, sizeof(tmp_addr));
                tmp_addr.slot = 0x1D;
                if (!virDomainPCIAddressSlotInUse(addrs, &tmp_addr)) {
                    assign = true;
                } else {
                    tmp_addr.slot = 0x1A;
                    if (!virDomainPCIAddressSlotInUse(addrs, &tmp_addr))
                        assign = true;
                }
                if (assign) {
                    if (virDomainPCIAddressReserveAddr(addrs, &tmp_addr,
                                                       flags, false, true) < 0)
                        goto cleanup;
                    def->controllers[i]->info.type
                        = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI;
                    def->controllers[i]->info.addr.pci.domain = 0;
                    def->controllers[i]->info.addr.pci.bus = 0;
                    def->controllers[i]->info.addr.pci.slot = tmp_addr.slot;
                    def->controllers[i]->info.addr.pci.function = 0;
                    def->controllers[i]->info.addr.pci.multi
                       = VIR_TRISTATE_SWITCH_ON;
                }
            }
            break;

        case VIR_DOMAIN_CONTROLLER_TYPE_PCI:
            if (def->controllers[i]->model == VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE &&
                def->controllers[i]->info.type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_NONE) {
                /* Try to assign this bridge to 00:1E.0 (because that
                * is its standard location on real hardware) unless
                * it's already taken, but don't insist on it.
                */
                memset(&tmp_addr, 0, sizeof(tmp_addr));
                tmp_addr.slot = 0x1E;
                if (!virDomainPCIAddressSlotInUse(addrs, &tmp_addr)) {
                    if (virDomainPCIAddressReserveAddr(addrs, &tmp_addr,
                                                       flags, true, false) < 0)
                        goto cleanup;
                    def->controllers[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI;
                    def->controllers[i]->info.addr.pci.domain = 0;
                    def->controllers[i]->info.addr.pci.bus = 0;
                    def->controllers[i]->info.addr.pci.slot = 0x1E;
                    def->controllers[i]->info.addr.pci.function = 0;
                }
            }
            break;
        }
    }

    /* Reserve slot 0x1F function 0 (ISA bridge, not in config model)
     * and function 3 (SMBus, also not (yet) in config model). As with
     * the SATA controller, these devices are always present in a q35
     * machine; there is no way to not have them.
     */
    if (addrs->nbuses) {
        memset(&tmp_addr, 0, sizeof(tmp_addr));
        tmp_addr.slot = 0x1F;
        tmp_addr.function = 0;
        tmp_addr.multi = VIR_TRISTATE_SWITCH_ON;
        if (virDomainPCIAddressReserveAddr(addrs, &tmp_addr, flags,
                                           false, false) < 0)
           goto cleanup;
        tmp_addr.function = 3;
        tmp_addr.multi = VIR_TRISTATE_SWITCH_ABSENT;
        if (virDomainPCIAddressReserveAddr(addrs, &tmp_addr, flags,
                                           false, false) < 0)
           goto cleanup;
    }

    if (def->nvideos > 0) {
        /* NB: unlike the pc machinetypes, on q35 machinetypes the
         * integrated devices are at slot 0x1f, so when qemu looks for
         * the first free lot for the first VGA, it will always be at
         * slot 1 (which was used up by the integrated PIIX3 devices
         * on pc machinetypes).
         */
        virDomainVideoDefPtr primaryVideo = def->videos[0];
        if (virDeviceInfoPCIAddressWanted(&primaryVideo->info)) {
            memset(&tmp_addr, 0, sizeof(tmp_addr));
            tmp_addr.slot = 1;

            if (!(addrStr = virDomainPCIAddressAsString(&tmp_addr)))
                goto cleanup;
            if (!virDomainPCIAddressValidate(addrs, &tmp_addr,
                                             addrStr, flags, false))
                goto cleanup;

            if (virDomainPCIAddressSlotInUse(addrs, &tmp_addr)) {
                if (qemuDeviceVideoUsable) {
                    if (virDomainPCIAddressReserveNextSlot(addrs,
                                                           &primaryVideo->info,
                                                           flags) < 0)
                        goto cleanup;
                } else {
                    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                                   _("PCI address 0:0:1.0 is in use, "
                                     "QEMU needs it for primary video"));
                    goto cleanup;
                }
            } else {
                if (virDomainPCIAddressReserveSlot(addrs, &tmp_addr, flags) < 0)
                    goto cleanup;
                primaryVideo->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI;
                primaryVideo->info.addr.pci = tmp_addr;
            }
        } else if (!qemuDeviceVideoUsable) {
            if (primaryVideo->info.addr.pci.domain != 0 ||
                primaryVideo->info.addr.pci.bus != 0 ||
                primaryVideo->info.addr.pci.slot != 1 ||
                primaryVideo->info.addr.pci.function != 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("Primary video card must have PCI address 0:0:1.0"));
                goto cleanup;
            }
            /* If TYPE == PCI, then virDomainCollectPCIAddress() function
             * has already reserved the address, so we must skip */
        }
    } else if (addrs->nbuses && !qemuDeviceVideoUsable) {
        memset(&tmp_addr, 0, sizeof(tmp_addr));
        tmp_addr.slot = 1;

        if (virDomainPCIAddressSlotInUse(addrs, &tmp_addr)) {
            VIR_DEBUG("PCI address 0:0:1.0 in use, future addition of a video"
                      " device will not be possible without manual"
                      " intervention");
            virResetLastError();
        } else if (virDomainPCIAddressReserveSlot(addrs, &tmp_addr, flags) < 0) {
            goto cleanup;
        }
    }
    ret = 0;
 cleanup:
    VIR_FREE(addrStr);
    return ret;
}


static int
qemuDomainValidateDevicePCISlotsChipsets(virDomainDefPtr def,
                                         virQEMUCapsPtr qemuCaps,
                                         virDomainPCIAddressSetPtr addrs)
{
    if (qemuDomainMachineIsI440FX(def) &&
        qemuDomainValidateDevicePCISlotsPIIX3(def, qemuCaps, addrs) < 0) {
        return -1;
    }

    if (qemuDomainMachineIsQ35(def) &&
        qemuDomainValidateDevicePCISlotsQ35(def, qemuCaps, addrs) < 0) {
        return -1;
    }

    return 0;
}


/*
 * This assigns static PCI slots to all configured devices.
 * The ordering here is chosen to match the ordering used
 * with old QEMU < 0.12, so that if a user updates a QEMU
 * host from old QEMU to QEMU >= 0.12, their guests should
 * get PCI addresses in the same order as before.
 *
 * NB, if they previously hotplugged devices then all bets
 * are off. Hotplug for old QEMU was unfixably broken wrt
 * to stable PCI addressing.
 *
 * Order is:
 *
 *  - Host bridge (slot 0)
 *  - PIIX3 ISA bridge, IDE controller, something else unknown, USB controller (slot 1)
 *  - Video (slot 2)
 *
 *  - These integrated devices were already added by
 *    qemuValidateDevicePCISlotsChipsets invoked right before this function
 *
 * Incrementally assign slots from 3 onwards:
 *
 *  - Net
 *  - Sound
 *  - SCSI controllers
 *  - VirtIO block
 *  - VirtIO balloon
 *  - Host device passthrough
 *  - Watchdog
 *  - pci serial devices
 *
 * Prior to this function being invoked, virDomainCollectPCIAddress() will have
 * added all existing PCI addresses from the 'def' to 'addrs'. Thus this
 * function must only try to reserve addresses if info.type == NONE and
 * skip over info.type == PCI
 */
static int
qemuDomainAssignDevicePCISlots(virDomainDefPtr def,
                               virQEMUCapsPtr qemuCaps,
                               virDomainPCIAddressSetPtr addrs)
{
    size_t i, j;
    virDomainPCIConnectFlags flags = 0; /* initialize to quiet gcc warning */
    virPCIDeviceAddress tmp_addr;

    /* PCI controllers */
    for (i = 0; i < def->ncontrollers; i++) {
        if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_PCI) {
            virDomainControllerModelPCI model = def->controllers[i]->model;

            if (model == VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT ||
                model == VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT ||
                !virDeviceInfoPCIAddressWanted(&def->controllers[i]->info))
                continue;

            /* convert the type of controller into a "CONNECT_TYPE"
             * flag to use when searching for the proper
             * controller/bus to connect it to on the upstream side.
             */
            flags = virDomainPCIControllerModelToConnectType(model);
            if (virDomainPCIAddressReserveNextSlot(addrs,
                                                   &def->controllers[i]->info,
                                                   flags) < 0)
                goto error;
        }
    }

    /* all other devices that plug into a PCI slot are treated as a
     * PCI endpoint devices that require a hotplug-capable slot
     * (except for some special cases which have specific handling
     * below)
     */
    flags = VIR_PCI_CONNECT_HOTPLUGGABLE | VIR_PCI_CONNECT_TYPE_PCI_DEVICE;

    for (i = 0; i < def->nfss; i++) {
        if (!virDeviceInfoPCIAddressWanted(&def->fss[i]->info))
            continue;

        /* Only support VirtIO-9p-pci so far. If that changes,
         * we might need to skip devices here */
        if (virDomainPCIAddressReserveNextSlot(addrs, &def->fss[i]->info,
                                               flags) < 0)
            goto error;
    }

    /* Network interfaces */
    for (i = 0; i < def->nnets; i++) {
        /* type='hostdev' network devices might be USB, and are also
         * in hostdevs list anyway, so handle them with other hostdevs
         * instead of here.
         */
        if ((def->nets[i]->type == VIR_DOMAIN_NET_TYPE_HOSTDEV) ||
            !virDeviceInfoPCIAddressWanted(&def->nets[i]->info)) {
            continue;
        }
        if (virDomainPCIAddressReserveNextSlot(addrs, &def->nets[i]->info,
                                               flags) < 0)
            goto error;
    }

    /* Sound cards */
    for (i = 0; i < def->nsounds; i++) {
        if (!virDeviceInfoPCIAddressWanted(&def->sounds[i]->info))
            continue;
        /* Skip ISA sound card, PCSPK and usb-audio */
        if (def->sounds[i]->model == VIR_DOMAIN_SOUND_MODEL_SB16 ||
            def->sounds[i]->model == VIR_DOMAIN_SOUND_MODEL_PCSPK ||
            def->sounds[i]->model == VIR_DOMAIN_SOUND_MODEL_USB)
            continue;

        if (virDomainPCIAddressReserveNextSlot(addrs, &def->sounds[i]->info,
                                               flags) < 0)
            goto error;
    }

    /* Device controllers (SCSI, USB, but not IDE, FDC or CCID) */
    for (i = 0; i < def->ncontrollers; i++) {
        /* PCI controllers have been dealt with earlier */
        if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_PCI)
            continue;

        /* USB controller model 'none' doesn't need a PCI address */
        if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_USB &&
            def->controllers[i]->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_NONE)
            continue;

        /* FDC lives behind the ISA bridge; CCID is a usb device */
        if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_FDC ||
            def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_CCID)
            continue;

        /* First IDE controller lives on the PIIX3 at slot=1, function=1,
           dealt with earlier on*/
        if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_IDE &&
            def->controllers[i]->idx == 0)
            continue;

        if (!virDeviceInfoPCIAddressWanted(&def->controllers[i]->info))
            continue;

        /* USB2 needs special handling to put all companions in the same slot */
        if (IS_USB2_CONTROLLER(def->controllers[i])) {
            virPCIDeviceAddress addr = { 0, 0, 0, 0, false };
            bool foundAddr = false;

            memset(&tmp_addr, 0, sizeof(tmp_addr));
            for (j = 0; j < def->ncontrollers; j++) {
                if (IS_USB2_CONTROLLER(def->controllers[j]) &&
                    def->controllers[j]->idx == def->controllers[i]->idx &&
                    virDeviceInfoPCIAddressPresent(&def->controllers[j]->info)) {
                    addr = def->controllers[j]->info.addr.pci;
                    foundAddr = true;
                    break;
                }
            }

            switch (def->controllers[i]->model) {
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_EHCI1:
                addr.function = 7;
                addr.multi = VIR_TRISTATE_SWITCH_ABSENT;
                break;
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_UHCI1:
                addr.function = 0;
                addr.multi = VIR_TRISTATE_SWITCH_ON;
                break;
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_UHCI2:
                addr.function = 1;
                addr.multi = VIR_TRISTATE_SWITCH_ABSENT;
                break;
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_UHCI3:
                addr.function = 2;
                addr.multi = VIR_TRISTATE_SWITCH_ABSENT;
                break;
            }

            if (!foundAddr) {
                /* This is the first part of the controller, so need
                 * to find a free slot & then reserve a function */
                if (virDomainPCIAddressGetNextSlot(addrs, &tmp_addr, flags) < 0)
                    goto error;

                addr.bus = tmp_addr.bus;
                addr.slot = tmp_addr.slot;

                addrs->lastaddr = addr;
                addrs->lastaddr.function = 0;
                addrs->lastaddr.multi = VIR_TRISTATE_SWITCH_ABSENT;
            }
            /* Finally we can reserve the slot+function */
            if (virDomainPCIAddressReserveAddr(addrs, &addr, flags,
                                               false, foundAddr) < 0)
                goto error;

            def->controllers[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI;
            def->controllers[i]->info.addr.pci = addr;
        } else {
            if (virDomainPCIAddressReserveNextSlot(addrs,
                                                   &def->controllers[i]->info,
                                                   flags) < 0)
                goto error;
        }
    }

    /* Disks (VirtIO only for now) */
    for (i = 0; i < def->ndisks; i++) {
        /* Only VirtIO disks use PCI addrs */
        if (def->disks[i]->bus != VIR_DOMAIN_DISK_BUS_VIRTIO)
            continue;

        /* don't touch s390 devices */
        if (virDeviceInfoPCIAddressPresent(&def->disks[i]->info) ||
            def->disks[i]->info.type ==
            VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_S390 ||
            def->disks[i]->info.type ==
            VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCW)
            continue;

        /* Also ignore virtio-mmio disks if our machine allows them */
        if (def->disks[i]->info.type ==
            VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_MMIO &&
            virQEMUCapsGet(qemuCaps, QEMU_CAPS_DEVICE_VIRTIO_MMIO))
            continue;

        if (!virDeviceInfoPCIAddressWanted(&def->disks[i]->info)) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("virtio disk cannot have an address of type '%s'"),
                           virDomainDeviceAddressTypeToString(def->disks[i]->info.type));
            goto error;
        }

        if (virDomainPCIAddressReserveNextSlot(addrs, &def->disks[i]->info,
                                               flags) < 0)
            goto error;
    }

    /* Host PCI devices */
    for (i = 0; i < def->nhostdevs; i++) {
        if (!virDeviceInfoPCIAddressWanted(def->hostdevs[i]->info))
            continue;
        if (def->hostdevs[i]->mode != VIR_DOMAIN_HOSTDEV_MODE_SUBSYS ||
            def->hostdevs[i]->source.subsys.type != VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_PCI)
            continue;

        if (virDomainPCIAddressReserveNextSlot(addrs,
                                               def->hostdevs[i]->info,
                                               flags) < 0)
            goto error;
    }

    /* VirtIO balloon */
    if (def->memballoon &&
        def->memballoon->model == VIR_DOMAIN_MEMBALLOON_MODEL_VIRTIO &&
        virDeviceInfoPCIAddressWanted(&def->memballoon->info)) {
        if (virDomainPCIAddressReserveNextSlot(addrs,
                                               &def->memballoon->info,
                                               flags) < 0)
            goto error;
    }

    /* VirtIO RNG */
    for (i = 0; i < def->nrngs; i++) {
        if (def->rngs[i]->model != VIR_DOMAIN_RNG_MODEL_VIRTIO ||
            !virDeviceInfoPCIAddressWanted(&def->rngs[i]->info))
            continue;

        if (virDomainPCIAddressReserveNextSlot(addrs,
                                               &def->rngs[i]->info, flags) < 0)
            goto error;
    }

    /* A watchdog - check if it is a PCI device */
    if (def->watchdog &&
        def->watchdog->model == VIR_DOMAIN_WATCHDOG_MODEL_I6300ESB &&
        virDeviceInfoPCIAddressWanted(&def->watchdog->info)) {
        if (virDomainPCIAddressReserveNextSlot(addrs, &def->watchdog->info,
                                               flags) < 0)
            goto error;
    }

    /* Assign a PCI slot to the primary video card if there is not an
     * assigned address. */
    if (def->nvideos > 0 &&
        virDeviceInfoPCIAddressWanted(&def->videos[0]->info)) {
        if (virDomainPCIAddressReserveNextSlot(addrs, &def->videos[0]->info,
                                               flags) < 0)
            goto error;
    }

    /* Further non-primary video cards which have to be qxl type */
    for (i = 1; i < def->nvideos; i++) {
        if (def->videos[i]->type != VIR_DOMAIN_VIDEO_TYPE_QXL) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("non-primary video device must be type of 'qxl'"));
            goto error;
        }
        if (!virDeviceInfoPCIAddressWanted(&def->videos[i]->info))
            continue;
        if (virDomainPCIAddressReserveNextSlot(addrs, &def->videos[i]->info,
                                               flags) < 0)
            goto error;
    }

    /* Shared Memory */
    for (i = 0; i < def->nshmems; i++) {
        if (!virDeviceInfoPCIAddressWanted(&def->shmems[i]->info))
            continue;

        if (virDomainPCIAddressReserveNextSlot(addrs,
                                               &def->shmems[i]->info, flags) < 0)
            goto error;
    }
    for (i = 0; i < def->ninputs; i++) {
        if (def->inputs[i]->bus != VIR_DOMAIN_INPUT_BUS_VIRTIO ||
            !virDeviceInfoPCIAddressWanted(&def->inputs[i]->info))
            continue;

        if (virDomainPCIAddressReserveNextSlot(addrs,
                                               &def->inputs[i]->info, flags) < 0)
            goto error;
    }
    for (i = 0; i < def->nparallels; i++) {
        /* Nada - none are PCI based (yet) */
    }
    for (i = 0; i < def->nserials; i++) {
        virDomainChrDefPtr chr = def->serials[i];

        if (chr->targetType != VIR_DOMAIN_CHR_SERIAL_TARGET_TYPE_PCI ||
            !virDeviceInfoPCIAddressWanted(&chr->info))
            continue;

        if (virDomainPCIAddressReserveNextSlot(addrs, &chr->info, flags) < 0)
            goto error;
    }
    for (i = 0; i < def->nchannels; i++) {
        /* Nada - none are PCI based (yet) */
    }
    for (i = 0; i < def->nhubs; i++) {
        /* Nada - none are PCI based (yet) */
    }

    return 0;

 error:
    return -1;
}


static bool
qemuDomainSupportsPCI(virDomainDefPtr def,
                      virQEMUCapsPtr qemuCaps)
{
    if ((def->os.arch != VIR_ARCH_ARMV7L) && (def->os.arch != VIR_ARCH_AARCH64))
        return true;

    if (STREQ(def->os.machine, "versatilepb"))
        return true;

    if (virDomainMachineIsVirt(def) &&
        virQEMUCapsGet(qemuCaps, QEMU_CAPS_OBJECT_GPEX))
        return true;

    return false;
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

        if (qemuDomainValidateDevicePCISlotsChipsets(def, qemuCaps,
                                                     addrs) < 0)
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

        if (qemuDomainAssignDevicePCISlots(def, qemuCaps, addrs) < 0)
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

    if (qemuDomainSupportsPCI(def, qemuCaps)) {
        if (qemuDomainValidateDevicePCISlotsChipsets(def, qemuCaps,
                                                     addrs) < 0)
            goto cleanup;

        if (qemuDomainAssignDevicePCISlots(def, qemuCaps, addrs) < 0)
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

