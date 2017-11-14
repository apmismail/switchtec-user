/*
 * Microsemi Switchtec(tm) PCIe Management Library
 * Copyright (c) 2017, Microsemi Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "switchtec/switchtec.h"
#include "switchtec/portable.h"
#include "switchtec/gas.h"
#include "switchtec/utils.h"
#include "../switchtec_priv.h"

#ifdef __WINDOWS__
#include "windows/switchtec_public.h"

#include <setupapi.h>

#include <errno.h>
#include <stdio.h>

struct switchtec_windows {
	struct switchtec_dev dev;
	HANDLE hdl;
};

#define to_switchtec_windows(d)  \
	((struct switchtec_windows *) \
	 ((char *)d - offsetof(struct switchtec_windows, dev)))

void platform_perror(const char *msg)
{
	char errmsg[500] = "";
	int err = GetLastError();

	FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM |
		       FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err,
		       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		       errmsg, sizeof (errmsg), NULL);

	if (!strlen(errmsg))
		sprintf(errmsg, "Error %d", err);

	fprintf(stderr, "%s: %s", msg, errmsg);
}

static int count_devices(void)
{
	HDEVINFO devinfo;
	DWORD count = 0;
	SP_DEVICE_INTERFACE_DATA deviface;

	devinfo = SetupDiGetClassDevs(&SWITCHTEC_INTERFACE_GUID,
				      NULL, NULL, DIGCF_DEVICEINTERFACE |
				      DIGCF_PRESENT);

	deviface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	while (SetupDiEnumDeviceInterfaces(devinfo, NULL,
					   &SWITCHTEC_INTERFACE_GUID,
					   count++, &deviface));

	return count - 1;
}

static BOOL get_path(HDEVINFO devinfo, SP_DEVICE_INTERFACE_DATA *deviface,
		     SP_DEVINFO_DATA *devdata, char *path, size_t path_size)
{
	DWORD size;
	SP_DEVICE_INTERFACE_DETAIL_DATA *devdetail;
	BOOL status = TRUE;
	char *hash;

	devdata->cbSize = sizeof(SP_DEVINFO_DATA);

	SetupDiGetDeviceInterfaceDetail(devinfo, deviface, NULL, 0, &size,
					NULL);

	devdetail = malloc(size);
	if (!devdetail) {
		perror("Enumeration");
		return FALSE;
	}

	devdetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

	status = SetupDiGetDeviceInterfaceDetail(devinfo, deviface, devdetail,
						 size, NULL, devdata);
	if (!status) {
		platform_perror("SetupDiGetDeviceInterfaceDetail");
		goto out;
	}

	strcpy_s(path, path_size, devdetail->DevicePath);

	/* Chop off the GUID */
	hash = strrchr(path, '#');
	if (hash)
		*hash = 0;

out:
	free(devdetail);
	return status;
}

static BOOL get_pci_address(HDEVINFO devinfo, SP_DEVINFO_DATA *devdata,
			    int *bus, int *dev, int *func)
{
	BOOL status;
	int ret;
	char loc[256];

	status = SetupDiGetDeviceRegistryProperty(devinfo, devdata,
			SPDRP_LOCATION_INFORMATION, NULL,
			(BYTE *)loc, sizeof(loc), NULL);
	if (!status) {
		platform_perror("SetupDiGetDeviceRegistryProperty (LOC)");
		return FALSE;
	}

	ret = sscanf(loc, "PCI bus %d, device %d, function %d", bus, dev, func);
	if (ret != 3) {
		fprintf(stderr, "Error parsing PCI BUS: '%s'\n", loc);
		return FALSE;
	}

	return TRUE;
}

static void get_pci_address_str(HDEVINFO devinfo, SP_DEVINFO_DATA *devdata,
				char *res, size_t res_size)
{
	BOOL status;
	int bus, dev, func;

	status = get_pci_address(devinfo, devdata, &bus, &dev, &func);
	if (!status)
		snprintf(res, res_size, "??:??.?");
	else
		snprintf(res, res_size, "%02x:%02x.%x", bus, dev, func);
}

