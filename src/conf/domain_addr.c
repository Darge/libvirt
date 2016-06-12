/*
 * domain_addr.c: helper APIs for managing domain device addresses
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

#include "viralloc.h"
#include "virlog.h"
#include "virstring.h"
#include "domain_addr.h"

#define VIR_FROM_THIS VIR_FROM_DOMAIN

VIR_LOG_INIT("conf.domain_addr");

#define VIO_ADDR_NET 0x1000ul
#define VIO_ADDR_SCSI 0x2000ul
#define VIO_ADDR_SERIAL 0x30000000ul
#define VIO_ADDR_NVRAM 0x3000ul

virDomainPCIConnectFlags
virDomainPCIControllerModelToConnectType(virDomainControllerModelPCI model)
{
    /* given a VIR_DOMAIN_CONTROLLER_MODEL_PCI*, set connectType to
     * the equivalent VIR_PCI_CONNECT_TYPE_*. return 0 on success, -1
     * if the model wasn't recognized.
     */
    switch (model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
        /* pci-root and pcie-root are implicit in the machine,
         * and have no upstream connection, "last" will never actually
         * happen, it's just there so that all possible cases are
         * covered in the switch (keeps the compiler happy).
         */
        return 0;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
        /* pci-bridge and pci-expander-bus are treated like a standard
         * PCI endpoint device, because they can plug into any
         * standard PCI slot.
         */
        return VIR_PCI_CONNECT_TYPE_PCI_DEVICE;

    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
        /* dmi-to-pci-bridge and pcie-expander-bus are treated like
         * PCIe devices (the part of pcie-expander-bus that is plugged
         * in isn't the expander bus itself, but a companion device
         * used for setting it up).
         */
        return VIR_PCI_CONNECT_TYPE_PCIE_DEVICE;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
        return VIR_PCI_CONNECT_TYPE_PCIE_ROOT_PORT;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
        return VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_UPSTREAM_PORT;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
        return VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_DOWNSTREAM_PORT;

        /* if this happens, there is an error in the code. A
         * PCI controller should always have a proper model
         * set
         */
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("PCI controller model incorrectly set to 'last'"));
        return -1;
    }
    return 0;
}

bool
virDomainPCIAddressFlagsCompatible(virPCIDeviceAddressPtr addr,
                                   const char *addrStr,
                                   virDomainPCIConnectFlags busFlags,
                                   virDomainPCIConnectFlags devFlags,
                                   bool reportError,
                                   bool fromConfig)
{
    virErrorNumber errType = (fromConfig
                              ? VIR_ERR_XML_ERROR : VIR_ERR_INTERNAL_ERROR);

    if (fromConfig) {
        /* If the requested connection was manually specified in
         * config, allow a PCI device to connect to a PCIe slot, or
         * vice versa.
         */
        if (busFlags & VIR_PCI_CONNECT_TYPES_ENDPOINT)
            busFlags |= VIR_PCI_CONNECT_TYPES_ENDPOINT;
        /* Also allow manual specification of bus to override
         * libvirt's assumptions about whether or not hotplug
         * capability will be required.
         */
        if (devFlags & VIR_PCI_CONNECT_HOTPLUGGABLE)
            busFlags |= VIR_PCI_CONNECT_HOTPLUGGABLE;
    }

    /* If this bus doesn't allow the type of connection (PCI
     * vs. PCIe) required by the device, or if the device requires
     * hot-plug and this bus doesn't have it, return false.
     */
    if (!(devFlags & busFlags & VIR_PCI_CONNECT_TYPES_MASK)) {
        if (reportError) {
            if (devFlags & VIR_PCI_CONNECT_TYPE_PCI_DEVICE) {
                virReportError(errType,
                               _("PCI bus is not compatible with the device "
                                 "at %s. Device requires a standard PCI slot, "
                                 "which is not provided by bus %.4x:%.2x"),
                               addrStr, addr->domain, addr->bus);
            } else if (devFlags & VIR_PCI_CONNECT_TYPE_PCIE_DEVICE) {
                virReportError(errType,
                               _("PCI bus is not compatible with the device "
                                 "at %s. Device requires a PCI Express slot, "
                                 "which is not provided by bus %.4x:%.2x"),
                               addrStr, addr->domain, addr->bus);
            } else {
                /* this should never happen. If it does, there is a
                 * bug in the code that sets the flag bits for devices.
                 */
                virReportError(errType,
                           _("The device information for %s has no PCI "
                             "connection types listed"), addrStr);
            }
        }
        return false;
    }
    if ((devFlags & VIR_PCI_CONNECT_HOTPLUGGABLE) &&
        !(busFlags & VIR_PCI_CONNECT_HOTPLUGGABLE)) {
        if (reportError) {
            virReportError(errType,
                           _("PCI bus is not compatible with the device "
                             "at %s. Device requires hot-plug capability, "
                             "which is not provided by bus %.4x:%.2x"),
                           addrStr, addr->domain, addr->bus);
        }
        return false;
    }
    return true;
}


/* Verify that the address is in bounds for the chosen bus, and
 * that the bus is of the correct type for the device (via
 * comparing the flags).
 */
bool
virDomainPCIAddressValidate(virDomainPCIAddressSetPtr addrs,
                            virPCIDeviceAddressPtr addr,
                            const char *addrStr,
                            virDomainPCIConnectFlags flags,
                            bool fromConfig)
{
    virDomainPCIAddressBusPtr bus;
    virErrorNumber errType = (fromConfig
                              ? VIR_ERR_XML_ERROR : VIR_ERR_INTERNAL_ERROR);

    if (addrs->nbuses == 0) {
        virReportError(errType, "%s", _("No PCI buses available"));
        return false;
    }
    if (addr->domain != 0) {
        virReportError(errType,
                       _("Invalid PCI address %s. "
                         "Only PCI domain 0 is available"),
                       addrStr);
        return false;
    }
    if (addr->bus >= addrs->nbuses) {
        virReportError(errType,
                       _("Invalid PCI address %s. "
                         "Only PCI buses up to %zu are available"),
                       addrStr, addrs->nbuses - 1);
        return false;
    }

    bus = &addrs->buses[addr->bus];

    /* assure that at least one of the requested connection types is
     * provided by this bus
     */
    if (!virDomainPCIAddressFlagsCompatible(addr, addrStr, bus->flags,
                                            flags, true, fromConfig))
        return false;

    /* some "buses" are really just a single port */
    if (bus->minSlot && addr->slot < bus->minSlot) {
        virReportError(errType,
                       _("Invalid PCI address %s. slot must be >= %zu"),
                       addrStr, bus->minSlot);
        return false;
    }
    if (addr->slot > bus->maxSlot) {
        virReportError(errType,
                       _("Invalid PCI address %s. slot must be <= %zu"),
                       addrStr, bus->maxSlot);
        return false;
    }
    if (addr->function > VIR_PCI_ADDRESS_FUNCTION_LAST) {
        virReportError(errType,
                       _("Invalid PCI address %s. function must be <= %u"),
                       addrStr, VIR_PCI_ADDRESS_FUNCTION_LAST);
        return false;
    }
    return true;
}


