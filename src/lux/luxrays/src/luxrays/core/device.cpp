/***************************************************************************
 * Copyright 1998-2015 by authors (see AUTHORS.txt)                        *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 * Licensed under the Apache License, Version 2.0 (the "License");         *
 * you may not use this file except in compliance with the License.        *
 * You may obtain a copy of the License at                                 *
 *                                                                         *
 *     http://www.apache.org/licenses/LICENSE-2.0                          *
 *                                                                         *
 * Unless required by applicable law or agreed to in writing, software     *
 * distributed under the License is distributed on an "AS IS" BASIS,       *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
 * See the License for the specific language governing permissions and     *
 * limitations under the License.                                          *
 ***************************************************************************/

#include "luxrays/core/intersectiondevice.h"
#include "luxrays/core/context.h"

namespace luxrays {

//------------------------------------------------------------------------------
// DeviceDescription
//------------------------------------------------------------------------------

void DeviceDescription::FilterOne(std::vector<DeviceDescription *> &deviceDescriptions)
{
	// Pure-CPU only: keep the first native thread device, drop the rest.
	int cpuIndex = -1;
	for (size_t i = 0; i < deviceDescriptions.size(); ++i) {
		if ((cpuIndex == -1) && (deviceDescriptions[i]->GetType() &
			DEVICE_TYPE_NATIVE_THREAD)) {
			cpuIndex = (int)i;
			break;
		}
	}

	if (cpuIndex != -1) {
		std::vector<DeviceDescription *> selectedDev;
		selectedDev.push_back(deviceDescriptions[cpuIndex]);
		deviceDescriptions = selectedDev;
	} else
		deviceDescriptions.clear();
}

void DeviceDescription::Filter(const DeviceType type,
	std::vector<DeviceDescription *> &deviceDescriptions)
{
	if (type == DEVICE_TYPE_ALL)
		return;
	size_t i = 0;
	while (i < deviceDescriptions.size()) {
		if ((deviceDescriptions[i]->GetType() & type) == 0) {
			// Remove the device from the list
			deviceDescriptions.erase(deviceDescriptions.begin() + i);
		} else
			++i;
	}
}

std::string DeviceDescription::GetDeviceType(const DeviceType type)
{
	switch (type) {
		case DEVICE_TYPE_ALL:
			return "ALL";
		case DEVICE_TYPE_NATIVE_THREAD:
			return "NATIVE_THREAD";
		case DEVICE_TYPE_VIRTUAL:
			return "VIRTUAL";
		default:
			return "UNKNOWN";
	}
}

//------------------------------------------------------------------------------
// Device
//------------------------------------------------------------------------------

Device::Device(const Context *context, const DeviceType type, const size_t index) :
	deviceContext(context), deviceType(type) {
	deviceIndex = index;
	started = false;
	usedMemory = 0;
}

Device::~Device() {
}

void Device::Start() {
	assert (!started);
	started = true;
}

void Device::Stop() {
	assert (started);
	started = false;
}

//------------------------------------------------------------------------------
// Native Device Description
//------------------------------------------------------------------------------

void NativeThreadDeviceDescription::AddDeviceDescs(std::vector<DeviceDescription *> &descriptions) {
	descriptions.push_back(new NativeThreadDeviceDescription("NativeThread"));
}

//------------------------------------------------------------------------------
// IntersectionDevice
//------------------------------------------------------------------------------

IntersectionDevice::IntersectionDevice(const Context *context,
	const DeviceType type, const size_t index) :
	Device(context, type, index), dataSet(NULL), queueCount(1), bufferCount(3),
	stackSize(24), dataParallelSupport(true) {
}

IntersectionDevice::~IntersectionDevice() {
}

void IntersectionDevice::SetDataSet(DataSet *newDataSet) {
	assert (!started);

	dataSet = newDataSet;
}

void IntersectionDevice::Start() {
	assert (dataSet != NULL);

	Device::Start();

	statsStartTime = WallClockTime();
	statsTotalSerialRayCount = 0.0;
	statsTotalDataParallelRayCount = 0.0;
	statsDeviceIdleTime = 0.0;
	statsDeviceTotalTime = 0.0;
}

void IntersectionDevice::SetQueueCount(const u_int count) {
	assert (!started);
	assert (count >= 1);

	queueCount = count;
}

}