static void get_description(HDEVINFO devinfo, SP_DEVINFO_DATA *devdata,
			    char *res, size_t res_size)
{
	SetupDiGetDeviceRegistryProperty(devinfo, devdata,
			SPDRP_DEVICEDESC, NULL,(BYTE *)res, res_size, NULL);
}

/*
 * Sigh... Mingw doesn't define this API yet in it's header and the library
 * only has the WCHAR version.
 */
WINSETUPAPI WINBOOL WINAPI SetupDiGetDevicePropertyW(HDEVINFO DeviceInfoSet,
	PSP_DEVINFO_DATA DeviceInfoData, const DEVPROPKEY *PropertyKey,
	DEVPROPTYPE *PropertyType, PBYTE PropertyBuffer,
	DWORD PropertyBufferSize, PDWORD RequiredSize,
	DWORD Flags);

static void get_property(HDEVINFO devinfo, SP_DEVINFO_DATA *devdata,
			 const DEVPROPKEY *propkey, char *res, size_t res_size)
{
	DEVPROPTYPE ptype;
	WCHAR buf[res_size];

	SetupDiGetDevicePropertyW(devinfo, devdata, propkey, &ptype,
				 (PBYTE)buf, sizeof(buf), NULL, 0);
	wcstombs(res, buf, res_size);
}

static void get_fw_property(HDEVINFO devinfo, SP_DEVINFO_DATA *devdata,
			    char *res, size_t res_size)
{
	char buf[16];
	long fw_ver;

	get_property(devinfo, devdata, &SWITCHTEC_PROP_FW_VERSION,
		     buf, sizeof(buf));

	fw_ver = strtol(buf, NULL, 16);

	if (fw_ver < 0)
		snprintf(res, res_size, "unknown");
	else
		version_to_string(fw_ver, res, res_size);
}

static void append_guid(const char *path, char *path_with_guid, size_t bufsize,
			const GUID *guid)
{
	snprintf(path_with_guid, bufsize,
		 "%s#{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
		 path, guid->Data1, guid->Data2, guid->Data3,
		 guid->Data4[0], guid->Data4[1], guid->Data4[2],
		 guid->Data4[3], guid->Data4[4], guid->Data4[5],
		 guid->Data4[6], guid->Data4[7]);
}

#ifdef __CHECKER__
#define __force __attribute__((force))
#else
#define __force
#endif

static BOOL map_gas(struct switchtec_windows *wdev)
{
	BOOL status;
	struct switchtec_gas_map map;

	status = DeviceIoControl(wdev->hdl, IOCTL_SWITCHTEC_GAS_MAP, NULL, 0,
				 &map, sizeof(map), NULL, NULL);
	if (!status)
		return status;

	wdev->dev.gas_map = (gasptr_t __force)map.gas;
	wdev->dev.gas_map_size = map.length;
	return TRUE;
}

static void unmap_gas(struct switchtec_windows *wdev)
{
	struct switchtec_gas_map map = {
		.gas = (void * __force)wdev->dev.gas_map,
		.length = wdev->dev.gas_map_size,
	};

	DeviceIoControl(wdev->hdl, IOCTL_SWITCHTEC_GAS_UNMAP, &map, sizeof(map),
			NULL, 0, NULL, NULL);
}

static void set_partition_info(struct switchtec_dev *dev)
{
	dev->partition = gas_reg_read8(dev, top.partition_id);
	dev->partition_count = gas_reg_read8(dev, top.partition_count);
}

struct switchtec_dev *switchtec_open_by_path(const char *path)
{
	struct switchtec_windows *wdev;
	char path_with_guid[MAX_PATH];

	wdev = malloc(sizeof(*wdev));
	if (!wdev)
		return NULL;

	append_guid(path, path_with_guid, sizeof(path_with_guid),
		    &SWITCHTEC_INTERFACE_GUID);