int
virDomainPCIAddressBusSetModel(virDomainPCIAddressBusPtr bus,
                               virDomainControllerModelPCI model)
{
    /* set flags for what can be connected *downstream* from each
     * bus.
     */
    switch (model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
        bus->flags = (VIR_PCI_CONNECT_HOTPLUGGABLE |
                      VIR_PCI_CONNECT_TYPE_PCI_DEVICE);
        bus->minSlot = 1;
        bus->maxSlot = VIR_PCI_ADDRESS_SLOT_LAST;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
        bus->flags = (VIR_PCI_CONNECT_HOTPLUGGABLE |
                      VIR_PCI_CONNECT_TYPE_PCI_DEVICE);
        bus->minSlot = 0;
        bus->maxSlot = VIR_PCI_ADDRESS_SLOT_LAST;
        break;

    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
        /* slots 1 - 31, no hotplug, PCIe endpoint device or
         * pcie-root-port only, unless the address was specified in
         * user config *and* the particular device being attached also
         * allows it.
         */
        bus->flags = (VIR_PCI_CONNECT_TYPE_PCIE_DEVICE
                      | VIR_PCI_CONNECT_TYPE_PCIE_ROOT_PORT);
        bus->minSlot = 1;
        bus->maxSlot = VIR_PCI_ADDRESS_SLOT_LAST;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
        /* slots 0 - 31, standard PCI slots,
         * but *not* hot-pluggable */
        bus->flags = VIR_PCI_CONNECT_TYPE_PCI_DEVICE;
        bus->minSlot = 0;
        bus->maxSlot = VIR_PCI_ADDRESS_SLOT_LAST;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
        /* provides one slot which is pcie, can be used by endpoint
         * devices and pcie-switch-upstream-ports, and is hotpluggable
         */
        bus->flags = VIR_PCI_CONNECT_TYPE_PCIE_DEVICE
           | VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_UPSTREAM_PORT
           | VIR_PCI_CONNECT_HOTPLUGGABLE;
        bus->minSlot = 0;
        bus->maxSlot = 0;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
        /* 32 slots, can only accept pcie-switch-downstrean-ports,
         * no hotplug
         */
        bus->flags = VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_DOWNSTREAM_PORT;
        bus->minSlot = 0;
        bus->maxSlot = VIR_PCI_ADDRESS_SLOT_LAST;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
        /* single slot, no hotplug, only accepts pcie-root-port or
         * pcie-switch-upstream-port.
         */
        bus->flags = (VIR_PCI_CONNECT_TYPE_PCIE_ROOT_PORT
                      | VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_UPSTREAM_PORT);
        bus->minSlot = 0;
        bus->maxSlot = 0;
        break;

    default:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Invalid PCI controller model %d"), model);
        return -1;
    }

    bus->model = model;
    return 0;
}


/* Ensure addr fits in the address set, by expanding it if needed.
 * This will only grow if the flags say that we need a normal
 * hot-pluggable PCI slot. If we need a different type of slot, it
 * will fail.
 *
 * Return value:
 * -1 = OOM
 *  0 = no action performed
 * >0 = number of buses added
 */
int
virDomainPCIAddressSetGrow(virDomainPCIAddressSetPtr addrs,
                           virPCIDeviceAddressPtr addr,
                           virDomainPCIConnectFlags flags)
{
    int add;
    size_t i;

    add = addr->bus - addrs->nbuses + 1;
    i = addrs->nbuses;
    if (add <= 0)
        return 0;

    /* auto-grow only works when we're adding plain PCI devices */
    if (!(flags & VIR_PCI_CONNECT_TYPE_PCI_DEVICE)) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Cannot automatically add a new PCI bus for a "
                         "device requiring a slot other than standard PCI."));
        return -1;
    }

    if (VIR_EXPAND_N(addrs->buses, addrs->nbuses, add) < 0)
        return -1;

    for (; i < addrs->nbuses; i++) {
        /* Any time we auto-add a bus, we will want a multi-slot
         * bus. Currently the only type of bus we will auto-add is a
         * pci-bridge, which is hot-pluggable and provides standard
         * PCI slots.
         */
        virDomainPCIAddressBusSetModel(&addrs->buses[i],
                                       VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE);
    }
    return add;
}


char *
virDomainPCIAddressAsString(virPCIDeviceAddressPtr addr)
{
    char *str;

    ignore_value(virAsprintf(&str, "%.4x:%.2x:%.2x.%.1x",
                             addr->domain,
                             addr->bus,
                             addr->slot,
                             addr->function));
    return str;
}


/*
 * Check if the PCI slot is used by another device.
 */
bool
virDomainPCIAddressSlotInUse(virDomainPCIAddressSetPtr addrs,
                             virPCIDeviceAddressPtr addr)
{
    return !!addrs->buses[addr->bus].slots[addr->slot];
}


/*
 * Reserve a slot (or just one function) for a device. If
 * reserveEntireSlot is true, all functions for the slot are reserved,
 * otherwise only one. If fromConfig is true, the address being
 * requested came directly from the config and errors should be worded
 * appropriately. If fromConfig is false, the address was
 * automatically created by libvirt, so it is an internal error (not
 * XML).
 */
int
virDomainPCIAddressReserveAddr(virDomainPCIAddressSetPtr addrs,
                               virPCIDeviceAddressPtr addr,
                               virDomainPCIConnectFlags flags,
                               bool reserveEntireSlot,
                               bool fromConfig)
{
    int ret = -1;
    char *addrStr = NULL;
    virDomainPCIAddressBusPtr bus;
    virErrorNumber errType = (fromConfig
                              ? VIR_ERR_XML_ERROR : VIR_ERR_INTERNAL_ERROR);

    if (!(addrStr = virDomainPCIAddressAsString(addr)))
        goto cleanup;

    /* Add an extra bus if necessary */
    if (addrs->dryRun && virDomainPCIAddressSetGrow(addrs, addr, flags) < 0)
        goto cleanup;
    /* Check that the requested bus exists, is the correct type, and we
     * are asking for a valid slot
     */
    if (!virDomainPCIAddressValidate(addrs, addr, addrStr, flags, fromConfig))
        goto cleanup;

    bus = &addrs->buses[addr->bus];

    if (reserveEntireSlot) {
        if (bus->slots[addr->slot]) {
            virReportError(errType,
                           _("Attempted double use of PCI slot %s "
                             "(may need \"multifunction='on'\" for "
                             "device on function 0)"), addrStr);
            goto cleanup;
        }
        bus->slots[addr->slot] = 0xFF; /* reserve all functions of slot */
        VIR_DEBUG("Reserving PCI slot %s (multifunction='off')", addrStr);
    } else {
        if (bus->slots[addr->slot] & (1 << addr->function)) {
            if (addr->function == 0) {
                virReportError(errType,
                               _("Attempted double use of PCI Address %s"),
                               addrStr);
            } else {
                virReportError(errType,
                               _("Attempted double use of PCI Address %s "
                                 "(may need \"multifunction='on'\" "
                                 "for device on function 0)"), addrStr);
            }
            goto cleanup;
        }
        bus->slots[addr->slot] |= (1 << addr->function);
        VIR_DEBUG("Reserving PCI address %s", addrStr);
    }

    ret = 0;
 cleanup:
    VIR_FREE(addrStr);
    return ret;
}


