/***************************************************************************
 *   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRays.                                         *
 *                                                                         *
 *   LuxRays is free software; you can redistribute it and/or modify       *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   LuxRays is distributed in the hope that it will be useful,            *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 *                                                                         *
 *   LuxRays website: http://www.luxrender.net                             *
 ***************************************************************************/

#include "hitpoints.h"
#include "lookupaccel.h"
#include "bxdf.h"

using namespace lux;

//------------------------------------------------------------------------------
// EmbreeHitPointAccel
//
// Each hit point becomes a single RTC_GEOMETRY_TYPE_USER containing
// a position and a search radius.
//------------------------------------------------------------------------------

EmbreeHitPointAccel::EmbreeHitPointAccel(HitPoints *hps) : HitPointsLookUpAccel(hps) {
	device = rtcNewDevice(NULL);
	assert (device != NULL);

	scene = NULL;
	geometry = NULL;

	// Upper bound on the number of surface hit points.
	maxNNodes = hitPoints->GetSize();
	nodeData = new HitPoint *[maxNNodes];

	nNodes = 0;
	maxRadius = 0.f;
}

EmbreeHitPointAccel::~EmbreeHitPointAccel() {
	if (geometry)
		rtcReleaseGeometry(geometry);
	if (scene)
		rtcReleaseScene(scene);
	if (device)
		rtcReleaseDevice(device);

	delete[] nodeData;
}

void EmbreeHitPointAccel::BoundsFunction(const struct RTCBoundsFunctionArguments *args) {
	const EmbreeHitPointAccel *accel =
		static_cast<const EmbreeHitPointAccel *>(args->geometryUserPtr);
	const HitPoint *hp = accel->nodeData[args->primID];

	const Point p(hp->GetPosition());
	const float r = sqrtf(hp->accumPhotonRadius2);

	RTCBounds *bounds_o = args->bounds_o;
	bounds_o->lower_x = p.x - r;
	bounds_o->lower_y = p.y - r;
	bounds_o->lower_z = p.z - r;
	bounds_o->upper_x = p.x + r;
	bounds_o->upper_y = p.y + r;
	bounds_o->upper_z = p.z + r;
}

bool EmbreeHitPointAccel::PointQueryCallback(struct RTCPointQueryFunctionArguments *args) {
	QueryContext *qctx = static_cast<QueryContext *>(args->userPtr);
	EmbreeHitPointAccel *accel = qctx->accel;

	HitPoint *hp = accel->nodeData[args->primID];

	// The query radius is used to prune the BVH. AddFluxToHitPoint() does
	// the test against this hit point's accumPhotonRadius2 and
	// then deposits flux, like every other accelerator.
	accel->AddFluxToHitPoint(*qctx->sample, hp, *qctx->photon);

	// Hit points generally have different radii.
	return false;
}

void EmbreeHitPointAccel::Refresh(scheduling::Scheduler *scheduler) {
	// Hit point positions change so the whole structure
	// is rebuilt from scratch each time.
	if (geometry) {
		rtcReleaseGeometry(geometry);
		geometry = NULL;
	}
	if (scene) {
		rtcReleaseScene(scene);
		scene = NULL;
	}

	nNodes = 0;
	maxRadius = 0.f;
	for (u_int i = 0; i < maxNNodes; ++i) {
		HitPoint * const hp = hitPoints->GetHitPoint(i);
		if (hp->IsSurface()) {
			nodeData[nNodes++] = hp;
			maxRadius = max<float>(maxRadius, sqrtf(hp->accumPhotonRadius2));
		}
	}

	LOG(LUX_DEBUG, LUX_NOERROR) << "Building Embree point query structure with " << nNodes << " nodes";
	LOG(LUX_DEBUG, LUX_NOERROR) << "Embree hit points max. radius: " << maxRadius;

	scene = rtcNewScene(device);
	// This scene is discarded and rebuilt every pass, so a fast build is
	// likely more valuable than a fast query.
	rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_LOW);

	geometry = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_USER);
	rtcSetGeometryUserPrimitiveCount(geometry, nNodes);

    rtcSetGeometryUserData(geometry, (void *)this);

	rtcSetGeometryBoundsFunction(geometry,
		&EmbreeHitPointAccel::BoundsFunction, this);
    
    rtcSetGeometryPointQueryFunction(geometry,
        &EmbreeHitPointAccel::PointQueryCallback);

	rtcCommitGeometry(geometry);

	rtcAttachGeometry(scene, geometry);
	rtcCommitScene(scene);
}

void EmbreeHitPointAccel::AddFlux(Sample &sample, const PhotonData &photon) {
	// Refresh() hasn't run yet, or there are no surface hit points
	// this pass: nothing to deposit flux onto.
	if (nNodes == 0)
		return;

	RTCPointQueryContext context;
	rtcInitPointQueryContext(&context);

	RTCPointQuery query;
	query.x = photon.p.x;
	query.y = photon.p.y;
	query.z = photon.p.z;
	query.radius = maxRadius;
	query.time = 0.f;

	QueryContext qctx = { this, &sample, &photon };
	rtcPointQuery(scene, &query, &context,
		&EmbreeHitPointAccel::PointQueryCallback, (void *) &qctx);
}