	wdev->hdl = CreateFile(path_with_guid, GENERIC_READ | GENERIC_WRITE,
			       FILE_SHARE_READ | FILE_SHARE_WRITE,
			       NULL, OPEN_EXISTING, 0, NULL);

	if (wdev->hdl == INVALID_HANDLE_VALUE)
		goto err_free;

	if (!map_gas(wdev))
		goto err_close;

	set_partition_info(&wdev->dev);

	return &wdev->dev;

err_close:
	CloseHandle(wdev->hdl);
err_free:
	free(wdev);
	return NULL;
}

struct switchtec_dev *switchtec_open_by_index(int index)
{
	HDEVINFO devinfo;
	SP_DEVICE_INTERFACE_DATA deviface;
	SP_DEVINFO_DATA devdata;
	char path[MAX_PATH];
	struct switchtec_dev *dev = NULL;
	BOOL status;

	devinfo = SetupDiGetClassDevs(&SWITCHTEC_INTERFACE_GUID,
				      NULL, NULL, DIGCF_DEVICEINTERFACE |
				      DIGCF_PRESENT);
	if (devinfo == INVALID_HANDLE_VALUE)
		return NULL;

	deviface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	status = SetupDiEnumDeviceInterfaces(devinfo, NULL,
					     &SWITCHTEC_INTERFACE_GUID,
					     index, &deviface);
	if (!status) {
		errno = ENODEV;
		goto out;
	}

	status = get_path(devinfo, &deviface,  &devdata,
			  path, sizeof(path));
	if (!status)
		goto out;

	dev = switchtec_open_by_path(path);

out:
	SetupDiDestroyDeviceInfoList(devinfo);
	return dev;
}

struct switchtec_dev *switchtec_open_by_pci_addr(int domain, int bus,
						 int device, int func)
{
	HDEVINFO devinfo;
	SP_DEVICE_INTERFACE_DATA deviface;
	SP_DEVINFO_DATA devdata;
	char path[MAX_PATH];
	struct switchtec_dev *dev = NULL;
	BOOL status;
	int dbus, ddevice, dfunc;
	int idx = 0;

	devinfo = SetupDiGetClassDevs(&SWITCHTEC_INTERFACE_GUID,
				      NULL, NULL, DIGCF_DEVICEINTERFACE |
				      DIGCF_PRESENT);
	if (devinfo == INVALID_HANDLE_VALUE)
		return NULL;

	deviface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	while (SetupDiEnumDeviceInterfaces(devinfo, NULL,
					   &SWITCHTEC_INTERFACE_GUID,
					   idx++, &deviface))
	{
		status = get_path(devinfo, &deviface,  &devdata,
				  path, sizeof(path));
		if (!status)
			continue;

		get_pci_address(devinfo, &devdata, &dbus, &ddevice, &dfunc);
		if (dbus == bus && ddevice == device && dfunc == func) {
			dev = switchtec_open_by_path(path);
			break;
		}
	}

	if (!dev)
		errno = ENODEV;

	SetupDiDestroyDeviceInfoList(devinfo);
	return dev;
}

void switchtec_close(struct switchtec_dev *dev)
{
	struct switchtec_windows *wdev = to_switchtec_windows(dev);

	if (!dev)
		return;

	unmap_gas(wdev);
	CloseHandle(wdev->hdl);
}