int
virDomainPCIAddressReserveSlot(virDomainPCIAddressSetPtr addrs,
                               virPCIDeviceAddressPtr addr,
                               virDomainPCIConnectFlags flags)
{
    return virDomainPCIAddressReserveAddr(addrs, addr, flags, true, false);
}

int
virDomainPCIAddressEnsureAddr(virDomainPCIAddressSetPtr addrs,
                              virDomainDeviceInfoPtr dev)
{
    int ret = -1;
    char *addrStr = NULL;
    /* Flags should be set according to the particular device,
     * but only the caller knows the type of device. Currently this
     * function is only used for hot-plug, though, and hot-plug is
     * only supported for standard PCI devices, so we can safely use
     * the setting below */
    virDomainPCIConnectFlags flags = (VIR_PCI_CONNECT_HOTPLUGGABLE |
                                      VIR_PCI_CONNECT_TYPE_PCI_DEVICE);

    if (!(addrStr = virDomainPCIAddressAsString(&dev->addr.pci)))
        goto cleanup;

    if (virDeviceInfoPCIAddressPresent(dev)) {
        /* We do not support hotplug multi-function PCI device now, so we should
         * reserve the whole slot. The function of the PCI device must be 0.
         */
        if (dev->addr.pci.function != 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Only PCI device addresses with function=0"
                             " are supported"));
            goto cleanup;
        }

        if (!virDomainPCIAddressValidate(addrs, &dev->addr.pci,
                                         addrStr, flags, true))
            goto cleanup;

        ret = virDomainPCIAddressReserveSlot(addrs, &dev->addr.pci, flags);
    } else {
        ret = virDomainPCIAddressReserveNextSlot(addrs, dev, flags);
    }

 cleanup:
    VIR_FREE(addrStr);
    return ret;
}


int
virDomainPCIAddressReleaseAddr(virDomainPCIAddressSetPtr addrs,
                               virPCIDeviceAddressPtr addr)
{
    addrs->buses[addr->bus].slots[addr->slot] &= ~(1 << addr->function);
    return 0;
}

int
virDomainPCIAddressReleaseSlot(virDomainPCIAddressSetPtr addrs,
                               virPCIDeviceAddressPtr addr)
{
    /* permit any kind of connection type in validation, since we
     * already had it, and are giving it back.
     */
    virDomainPCIConnectFlags flags = VIR_PCI_CONNECT_TYPES_MASK;
    int ret = -1;
    char *addrStr = NULL;

    if (!(addrStr = virDomainPCIAddressAsString(addr)))
        goto cleanup;

    if (!virDomainPCIAddressValidate(addrs, addr, addrStr, flags, false))
        goto cleanup;

    addrs->buses[addr->bus].slots[addr->slot] = 0;
    ret = 0;
 cleanup:
    VIR_FREE(addrStr);
    return ret;
}


virDomainPCIAddressSetPtr
virDomainPCIAddressSetAlloc(unsigned int nbuses)
{
    virDomainPCIAddressSetPtr addrs;

    if (VIR_ALLOC(addrs) < 0)
        goto error;

    if (VIR_ALLOC_N(addrs->buses, nbuses) < 0)
        goto error;

    addrs->nbuses = nbuses;
    return addrs;

 error:
    virDomainPCIAddressSetFree(addrs);
    return NULL;
}


int
virDomainPCIAddressGetNextSlot(virDomainPCIAddressSetPtr addrs,
                               virPCIDeviceAddressPtr next_addr,
                               virDomainPCIConnectFlags flags)
{
    /* default to starting the search for a free slot from
     * the first slot of domain 0 bus 0...
     */
    virPCIDeviceAddress a = { 0, 0, 0, 0, false };
    char *addrStr = NULL;

    if (addrs->nbuses == 0) {
        virReportError(VIR_ERR_XML_ERROR, "%s", _("No PCI buses available"));
        goto error;
    }

    /* ...unless this search is for the exact same type of device as
     * last time, then continue the search from the next slot after
     * the previous match (the "next slot" may possibly be the first
     * slot of the next bus).
     */
    if (flags == addrs->lastFlags) {
        a = addrs->lastaddr;
        if (++a.slot > addrs->buses[a.bus].maxSlot &&
            ++a.bus < addrs->nbuses)
            a.slot = addrs->buses[a.bus].minSlot;
    } else {
        a.slot = addrs->buses[0].minSlot;
    }

    while (a.bus < addrs->nbuses) {
        VIR_FREE(addrStr);
        if (!(addrStr = virDomainPCIAddressAsString(&a)))
            goto error;
        if (!virDomainPCIAddressFlagsCompatible(&a, addrStr,
                                                addrs->buses[a.bus].flags,
                                                flags, false, false)) {
            VIR_DEBUG("PCI bus %.4x:%.2x is not compatible with the device",
                      a.domain, a.bus);
        } else {
            while (a.slot <= addrs->buses[a.bus].maxSlot) {
                if (!virDomainPCIAddressSlotInUse(addrs, &a))
                    goto success;

                VIR_DEBUG("PCI slot %.4x:%.2x:%.2x already in use",
                          a.domain, a.bus, a.slot);
                a.slot++;
            }
        }
        if (++a.bus < addrs->nbuses)
            a.slot = addrs->buses[a.bus].minSlot;
    }

    /* There were no free slots after the last used one */
    if (addrs->dryRun) {
        /* a is already set to the first new bus */
        if (virDomainPCIAddressSetGrow(addrs, &a, flags) < 0)
            goto error;
        /* this device will use the first slot of the new bus */
        a.slot = addrs->buses[a.bus].minSlot;
        goto success;
    } else if (flags == addrs->lastFlags) {
        /* Check the buses from 0 up to the last used one */
        for (a.bus = 0; a.bus <= addrs->lastaddr.bus; a.bus++) {
            a.slot = addrs->buses[a.bus].minSlot;
            VIR_FREE(addrStr);
            if (!(addrStr = virDomainPCIAddressAsString(&a)))
                goto error;
            if (!virDomainPCIAddressFlagsCompatible(&a, addrStr,
                                                    addrs->buses[a.bus].flags,
                                                    flags, false, false)) {
                VIR_DEBUG("PCI bus %.4x:%.2x is not compatible with the device",
                          a.domain, a.bus);
            } else {
                while (a.slot <= addrs->buses[a.bus].maxSlot) {
                    if (!virDomainPCIAddressSlotInUse(addrs, &a))
                        goto success;

                    VIR_DEBUG("PCI slot %.4x:%.2x:%.2x already in use",
                              a.domain, a.bus, a.slot);
                    a.slot++;
                }
            }
        }
    }

    virReportError(VIR_ERR_INTERNAL_ERROR,
                   "%s", _("No more available PCI slots"));
 error:
    VIR_FREE(addrStr);
    return -1;

 success:
    VIR_DEBUG("Found free PCI slot %.4x:%.2x:%.2x",
              a.domain, a.bus, a.slot);
    *next_addr = a;
    VIR_FREE(addrStr);
    return 0;
}

