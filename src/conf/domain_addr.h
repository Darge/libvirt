/*
 * domain_addr.h: helper APIs for managing domain device addresses
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

#ifndef __DOMAIN_ADDR_H__
# define __DOMAIN_ADDR_H__

# include "domain_conf.h"

# define VIR_PCI_ADDRESS_FUNCTION_LAST 7

/* a combination of all bits that describe the type of connections
 * allowed, e.g. PCI, PCIe, switch
 */
# define VIR_PCI_CONNECT_TYPES_MASK \
   (VIR_PCI_CONNECT_TYPE_PCI_DEVICE | VIR_PCI_CONNECT_TYPE_PCIE_DEVICE | \
    VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_UPSTREAM_PORT | \
    VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_DOWNSTREAM_PORT | \
    VIR_PCI_CONNECT_TYPE_PCIE_ROOT_PORT)

/* combination of all bits that could be used to connect a normal
 * endpoint device (i.e. excluding the connection possible between an
 * upstream and downstream switch port, or a PCIe root port and a PCIe
 * port)
 */
# define VIR_PCI_CONNECT_TYPES_ENDPOINT \
   (VIR_PCI_CONNECT_TYPE_PCI_DEVICE | VIR_PCI_CONNECT_TYPE_PCIE_DEVICE)

virDomainPCIConnectFlags
virDomainPCIControllerModelToConnectType(virDomainControllerModelPCI model);

char *virDomainPCIAddressAsString(virPCIDeviceAddressPtr addr)
      ATTRIBUTE_NONNULL(1);

virDomainPCIAddressSetPtr virDomainPCIAddressSetAlloc(unsigned int nbuses);

bool virDomainPCIAddressFlagsCompatible(virPCIDeviceAddressPtr addr,
                                        const char *addrStr,
                                        virDomainPCIConnectFlags busFlags,
                                        virDomainPCIConnectFlags devFlags,
                                        bool reportError,
                                        bool fromConfig)
     ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

bool virDomainPCIAddressValidate(virDomainPCIAddressSetPtr addrs,
                                 virPCIDeviceAddressPtr addr,
                                 const char *addrStr,
                                 virDomainPCIConnectFlags flags,
                                 bool fromConfig)
     ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);


int virDomainPCIAddressBusSetModel(virDomainPCIAddressBusPtr bus,
                                   virDomainControllerModelPCI model)
    ATTRIBUTE_NONNULL(1);

bool virDomainPCIAddressSlotInUse(virDomainPCIAddressSetPtr addrs,
                                  virPCIDeviceAddressPtr addr)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int virDomainPCIAddressSetGrow(virDomainPCIAddressSetPtr addrs,
                               virPCIDeviceAddressPtr addr,
                               virDomainPCIConnectFlags flags)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int virDomainPCIAddressReserveAddr(virDomainPCIAddressSetPtr addrs,
                                   virPCIDeviceAddressPtr addr,
                                   virDomainPCIConnectFlags flags,
                                   bool reserveEntireSlot,
                                   bool fromConfig)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int virDomainPCIAddressReserveSlot(virDomainPCIAddressSetPtr addrs,
                                   virPCIDeviceAddressPtr addr,
                                   virDomainPCIConnectFlags flags)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int virDomainPCIAddressEnsureAddr(virDomainPCIAddressSetPtr addrs,
                                  virDomainDeviceInfoPtr dev)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int virDomainPCIAddressReleaseAddr(virDomainPCIAddressSetPtr addrs,
                                   virPCIDeviceAddressPtr addr)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int virDomainPCIAddressReleaseSlot(virDomainPCIAddressSetPtr addrs,
                                   virPCIDeviceAddressPtr addr)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int virDomainPCIAddressGetNextSlot(virDomainPCIAddressSetPtr addrs,
                                   virPCIDeviceAddressPtr next_addr,
                                   virDomainPCIConnectFlags flags)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int virDomainPCIAddressReserveNextSlot(virDomainPCIAddressSetPtr addrs,
                                       virDomainDeviceInfoPtr dev,
                                       virDomainPCIConnectFlags flags)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int virDomainCCWAddressAssign(virDomainDeviceInfoPtr dev,
                              virDomainCCWAddressSetPtr addrs,
                              bool autoassign)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);
int virDomainCCWAddressAllocate(virDomainDefPtr def,
                                virDomainDeviceDefPtr dev,
                                virDomainDeviceInfoPtr info,
                                void *data)
    ATTRIBUTE_NONNULL(3) ATTRIBUTE_NONNULL(4);
int virDomainCCWAddressValidate(virDomainDefPtr def,
                                virDomainDeviceDefPtr dev,
                                virDomainDeviceInfoPtr info,
                                void *data)
    ATTRIBUTE_NONNULL(3) ATTRIBUTE_NONNULL(4);

int virDomainCCWAddressReleaseAddr(virDomainCCWAddressSetPtr addrs,
                                   virDomainDeviceInfoPtr dev)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);
virDomainCCWAddressSetPtr virDomainCCWAddressSetCreate(void);

virDomainVirtioSerialAddrSetPtr
virDomainVirtioSerialAddrSetCreate(void);
int
virDomainVirtioSerialAddrSetAddControllers(virDomainVirtioSerialAddrSetPtr addrs,
                                           virDomainDefPtr def)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);
bool
virDomainVirtioSerialAddrIsComplete(virDomainDeviceInfoPtr info);
int
virDomainVirtioSerialAddrAutoAssign(virDomainDefPtr def,
                                    virDomainVirtioSerialAddrSetPtr addrs,
                                    virDomainDeviceInfoPtr info,
                                    bool allowZero)
    ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);

int
virDomainVirtioSerialAddrAssign(virDomainDefPtr def,
                                virDomainVirtioSerialAddrSetPtr addrs,
                                virDomainDeviceInfoPtr info,
                                bool allowZero,
                                bool portOnly)
    ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);

int
virDomainVirtioSerialAddrReserve(virDomainDefPtr def,
                                 virDomainDeviceDefPtr dev,
                                 virDomainDeviceInfoPtr info,
                                 void *data)
    ATTRIBUTE_NONNULL(3) ATTRIBUTE_NONNULL(4);

int
virDomainVirtioSerialAddrRelease(virDomainVirtioSerialAddrSetPtr addrs,
                                 virDomainDeviceInfoPtr info)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int
virDomainDeviceAddressAssignSpaprVIO(virDomainDefPtr def,
                                virDomainDeviceInfoPtr info,
                                unsigned long long default_reg);

int
virDomainAssignSpaprVIOAddresses(virDomainDefPtr def);

void
virDomainPrimeVirtioDeviceAddresses(virDomainDefPtr def,
                                     virDomainDeviceAddressType type);

int
virDomainAssignS390Addresses(virDomainDefPtr def,
                              virDomainObjPtr obj,
                              bool virtio_ccw_capability,
                              bool virtio_s390_capability);

bool
virDomainMachineIsS390CCW(const virDomainDef *def);

bool
virDomainMachineIsVirt(const virDomainDef *def);

void
virDomainAssignARMVirtioMMIOAddresses(virDomainDefPtr def,
                                       bool virtio_mmio_capability);

int
virDomainAssignVirtioSerialAddresses(virDomainDefPtr def,
                                      virDomainObjPtr obj);

#endif /* __DOMAIN_ADDR_H__ */
