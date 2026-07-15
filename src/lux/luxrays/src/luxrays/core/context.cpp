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

#include <cstdlib>
#include <cassert>
#include <iosfwd>
#include <sstream>
#include <stdexcept>

#include "luxrays/core/context.h"
#include "luxrays/core/intersectiondevice.h"
#include "luxrays/core/virtualdevice.h"

using namespace luxrays;

Context::Context(LuxRaysDebugHandler handler, const Properties &config) : cfg(config) {
	debugHandler = handler;
	currentDataSet = NULL;
	started = false;
	verbose = cfg.Get(Property("context.verbose")(true)).Get<bool>();

	// Get the list of devices available on the platform
	NativeThreadDeviceDescription::AddDeviceDescs(deviceDescriptions);

	// Print device info
	for (size_t i = 0; i < deviceDescriptions.size(); ++i) {
		DeviceDescription *desc = deviceDescriptions[i];
		LR_LOG(this, "Device " << i << " name: " <<
			desc->GetName());

		LR_LOG(this, "Device " << i << " type: " <<
			DeviceDescription::GetDeviceType(desc->GetType()));

		LR_LOG(this, "Device " << i << " compute units: " <<
			desc->GetComputeUnits());

		LR_LOG(this, "Device " << i << " preferred float vector width: " <<
			desc->GetNativeVectorWidthFloat());

		LR_LOG(this, "Device " << i << " max allocable memory: " <<
			desc->GetMaxMemory() / (1024 * 1024) << "MBytes");

		LR_LOG(this, "Device " << i << " max allocable memory block size: " <<
			desc->GetMaxMemoryAllocSize() / (1024 * 1024) << "MBytes");
	}
}

Context::~Context() {
	if (started)
		Stop();

	for (size_t i = 0; i < idevices.size(); ++i)
		delete idevices[i];
	for (size_t i = 0; i < deviceDescriptions.size(); ++i)
		delete deviceDescriptions[i];
}

void Context::SetDataSet(DataSet *dataSet) {
	assert (!started);

	currentDataSet = dataSet;

	for (size_t i = 0; i < idevices.size(); ++i)
		idevices[i]->SetDataSet(currentDataSet);
}

void Context::UpdateDataSet() {
	assert (started);

	// Update the data set
	currentDataSet->UpdateAccelerators();
}

void Context::Start() {
	assert (!started);

	for (size_t i = 0; i < idevices.size(); ++i)
		idevices[i]->Start();

	started = true;
}

void Context::Interrupt() {
	assert (started);

	for (size_t i = 0; i < idevices.size(); ++i)
		idevices[i]->Interrupt();
}

void Context::Stop() {
	assert (started);

	Interrupt();

	for (size_t i = 0; i < idevices.size(); ++i)
		idevices[i]->Stop();

	started = false;
}

const std::vector<DeviceDescription *> &Context::GetAvailableDeviceDescriptions() const {
	return deviceDescriptions;
}

const std::vector<IntersectionDevice *> &Context::GetIntersectionDevices() const {
	return idevices;
}

std::vector<IntersectionDevice *> Context::CreateIntersectionDevices(
	std::vector<DeviceDescription *> &deviceDesc, const size_t indexOffset) {
	assert (!started);

	LR_LOG(this, "Creating " << deviceDesc.size() << " intersection device(s)");

	std::vector<IntersectionDevice *> newDevices;
	for (size_t i = 0; i < deviceDesc.size(); ++i) {
		LR_LOG(this, "Allocating intersection device " << i << ": " << deviceDesc[i]->GetName() <<
				" (Type = " << DeviceDescription::GetDeviceType(deviceDesc[i]->GetType()) << ")");

		IntersectionDevice *device;
		if (deviceDesc[i]->GetType() == DEVICE_TYPE_NATIVE_THREAD) {
			// Nathive thread devices
			device = new NativeThreadIntersectionDevice(this, indexOffset + i);
		}
		else
			assert (false);

		newDevices.push_back(device);
	}

	return newDevices;
}

std::vector<IntersectionDevice *> Context::AddIntersectionDevices(std::vector<DeviceDescription *> &deviceDesc) {
	assert (!started);

	std::vector<IntersectionDevice *> newDevices = CreateIntersectionDevices(deviceDesc, idevices.size());
	for (size_t i = 0; i < newDevices.size(); ++i)
		idevices.push_back(newDevices[i]);

	return newDevices;
}

std::vector<IntersectionDevice *> Context::AddVirtualIntersectionDevice(
	std::vector<DeviceDescription *> &deviceDescs) {
	assert (!started);
	assert (deviceDescs.size() > 0);

	std::vector<IntersectionDevice *> realDevices = CreateIntersectionDevices(deviceDescs, idevices.size());
	VirtualIntersectionDevice *virtualDevice = new VirtualIntersectionDevice(realDevices, idevices.size());
	idevices.push_back(virtualDevice);

	return realDevices;
}