int
virDomainPCIAddressReserveNextSlot(virDomainPCIAddressSetPtr addrs,
                                   virDomainDeviceInfoPtr dev,
                                   virDomainPCIConnectFlags flags)
{
    virPCIDeviceAddress addr;
    if (virDomainPCIAddressGetNextSlot(addrs, &addr, flags) < 0)
        return -1;

    if (virDomainPCIAddressReserveSlot(addrs, &addr, flags) < 0)
        return -1;

    if (!addrs->dryRun) {
        dev->type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_PCI;
        dev->addr.pci = addr;
    }

    addrs->lastaddr = addr;
    addrs->lastFlags = flags;
    return 0;
}


static char*
virDomainCCWAddressAsString(virDomainDeviceCCWAddressPtr addr)
{
    char *addrstr = NULL;

    ignore_value(virAsprintf(&addrstr, "%x.%x.%04x",
                             addr->cssid,
                             addr->ssid,
                             addr->devno));
    return addrstr;
}

static int
virDomainCCWAddressIncrement(virDomainDeviceCCWAddressPtr addr)
{
    virDomainDeviceCCWAddress ccwaddr = *addr;

    /* We are not touching subchannel sets and channel subsystems */
    if (++ccwaddr.devno > VIR_DOMAIN_DEVICE_CCW_MAX_DEVNO)
        return -1;

    *addr = ccwaddr;
    return 0;
}


int
virDomainCCWAddressAssign(virDomainDeviceInfoPtr dev,
                          virDomainCCWAddressSetPtr addrs,
                          bool autoassign)
{
    int ret = -1;
    char *addr = NULL;

    if (dev->type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCW)
        return 0;

    if (!autoassign && dev->addr.ccw.assigned) {
        if (!(addr = virDomainCCWAddressAsString(&dev->addr.ccw)))
            goto cleanup;

        if (virHashLookup(addrs->defined, addr)) {
            virReportError(VIR_ERR_XML_ERROR,
                           _("The CCW devno '%s' is in use already "),
                           addr);
            goto cleanup;
        }
    } else if (autoassign && !dev->addr.ccw.assigned) {
        if (!(addr = virDomainCCWAddressAsString(&addrs->next)))
            goto cleanup;

        while (virHashLookup(addrs->defined, addr)) {
            if (virDomainCCWAddressIncrement(&addrs->next) < 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("There are no more free CCW devnos."));
                goto cleanup;
            }
            VIR_FREE(addr);
            if (!(addr = virDomainCCWAddressAsString(&addrs->next)))
                goto cleanup;
        }
        dev->addr.ccw = addrs->next;
        dev->addr.ccw.assigned = true;
    } else {
        return 0;
    }

    if (virHashAddEntry(addrs->defined, addr, addr) < 0)
        goto cleanup;
    else
        addr = NULL; /* memory will be freed by hash table */

    ret = 0;

 cleanup:
    VIR_FREE(addr);
    return ret;
}

int
virDomainCCWAddressAllocate(virDomainDefPtr def ATTRIBUTE_UNUSED,
                            virDomainDeviceDefPtr dev ATTRIBUTE_UNUSED,
                            virDomainDeviceInfoPtr info,
                            void *data)
{
    return virDomainCCWAddressAssign(info, data, true);
}

int
virDomainCCWAddressValidate(virDomainDefPtr def ATTRIBUTE_UNUSED,
                            virDomainDeviceDefPtr dev ATTRIBUTE_UNUSED,
                            virDomainDeviceInfoPtr info,
                            void *data)
{
    return virDomainCCWAddressAssign(info, data, false);
}

int
virDomainCCWAddressReleaseAddr(virDomainCCWAddressSetPtr addrs,
                               virDomainDeviceInfoPtr dev)
{
    char *addr;
    int ret;

    addr = virDomainCCWAddressAsString(&(dev->addr.ccw));
    if (!addr)
        return -1;

    if ((ret = virHashRemoveEntry(addrs->defined, addr)) == 0 &&
        dev->addr.ccw.cssid == addrs->next.cssid &&
        dev->addr.ccw.ssid == addrs->next.ssid &&
        dev->addr.ccw.devno < addrs->next.devno) {
        addrs->next.devno = dev->addr.ccw.devno;
        addrs->next.assigned = false;
    }

    VIR_FREE(addr);

    return ret;
}

virDomainCCWAddressSetPtr
virDomainCCWAddressSetCreate(void)
{
    virDomainCCWAddressSetPtr addrs = NULL;

    if (VIR_ALLOC(addrs) < 0)
        goto error;

    if (!(addrs->defined = virHashCreate(10, virHashValueFree)))
        goto error;

    /* must use cssid = 0xfe (254) for virtio-ccw devices */
    addrs->next.cssid = 254;
    addrs->next.ssid = 0;
    addrs->next.devno = 0;
    addrs->next.assigned = 0;
    return addrs;

 error:
    virDomainCCWAddressSetFree(addrs);
    return NULL;
}


#define VIR_DOMAIN_DEFAULT_VIRTIO_SERIAL_PORTS 31


/* virDomainVirtioSerialAddrSetCreate
 *
 * Allocates an address set for virtio serial addresses
 */
virDomainVirtioSerialAddrSetPtr
virDomainVirtioSerialAddrSetCreate(void)
{
    virDomainVirtioSerialAddrSetPtr ret = NULL;

    if (VIR_ALLOC(ret) < 0)
        return NULL;

    return ret;
}

static ssize_t
virDomainVirtioSerialAddrPlaceController(virDomainVirtioSerialAddrSetPtr addrs,
                                         virDomainVirtioSerialControllerPtr cont)
{
    size_t i;

    for (i = 0; i < addrs->ncontrollers; i++) {
        if (addrs->controllers[i]->idx == cont->idx) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("virtio serial controller with index %u already exists"
                             " in the address set"),
                           cont->idx);
            return -2;
        }
        if (addrs->controllers[i]->idx > cont->idx)
            return i;
    }
    return -1;
}

