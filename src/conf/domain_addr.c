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
    - Deal with unused parameters warnings (connected to the "obj" problem)
    - rename virDomainAddressFindNewBusNr to virDomainPCIAddr*?
    - are functions qemu/vir DomainMachineIsQ35 and qemu/vir qemuDomainMachineIsI440FX
      qemu-only, or it's okay that they are moved to domain_addr.c?
      Is there any better place for them than domain_addr.c? domain_conf?
      These two functions are executed in many places...
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


bool
virDomainPCIBusFullyReserved(virDomainPCIAddressBusPtr bus)
{
    size_t i;

    for (i = bus->minSlot; i <= bus->maxSlot; i++)
        if (!bus->slots[i])
            return false;

    return true;
}


void
virDomainPCIControllerSetDefaultModelName(virDomainControllerDefPtr cont)
{
    int *modelName = &cont->opts.pciopts.modelName;

    /* make sure it's not already set */
    if (*modelName != VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_NONE)
        return;
    switch ((virDomainControllerModelPCI)cont->model) {
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_BRIDGE:
        *modelName = VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PCI_BRIDGE;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_DMI_TO_PCI_BRIDGE:
        *modelName = VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_I82801B11_BRIDGE;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT_PORT:
        *modelName = VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_IOH3420;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_UPSTREAM_PORT:
        *modelName = VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_X3130_UPSTREAM;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_SWITCH_DOWNSTREAM_PORT:
        *modelName = VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_XIO3130_DOWNSTREAM;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_EXPANDER_BUS:
        *modelName = VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PXB;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_EXPANDER_BUS:
        *modelName = VIR_DOMAIN_CONTROLLER_PCI_MODEL_NAME_PXB_PCIE;
        break;
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCIE_ROOT:
    case VIR_DOMAIN_CONTROLLER_MODEL_PCI_LAST:
        break;
    }
}



int
virDomainAddressFindNewBusNr(virDomainDefPtr def)
{
/* Try to find a nice default for busNr for a new pci-expander-bus.
 * This is a bit tricky, since you need to satisfy the following:
 *
 * 1) There need to be enough unused bus numbers between busNr of this
 *    bus and busNr of the next highest bus for the guest to assign a
 *    unique bus number to each PCI bus that is a child of this
 *    bus. Each PCI controller. On top of this, the pxb device (which
 *    implements the pci-extender-bus) includes a pci-bridge within
 *    it, and that bridge also uses one bus number (so each pxb device
 *    requires at least 2 bus numbers).
 *
 * 2) There need to be enough bus numbers *below* this for all the
 *    child controllers of the pci-expander-bus with the next lower
 *    busNr (or the pci-root bus if there are no lower
 *    pci-expander-buses).
 *
 * 3) If at all possible, we want to avoid needing to change the busNr
 *    of a bus in the future, as that changes the guest's device ABI,
 *    which could potentially lead to issues with a guest OS that is
 *    picky about such things.
 *
 *  Due to the impossibility of predicting what might be added to the
 *  config in the future, we can't make a foolproof choice, but since
 *  a pci-expander-bus (pxb) has slots for 32 devices, and the only
 *  practical use for it is to assign real devices on a particular
 *  NUMA node in the host, it's reasonably safe to assume it should
 *  never need any additional child buses (probably only a few of the
 *  32 will ever be used). So for pci-expander-bus we find the lowest
 *  existing busNr, and set this one to the current lowest - 2 (one
 *  for the pxb, one for the intergrated pci-bridge), thus leaving the
 *  maximum possible bus numbers available for other buses plugged
 *  into pci-root (i.e. pci-bridges and other
 *  pci-expander-buses). Anyone who needs more than 32 devices
 *  descended from one pci-expander-bus should set the busNr manually
 *  in the config.
 *
 *  There is room for more error checking here - in particular we
 *  can/should determine the ultimate parent (root-bus) of each PCI
 *  controller and determine if there is enough space for all the
 *  buses within the current range allotted to the bus just prior to
 *  this one.
 */
    size_t i;
    int lowestBusNr = 256;

    for (i = 0; i < def->ncontrollers; i++) {
        if (def->controllers[i]->type == VIR_DOMAIN_CONTROLLER_TYPE_PCI) {
            int thisBusNr = def->controllers[i]->opts.pciopts.busNr;

            if (thisBusNr >= 0 && thisBusNr < lowestBusNr)
                lowestBusNr = thisBusNr;
        }
    }

    /* If we already have a busNR = 1, then we can't auto-assign (0 is
     * the pci[e]-root, and the others may have been assigned
     * purposefully).
     */
    if (lowestBusNr <= 2)
        return -1;

    return lowestBusNr - 2;
}


int
virDomainValidateDevicePCISlotsPIIX3(virDomainDefPtr def,
                                      virDomainPCIAddressSetPtr addrs,
                                      bool qemuDeviceVideoUsable)
{
    int ret = -1;
    size_t i;
    virPCIDeviceAddress tmp_addr;
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


int
virDomainValidateDevicePCISlotsQ35(virDomainDefPtr def,
                                    virDomainPCIAddressSetPtr addrs,
                                    bool qemuDeviceVideoUsable)
{
    int ret = -1;
    size_t i;
    virPCIDeviceAddress tmp_addr;
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


int
virDomainValidateDevicePCISlotsChipsets(virDomainDefPtr def,
                                         virDomainPCIAddressSetPtr addrs,
                                         bool qemuDeviceVideoUsable)
{
    if (virDomainMachineIsI440FX(def) &&
        virDomainValidateDevicePCISlotsPIIX3(def, addrs, qemuDeviceVideoUsable) < 0) {
        return -1;
    }

    if (virDomainMachineIsQ35(def) &&
        virDomainValidateDevicePCISlotsQ35(def, addrs, qemuDeviceVideoUsable) < 0) {
        return -1;
    }

    return 0;
}


bool
virDomainMachineIsQ35(const virDomainDef *def)
{
    return (STRPREFIX(def->os.machine, "pc-q35") ||
            STREQ(def->os.machine, "q35"));
}


bool
virDomainMachineIsI440FX(const virDomainDef *def)
{
    return (STREQ(def->os.machine, "pc") ||
            STRPREFIX(def->os.machine, "pc-0.") ||
            STRPREFIX(def->os.machine, "pc-1.") ||
            STRPREFIX(def->os.machine, "pc-i440") ||
            STRPREFIX(def->os.machine, "rhel"));
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
int
virDomainAssignDevicePCISlots(virDomainDefPtr def,
                               virDomainPCIAddressSetPtr addrs,
                               bool virtio_mmio_capability)
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
            virtio_mmio_capability)
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


bool
virDomainSupportsPCI(virDomainDefPtr def,
                      bool object_gpex_capability)
{
    if ((def->os.arch != VIR_ARCH_ARMV7L) && (def->os.arch != VIR_ARCH_AARCH64))
        return true;

    if (STREQ(def->os.machine, "versatilepb"))
        return true;

    if (virDomainMachineIsVirt(def) &&
        object_gpex_capability)
        return true;

    return false;
}