int switchtec_list(struct switchtec_device_info **devlist)
{
	HDEVINFO devinfo;
	SP_DEVICE_INTERFACE_DATA deviface;
	SP_DEVINFO_DATA devdata;
	struct switchtec_device_info *dl;

	BOOL status;
	DWORD idx = 0;
	DWORD cnt = 0;

	dl = *devlist = calloc(count_devices(),
			  sizeof(struct switchtec_device_info));
	if (!dl) {
		errno = ENOMEM;
		return -errno;
	}

	devinfo = SetupDiGetClassDevs(&SWITCHTEC_INTERFACE_GUID,
				      NULL, NULL, DIGCF_DEVICEINTERFACE |
				      DIGCF_PRESENT);
	if (devinfo == INVALID_HANDLE_VALUE)
		return 0;

	deviface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	while (SetupDiEnumDeviceInterfaces(devinfo, NULL,
					   &SWITCHTEC_INTERFACE_GUID,
					   idx, &deviface))
	{
		snprintf(dl[cnt].name, sizeof(dl[cnt].name),
			 "switchtec%ld", idx++);

		status = get_path(devinfo, &deviface,  &devdata,
				  dl[cnt].path, sizeof(dl[cnt].path));
		if (!status)
			continue;

		get_pci_address_str(devinfo, &devdata, dl[cnt].pci_dev,
				    sizeof(dl[cnt].pci_dev));
		get_description(devinfo, &devdata, dl[cnt].desc,
				sizeof(dl[cnt].desc));

		get_property(devinfo, &devdata, &SWITCHTEC_PROP_PRODUCT_ID,
			     dl[cnt].product_id, sizeof(dl[cnt].product_id));
		get_property(devinfo, &devdata, &SWITCHTEC_PROP_PRODUCT_REV,
			     dl[cnt].product_rev, sizeof(dl[cnt].product_rev));
		get_fw_property(devinfo, &devdata, dl[cnt].fw_version,
				sizeof(dl[cnt].fw_version));
		cnt++;
	}

	SetupDiDestroyDeviceInfoList(devinfo);

	return cnt;
}

int switchtec_get_fw_version(struct switchtec_dev *dev, char *buf,
			     size_t buflen)
{
	long long ver;

	ver = gas_reg_read32(dev, sys_info.firmware_version);
	version_to_string(ver, buf, buflen);

	return 0;
}

int switchtec_cmd(struct switchtec_dev *dev, uint32_t cmd,
		  const void *payload, size_t payload_len, void *resp,
		  size_t resp_len)
{
	struct switchtec_windows *wdev = to_switchtec_windows(dev);
	BOOL status;
	int ret;

	struct switchtec_mrpc_cmd *mcmd;
	struct switchtec_mrpc_result *mres;
	size_t mcmd_len, mres_len;

	mcmd_len = offsetof(struct switchtec_mrpc_cmd, data) + payload_len;
	mres_len = offsetof(struct switchtec_mrpc_result, data) + resp_len;

	mcmd = calloc(1, mcmd_len);
	if (!mcmd)
		return -errno;

	mres = calloc(1, mres_len);
	if (!mres) {
		free(mcmd);
		return -errno;
	}

	mcmd->cmd = cmd;
	memcpy(mcmd->data, payload, payload_len);

	status = DeviceIoControl(wdev->hdl, IOCTL_SWITCHTEC_MRPC,
				 mcmd, (DWORD)mcmd_len,
				 mres, (DWORD)mres_len,
				 NULL, NULL);
	if (!status) {
		ret = -EIO;
		goto free_and_exit;
	}

	memcpy(resp, mres->data, resp_len);
	ret = mres->status;

free_and_exit:
	free(mres);
	free(mcmd);
	return ret;
}

int switchtec_get_devices(struct switchtec_dev *dev,
			  struct switchtec_status *status,
			  int ports)
{
	/* I don't see any easy way of implementing this on windows */
	return 0;
}

int switchtec_pff_to_port(struct switchtec_dev *dev, int pff,
			  int *partition, int *port)
{
	int i, part;
	uint32_t reg;
	struct part_cfg_regs *pcfg;

	*port = -1;

	for (part = 0; part < dev->partition_count; part++) {
		pcfg = &dev->gas_map->part_cfg[part];
		*partition = part;

		reg = gas_read32(&pcfg->usp_pff_inst_id);
		if (reg == pff) {
			*port = 0;
			return 0;
		}

		reg = gas_read32(&pcfg->vep_pff_inst_id);
		if (reg == pff) {
			*port = SWITCHTEC_PFF_PORT_VEP;
			return 0;
		}

		for (i = 0; i < ARRAY_SIZE(pcfg->dsp_pff_inst_id); i++) {
			reg = gas_read32(&pcfg->dsp_pff_inst_id[i]);
			if (reg != pff)
				continue;

			*port = i + 1;
			break;
		}

		if (*port != -1)
			return 0;
	}

	errno = EINVAL;
	return -EINVAL;
}