static ssize_t
virDomainVirtioSerialAddrFindController(virDomainVirtioSerialAddrSetPtr addrs,
                                        unsigned int idx)
{
    size_t i;

    for (i = 0; i < addrs->ncontrollers; i++) {
        if (addrs->controllers[i]->idx == idx)
            return i;
    }
    return -1;
}

/* virDomainVirtioSerialAddrSetAddController
 *
 * Adds virtio serial ports of the existing controller
 * to the address set.
 */
static int
virDomainVirtioSerialAddrSetAddController(virDomainVirtioSerialAddrSetPtr addrs,
                                          virDomainControllerDefPtr cont)
{
    int ret = -1;
    int ports;
    virDomainVirtioSerialControllerPtr cnt = NULL;
    ssize_t insertAt;

    if (cont->type != VIR_DOMAIN_CONTROLLER_TYPE_VIRTIO_SERIAL)
        return 0;

    ports = cont->opts.vioserial.ports;
    if (ports == -1)
        ports = VIR_DOMAIN_DEFAULT_VIRTIO_SERIAL_PORTS;

    VIR_DEBUG("Adding virtio serial controller index %u with %d"
              " ports to the address set", cont->idx, ports);

    if (VIR_ALLOC(cnt) < 0)
        goto cleanup;

    if (!(cnt->ports = virBitmapNew(ports)))
        goto cleanup;
    cnt->idx = cont->idx;

    if ((insertAt = virDomainVirtioSerialAddrPlaceController(addrs, cnt)) < -1)
        goto cleanup;
    if (VIR_INSERT_ELEMENT(addrs->controllers, insertAt,
                           addrs->ncontrollers, cnt) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virDomainVirtioSerialControllerFree(cnt);
    return ret;
}

/* virDomainVirtioSerialAddrSetAddControllers
 *
 * Adds virtio serial ports of controllers present in the domain definition
 * to the address set.
 */
int
virDomainVirtioSerialAddrSetAddControllers(virDomainVirtioSerialAddrSetPtr addrs,
                                           virDomainDefPtr def)
{
    size_t i;

    for (i = 0; i < def->ncontrollers; i++) {
        if (virDomainVirtioSerialAddrSetAddController(addrs,
                                                      def->controllers[i]) < 0)
            return -1;
    }

    return 0;
}

static int
virDomainVirtioSerialAddrSetAutoaddController(virDomainDefPtr def,
                                              virDomainVirtioSerialAddrSetPtr addrs,
                                              unsigned int idx)
{
    int contidx;

    if (virDomainDefMaybeAddController(def,
                                       VIR_DOMAIN_CONTROLLER_TYPE_VIRTIO_SERIAL,
                                       idx, -1) < 0)
        return -1;

    contidx = virDomainControllerFind(def, VIR_DOMAIN_CONTROLLER_TYPE_VIRTIO_SERIAL, idx);

    if (virDomainVirtioSerialAddrSetAddController(addrs, def->controllers[contidx]) < 0)
        return -1;

    return 0;
}

static int
virDomainVirtioSerialAddrNext(virDomainDefPtr def,
                              virDomainVirtioSerialAddrSetPtr addrs,
                              virDomainDeviceVirtioSerialAddress *addr,
                              bool allowZero)
{
    int ret = -1;
    ssize_t port, startPort = 0;
    ssize_t i;
    unsigned int controller;

    /* port number 0 is reserved for virtconsoles */
    if (allowZero)
        startPort = -1;

    if (addrs->ncontrollers == 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("no virtio-serial controllers are available"));
        goto cleanup;
    }

    for (i = 0; i < addrs->ncontrollers; i++) {
        virBitmapPtr map = addrs->controllers[i]->ports;
        if ((port = virBitmapNextClearBit(map, startPort)) >= 0) {
            controller = addrs->controllers[i]->idx;
            goto success;
        }
    }

    if (def) {
        for (i = 0; i < INT_MAX; i++) {
            int idx = virDomainControllerFind(def, VIR_DOMAIN_CONTROLLER_TYPE_VIRTIO_SERIAL, i);

            if (idx == -1) {
                if (virDomainVirtioSerialAddrSetAutoaddController(def, addrs, i) < 0)
                    goto cleanup;
                controller = i;
                port = startPort + 1;
                goto success;
            }
        }
    }

    virReportError(VIR_ERR_XML_ERROR, "%s",
                   _("Unable to find a free virtio-serial port"));

 cleanup:
    return ret;

 success:
    addr->bus = 0;
    addr->port = port;
    addr->controller = controller;
    VIR_DEBUG("Found free virtio serial controller %u port %u", addr->controller,
              addr->port);
    ret = 0;
    goto cleanup;
}

static int
virDomainVirtioSerialAddrNextFromController(virDomainVirtioSerialAddrSetPtr addrs,
                                            virDomainDeviceVirtioSerialAddress *addr)
{
    ssize_t port;
    ssize_t i;
    virBitmapPtr map;

    i = virDomainVirtioSerialAddrFindController(addrs, addr->controller);
    if (i < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("virtio-serial controller %u not available"),
                       addr->controller);
        return -1;
    }

    map = addrs->controllers[i]->ports;
    if ((port = virBitmapNextClearBit(map, 0)) <= 0) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("Unable to find a free port on virtio-serial controller %u"),
                       addr->controller);
        return -1;
    }

    addr->bus = 0;
    addr->port = port;
    VIR_DEBUG("Found free virtio serial controller %u port %u", addr->controller,
              addr->port);
    return 0;
}

/* virDomainVirtioSerialAddrAutoAssign
 *
 * reserve a virtio serial address of the device (if it has one)
 * or assign a virtio serial address to the device
 */
int
virDomainVirtioSerialAddrAutoAssign(virDomainDefPtr def,
                                    virDomainVirtioSerialAddrSetPtr addrs,
                                    virDomainDeviceInfoPtr info,
                                    bool allowZero)
{
    bool portOnly = info->type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_SERIAL;
    if (info->type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_SERIAL &&
        info->addr.vioserial.port)
        return virDomainVirtioSerialAddrReserve(NULL, NULL, info, addrs);
    else
        return virDomainVirtioSerialAddrAssign(def, addrs, info, allowZero, portOnly);
}


int
virDomainVirtioSerialAddrAssign(virDomainDefPtr def,
                                virDomainVirtioSerialAddrSetPtr addrs,
                                virDomainDeviceInfoPtr info,
                                bool allowZero,
                                bool portOnly)
{
    int ret = -1;
    virDomainDeviceInfo nfo = { NULL };
    virDomainDeviceInfoPtr ptr = allowZero ? &nfo : info;

    ptr->type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_SERIAL;

    if (portOnly) {
        if (virDomainVirtioSerialAddrNextFromController(addrs,
                                                        &ptr->addr.vioserial) < 0)
            goto cleanup;
    } else {
        if (virDomainVirtioSerialAddrNext(def, addrs, &ptr->addr.vioserial,
                                          allowZero) < 0)
            goto cleanup;
    }

    if (virDomainVirtioSerialAddrReserve(NULL, NULL, ptr, addrs) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    return ret;
}

/* virDomainVirtioSerialAddrIsComplete
 *
 * Check if the address is complete, or it needs auto-assignment
 */
bool
virDomainVirtioSerialAddrIsComplete(virDomainDeviceInfoPtr info)
{
    return info->type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_SERIAL &&
        info->addr.vioserial.port != 0;
}

/* virDomainVirtioSerialAddrReserve
 *
 * Reserve the virtio serial address of the device
 *
 * For use with virDomainDeviceInfoIterate,
 * opaque should be the address set
 */
int
virDomainVirtioSerialAddrReserve(virDomainDefPtr def ATTRIBUTE_UNUSED,
                                 virDomainDeviceDefPtr dev ATTRIBUTE_UNUSED,
                                 virDomainDeviceInfoPtr info,
                                 void *data)
{
    virDomainVirtioSerialAddrSetPtr addrs = data;
    char *str = NULL;
    int ret = -1;
    virBitmapPtr map = NULL;
    bool b;
    ssize_t i;

    if (!virDomainVirtioSerialAddrIsComplete(info))
        return 0;

    VIR_DEBUG("Reserving virtio serial %u %u", info->addr.vioserial.controller,
              info->addr.vioserial.port);

    i = virDomainVirtioSerialAddrFindController(addrs, info->addr.vioserial.controller);
    if (i < 0) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("virtio serial controller %u is missing"),
                       info->addr.vioserial.controller);
        goto cleanup;
    }

    map = addrs->controllers[i]->ports;
    if (virBitmapGetBit(map, info->addr.vioserial.port, &b) < 0) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("virtio serial controller %u does not have port %u"),
                       info->addr.vioserial.controller,
                       info->addr.vioserial.port);
        goto cleanup;
    }

    if (b) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("virtio serial port %u on controller %u is already occupied"),
                       info->addr.vioserial.port,
                       info->addr.vioserial.controller);
        goto cleanup;
    }

    ignore_value(virBitmapSetBit(map, info->addr.vioserial.port));

    ret = 0;

 cleanup:
    VIR_FREE(str);
    return ret;
}

/* virDomainVirtioSerialAddrRelease
 *
 * Release the virtio serial address of the device
 */
int
virDomainVirtioSerialAddrRelease(virDomainVirtioSerialAddrSetPtr addrs,
                                 virDomainDeviceInfoPtr info)
{
    virBitmapPtr map;
    char *str = NULL;
    int ret = -1;
    ssize_t i;

    if (info->type != VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_SERIAL ||
        info->addr.vioserial.port == 0)
        return 0;

    VIR_DEBUG("Releasing virtio serial %u %u", info->addr.vioserial.controller,
              info->addr.vioserial.port);

    i = virDomainVirtioSerialAddrFindController(addrs, info->addr.vioserial.controller);
    if (i < 0) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("virtio serial controller %u is missing"),
                       info->addr.vioserial.controller);
        goto cleanup;
    }

    map = addrs->controllers[i]->ports;
    if (virBitmapClearBit(map, info->addr.vioserial.port) < 0) {
        virReportError(VIR_ERR_XML_ERROR,
                       _("virtio serial controller %u does not have port %u"),
                       info->addr.vioserial.controller,
                       info->addr.vioserial.port);
        goto cleanup;
    }

    ret = 0;

 cleanup:
    VIR_FREE(str);
    return ret;
}

/* WORK IN PROGRESS */
/* 
    TODO:
    - Add "attribute_nonull" attributes.
    - Change name of virtio_ccw_capability and the second one.
    - Deal with qemuDomainMachineIsS390CCW(), because its qemu specific now.
      Is it really qemu-specific? Same question applies to a few other functions
      I moved.
    - Use virDomainCCWAddressSetFree(priv->ccwaddrs); somewhere.
      Make sure it's used in every place where previously the private data was cleared.
    - I am skeptical about vm->def->vioserialaddrs, because how do we know that we
      really want to refer to def and not the new def?
      The same question applies to vm->def->ccwaddrs
    - What in the previous code was (obj && obj->privateData) was changed
      to nothing in the address handling (it's when the current address sets are assigned)
      Make sure that it's correct.
      These lines:
                virDomainPCIAddressSetFree(def->pciaddrs);
                def->pciaddrs = addrs;
                addrs = NULL;
    - Do I really have to move all these declarations of PCI stuff to domain_conf.h
      just because I want to run a function that clears the pci address set?
      That seems like too much, but circular dependencies otherwise.
      Find a better way. The current placement of these defines might be wrong.
*/