int switchtec_port_to_pff(struct switchtec_dev *dev, int partition,
			  int port, int *pff)
{
	errno = ENOSYS;
	return -errno;
}

static void set_fw_info_part(struct switchtec_fw_image_info *info,
			     struct partition_info __gas *pi)
{
	info->image_addr = gas_read32(&pi->address);
	info->image_len = gas_read32(&pi->length);
}

int switchtec_flash_part(struct switchtec_dev *dev,
			 struct switchtec_fw_image_info *info,
			 enum switchtec_fw_image_type part)
{
	struct flash_info_regs __gas *fi = &dev->gas_map->flash_info;
	struct sys_info_regs __gas *si = &dev->gas_map->sys_info;
	uint32_t active_addr = -1;

	memset(info, 0, sizeof(*info));

	switch (part) {
	case SWITCHTEC_FW_TYPE_IMG0:
		active_addr = gas_read32(&fi->active_img.address);
		set_fw_info_part(info, &fi->img0);

		if (gas_read16(&si->img_running) == SWITCHTEC_IMG0_RUNNING)
			info->active |= SWITCHTEC_FW_PART_RUNNING;
		break;

	case SWITCHTEC_FW_TYPE_IMG1:
		active_addr = gas_read32(&fi->active_img.address);
		set_fw_info_part(info, &fi->img1);

		if (gas_read16(&si->img_running) == SWITCHTEC_IMG1_RUNNING)
			info->active |= SWITCHTEC_FW_PART_RUNNING;
		break;

	case SWITCHTEC_FW_TYPE_DAT0:
		active_addr = gas_read32(&fi->active_cfg.address);
		set_fw_info_part(info, &fi->cfg0);

		if (gas_read16(&si->cfg_running) == SWITCHTEC_CFG0_RUNNING)
			info->active |= SWITCHTEC_FW_PART_RUNNING;
		break;


	case SWITCHTEC_FW_TYPE_DAT1:
		active_addr = gas_read32(&fi->active_cfg.address);
		set_fw_info_part(info, &fi->cfg0);

		if (gas_read16(&si->cfg_running) == SWITCHTEC_CFG1_RUNNING)
			info->active |= SWITCHTEC_FW_PART_RUNNING;
		break;

	case SWITCHTEC_FW_TYPE_NVLOG:
		set_fw_info_part(info, &fi->nvlog);
		break;

	default:
		return -EINVAL;
	}

	if (info->image_addr == active_addr)
		info->active |= SWITCHTEC_FW_PART_ACTIVE;

	return 0;
}

int switchtec_event_summary(struct switchtec_dev *dev,
			    struct switchtec_event_summary *sum)
{
	errno = ENOSYS;
	return -errno;
}

int switchtec_event_check(struct switchtec_dev *dev,
			  struct switchtec_event_summary *check,
			  struct switchtec_event_summary *res)
{
	errno = ENOSYS;
	return -errno;
}

int switchtec_event_ctl(struct switchtec_dev *dev,
			enum switchtec_event_id e,
			int index, int flags,
			uint32_t data[5])
{
	errno = ENOSYS;
	return -errno;
}

int switchtec_event_wait(struct switchtec_dev *dev, int timeout_ms)
{
	errno = ENOSYS;
	return -errno;
}

gasptr_t switchtec_gas_map(struct switchtec_dev *dev, int writeable,
                           size_t *map_size)
{
	if (map_size)
		*map_size = dev->gas_map_size;

	return dev->gas_map;
}

void switchtec_gas_unmap(struct switchtec_dev *dev, gasptr_t map)
{
}

#endif