static int
virDomainSpaprVIOFindByReg(virDomainDefPtr def ATTRIBUTE_UNUSED,
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


int
virDomainDeviceAddressAssignSpaprVIO(virDomainDefPtr def,
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

    ret = virDomainDeviceInfoIterate(def, virDomainSpaprVIOFindByReg, info);
    while (ret != 0) {
        if (user_reg) {
            virReportError(VIR_ERR_XML_ERROR,
                           _("spapr-vio address %#llx already in use"),
                           info->addr.spaprvio.reg);
            return -EEXIST;
        }

        /* We assigned the reg, so try a new value */
        info->addr.spaprvio.reg += 0x1000;
        ret = virDomainDeviceInfoIterate(def, virDomainSpaprVIOFindByReg,
                                         info);
    }

    return 0;
}


int
virDomainAssignSpaprVIOAddresses(virDomainDefPtr def)
{
    size_t i;
    int ret = -1;

    /* Default values match QEMU. See spapr_(llan|vscsi|vty).c */

    for (i = 0; i < def->nnets; i++) {
        if (def->nets[i]->model &&
            STREQ(def->nets[i]->model, "spapr-vlan"))
            def->nets[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO;
        if (virDomainDeviceAddressAssignSpaprVIO(def, &def->nets[i]->info,
                                            VIO_ADDR_NET) < 0)
            goto cleanup;
    }

    for (i = 0; i < def->ncontrollers; i++) {
        if (virDomainDeviceAddressAssignSpaprVIO(def, &def->controllers[i]->info,
                                            VIO_ADDR_SCSI) < 0)
            goto cleanup;
    }

    for (i = 0; i < def->nserials; i++) {
        if (def->serials[i]->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_SERIAL &&
            ARCH_IS_PPC64(def->os.arch) &&
            STRPREFIX(def->os.machine, "pseries"))
            def->serials[i]->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO;
        if (virDomainDeviceAddressAssignSpaprVIO(def, &def->serials[i]->info,
                                            VIO_ADDR_SERIAL) < 0)
            goto cleanup;
    }

    if (def->nvram) {
        if (ARCH_IS_PPC64(def->os.arch) &&
            STRPREFIX(def->os.machine, "pseries"))
            def->nvram->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_SPAPRVIO;
        if (virDomainDeviceAddressAssignSpaprVIO(def, &def->nvram->info,
                                            VIO_ADDR_NVRAM) < 0)
            goto cleanup;
    }

    /* No other devices are currently supported on spapr-vio */

    ret = 0;

 cleanup:
    return ret;
}


void
virDomainPrimeVirtioDeviceAddresses(virDomainDefPtr def,
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


/*
 * Three steps populating CCW devnos
 * 1. Allocate empty address set
 * 2. Gather addresses with explicit devno
 * 3. Assign defaults to the rest
 */
int
virDomainAssignS390Addresses(virDomainDefPtr def,
                              virDomainObjPtr obj,
                              bool virtio_ccw_capability,
                              bool virtio_s390_capability)
{
    int ret = -1;
    virDomainCCWAddressSetPtr addrs = NULL;

    if (virDomainMachineIsS390CCW(def) &&
        virtio_ccw_capability) {
        virDomainPrimeVirtioDeviceAddresses(
            def, VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCW);

        if (!(addrs = virDomainCCWAddressSetCreate()))
            goto cleanup;

        if (virDomainDeviceInfoIterate(def, virDomainCCWAddressValidate,
                                       addrs) < 0)
            goto cleanup;

        if (virDomainDeviceInfoIterate(def, virDomainCCWAddressAllocate,
                                       addrs) < 0)
            goto cleanup;
    } else if (virtio_s390_capability) {
        /* deal with legacy virtio-s390 */
        virDomainPrimeVirtioDeviceAddresses(
            def, VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_S390);
    }

    if (obj) {
        if (addrs) {
            /* if this is the live domain object, we persist the CCW addresses*/
            virDomainCCWAddressSetFree(def->ccwaddrs);
            def->ccwaddrs = addrs;
            addrs = NULL;
        }
    }
    ret = 0;

 cleanup:
    virDomainCCWAddressSetFree(addrs);

    return ret;
}


bool
virDomainMachineIsS390CCW(const virDomainDef *def)
{
    return STRPREFIX(def->os.machine, "s390-ccw");
}


bool
virDomainMachineIsVirt(const virDomainDef *def)
{
    return STREQ(def->os.machine, "virt") ||
           STRPREFIX(def->os.machine, "virt-");
}


void
virDomainAssignARMVirtioMMIOAddresses(virDomainDefPtr def,
                                       bool virtio_mmio_capability)
{
    if (def->os.arch != VIR_ARCH_ARMV7L &&
        def->os.arch != VIR_ARCH_AARCH64)
        return;

    if (!(STRPREFIX(def->os.machine, "vexpress-") ||
          virDomainMachineIsVirt(def)))
        return;

    if (virtio_mmio_capability) {
        virDomainPrimeVirtioDeviceAddresses(
            def, VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_MMIO);
    }
}


int
virDomainAssignVirtioSerialAddresses(virDomainDefPtr def,
                                      virDomainObjPtr obj)
{
    int ret = -1;
    size_t i;
    virDomainVirtioSerialAddrSetPtr addrs = NULL;

    if (!(addrs = virDomainVirtioSerialAddrSetCreate()))
        goto cleanup;

    if (virDomainVirtioSerialAddrSetAddControllers(addrs, def) < 0)
        goto cleanup;

    if (virDomainDeviceInfoIterate(def, virDomainVirtioSerialAddrReserve,
                                   addrs) < 0)
        goto cleanup;

    VIR_DEBUG("Finished reserving existing ports");

    for (i = 0; i < def->nconsoles; i++) {
        virDomainChrDefPtr chr = def->consoles[i];
        if (chr->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_CONSOLE &&
            chr->targetType == VIR_DOMAIN_CHR_CONSOLE_TARGET_TYPE_VIRTIO &&
            !virDomainVirtioSerialAddrIsComplete(&chr->info) &&
            virDomainVirtioSerialAddrAutoAssign(def, addrs, &chr->info, true) < 0)
            goto cleanup;
    }

    for (i = 0; i < def->nchannels; i++) {
        virDomainChrDefPtr chr = def->channels[i];
        if (chr->deviceType == VIR_DOMAIN_CHR_DEVICE_TYPE_CHANNEL &&
            chr->targetType == VIR_DOMAIN_CHR_CHANNEL_TARGET_TYPE_VIRTIO &&
            !virDomainVirtioSerialAddrIsComplete(&chr->info) &&
            virDomainVirtioSerialAddrAutoAssign(def, addrs, &chr->info, false) < 0)
            goto cleanup;
    }

    if (obj) {
        /* if this is the live domain object, we persist the addresses */
        virDomainVirtioSerialAddrSetFree(def->vioserialaddrs);
        def->vioserialaddrs = addrs;
        addrs = NULL;
    }
    ret = 0;

 cleanup:
    virDomainVirtioSerialAddrSetFree(addrs);
    return ret;
}


void
virDomainReleaseDeviceAddress(virDomainObjPtr vm,
                               virDomainDeviceInfoPtr info,
                               const char *devstr,
                               bool virtio_ccw_capability)
{
    if (!devstr)
        devstr = info->alias;

    if (info->type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_CCW &&
        virDomainMachineIsS390CCW(vm->def) &&
        virtio_ccw_capability &&
        virDomainCCWAddressReleaseAddr(vm->def->ccwaddrs, info) < 0)
        VIR_WARN("Unable to release CCW address on %s",
                 NULLSTR(devstr));
    else if (virDeviceInfoPCIAddressPresent(info) &&
             virDomainPCIAddressReleaseSlot(vm->def->pciaddrs,
                                            &info->addr.pci) < 0)
        VIR_WARN("Unable to release PCI address on %s",
                 NULLSTR(devstr));
    if (info->type == VIR_DOMAIN_DEVICE_ADDRESS_TYPE_VIRTIO_SERIAL &&
        virDomainVirtioSerialAddrRelease(vm->def->vioserialaddrs, info) < 0)
        VIR_WARN("Unable to release virtio-serial address on %s",
                 NULLSTR(devstr));
}

int
virDomainCollectPCIAddress(virDomainDefPtr def ATTRIBUTE_UNUSED,
                            virDomainDeviceDefPtr device,
                            virDomainDeviceInfoPtr info,
                            void *opaque)
{
    virDomainPCIAddressSetPtr addrs = opaque;
    int ret = -1;
    virPCIDeviceAddressPtr addr = &info->addr.pci;
    bool entireSlot;
    /* flags may be changed from default below */
    virDomainPCIConnectFlags flags = (VIR_PCI_CONNECT_HOTPLUGGABLE |
                                      VIR_PCI_CONNECT_TYPE_PCI_DEVICE);

    if (!virDeviceInfoPCIAddressPresent(info) ||
        ((device->type == VIR_DOMAIN_DEVICE_HOSTDEV) &&
         (device->data.hostdev->parent.type != VIR_DOMAIN_DEVICE_NONE))) {
        /* If a hostdev has a parent, its info will be a part of the
         * parent, and will have its address collected during the scan
         * of the parent's device type.
        */
        return 0;
    }

    /* Change flags according to differing requirements of different
     * devices.
     */
    switch (device->type) {
    case VIR_DOMAIN_DEVICE_CONTROLLER:
        switch (device->data.controller->type) {
        case  VIR_DOMAIN_CONTROLLER_TYPE_PCI:
           flags = virDomainPCIControllerModelToConnectType(device->data.controller->model);
            break;

        case VIR_DOMAIN_CONTROLLER_TYPE_SATA:
            /* SATA controllers aren't hot-plugged, and can be put in
             * either a PCI or PCIe slot
             */
            flags = (VIR_PCI_CONNECT_TYPE_PCI_DEVICE
                     | VIR_PCI_CONNECT_TYPE_PCIE_DEVICE);
            break;

        case VIR_DOMAIN_CONTROLLER_TYPE_USB:
            /* allow UHCI and EHCI controllers to be manually placed on
             * the PCIe bus (but don't put them there automatically)
             */
            switch (device->data.controller->model) {
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_EHCI:
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_EHCI1:
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_UHCI1:
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_UHCI2:
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_UHCI3:
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_VT82C686B_UHCI:
                flags = VIR_PCI_CONNECT_TYPE_PCI_DEVICE;
                break;
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_NEC_XHCI:
                /* should this be PCIE-only? Or do we need to allow PCI
                 * for backward compatibility?
                 */
                flags = (VIR_PCI_CONNECT_TYPE_PCI_DEVICE
                         | VIR_PCI_CONNECT_TYPE_PCIE_DEVICE);
                break;
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_PCI_OHCI:
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_PIIX3_UHCI:
            case VIR_DOMAIN_CONTROLLER_MODEL_USB_PIIX4_UHCI:
                /* Allow these for PCI only */
                break;
            }
        }
        break;

    case VIR_DOMAIN_DEVICE_SOUND:
        switch (device->data.sound->model) {
        case VIR_DOMAIN_SOUND_MODEL_ICH6:
        case VIR_DOMAIN_SOUND_MODEL_ICH9:
            flags = VIR_PCI_CONNECT_TYPE_PCI_DEVICE;
            break;
        }
        break;

    case VIR_DOMAIN_DEVICE_VIDEO:
        /* video cards aren't hot-plugged, and can be put in either a
         * PCI or PCIe slot
         */
       flags = (VIR_PCI_CONNECT_TYPE_PCI_DEVICE
                | VIR_PCI_CONNECT_TYPE_PCIE_DEVICE);
        break;
    }

    /* Ignore implicit controllers on slot 0:0:1.0:
     * implicit IDE controller on 0:0:1.1 (no qemu command line)
     * implicit USB controller on 0:0:1.2 (-usb)
     *
     * If the machine does have a PCI bus, they will get reserved
     * in qemuDomainAssignDevicePCISlots().
     */

    /* These are the IDE and USB controllers in the PIIX3, hardcoded
     * to bus 0 slot 1.  They cannot be attached to a PCIe slot, only
     * PCI.
     */
    if (device->type == VIR_DOMAIN_DEVICE_CONTROLLER && addr->domain == 0 &&
        addr->bus == 0 && addr->slot == 1) {
        virDomainControllerDefPtr cont = device->data.controller;

        if ((cont->type == VIR_DOMAIN_CONTROLLER_TYPE_IDE && cont->idx == 0 &&
             addr->function == 1) ||
            (cont->type == VIR_DOMAIN_CONTROLLER_TYPE_USB && cont->idx == 0 &&
             (cont->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_PIIX3_UHCI ||
              cont->model == -1) && addr->function == 2)) {
            /* Note the check for nbuses > 0 - if there are no PCI
             * buses, we skip this check. This is a quirk required for
             * some machinetypes such as s390, which pretend to have a
             * PCI bus for long enough to generate the "-usb" on the
             * commandline, but that don't really care if a PCI bus
             * actually exists. */
            if (addrs->nbuses > 0 &&
                !(addrs->buses[0].flags & VIR_PCI_CONNECT_TYPE_PCI_DEVICE)) {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("Bus 0 must be PCI for integrated PIIX3 "
                                 "USB or IDE controllers"));
                return -1;
            } else {
                return 0;
            }
        }
    }

    entireSlot = (addr->function == 0 &&
                  addr->multi != VIR_TRISTATE_SWITCH_ON);

    if (virDomainPCIAddressReserveAddr(addrs, addr, flags,
                                       entireSlot, true) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    return ret;
}


virDomainPCIAddressSetPtr
virDomainPCIAddressSetCreate(virDomainDefPtr def,
                              unsigned int nbuses,
                              bool dryRun)
{
    virDomainPCIAddressSetPtr addrs;
    size_t i;

    if ((addrs = virDomainPCIAddressSetAlloc(nbuses)) == NULL)
        return NULL;

    addrs->nbuses = nbuses;
    addrs->dryRun = dryRun;

    /* As a safety measure, set default model='pci-root' for first pci
     * controller and 'pci-bridge' for all subsequent. After setting
     * those defaults, then scan the config and set the actual model
     * for all addrs[idx]->bus that already have a corresponding
     * controller in the config.
     *
     */
    if (nbuses > 0)
        virDomainPCIAddressBusSetModel(&addrs->buses[0],
                                       VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT);
    for (i = 1; i < nbuses; i++) {
        virDomainPCIAddressBusSetModel(&addrs->buses[i],
                                       VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE);
    }

    for (i = 0; i < def->ncontrollers; i++) {
        size_t idx = def->controllers[i]->idx;

        if (def->controllers[i]->type != VIR_DOMAIN_CONTROLLER_TYPE_PCI)
            continue;

        if (idx >= addrs->nbuses) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Inappropriate new pci controller index %zu "
                             "not found in addrs"), idx);
            goto error;
        }

        if (virDomainPCIAddressBusSetModel(&addrs->buses[idx],
                                           def->controllers[i]->model) < 0)
            goto error;
        }

    if (virDomainDeviceInfoIterate(def, virDomainCollectPCIAddress, addrs) < 0)
        goto error;

    return addrs;

 error:
    virDomainPCIAddressSetFree(addrs);
    return NULL;
}

