/***************************************************************************
 *   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   Lux Renderer is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   Lux Renderer is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 *                                                                         *
 *   This project is based on PBRT ; see http://www.pbrt.org               *
 *   Lux Renderer website : http://www.luxrender.org                       *
 ***************************************************************************/
#include <map>

#include "error.h"
#include "embree.h"
#include "dynload.h"
#include "shapes/mesh.h"

using namespace luxrays;
using namespace lux;

void lux::errorFunction(void* userPtr, enum RTCError error, const char* str)
{
	printf("error %d: %s\n", error, str);
}

void embree_accel::CollectTriangleInfos(const boost::shared_ptr<Primitive> &prim,
	const AreaLight *areaLight, const MotionPrimitive *motion,
	vector<EmbreeTriangleInfo> &out, u_int &skippedCount)
{
	const Primitive *raw = prim.get();

	if (const MeshBaryTriangle *tri = dynamic_cast<const MeshBaryTriangle *>(raw)) {
		// Retain this leaf's shared_ptr. If it was produced by the
		// Refine() call below, nothing else may be holding a reference,
		// and we need the pointer to stay valid.
		primitives.push_back(prim);

		EmbreeTriangleInfo info;
		info.triangle = tri;
		info.areaLight = areaLight;
		info.motion = motion;
		out.push_back(info);
		return;
	}

	if (const AreaLightPrimitive *alp = dynamic_cast<const AreaLightPrimitive *>(raw)) {
		// A primitive shouldn't be wrapped by more than one
		// AreaLightPrimitive; if it is, keep the outermost.
		const AreaLight *al = areaLight ? areaLight : alp->GetAreaLight().get();
		CollectTriangleInfos(alp->GetPrimitive(), al, motion, out, skippedCount);
		return;
	}

	if (const MotionPrimitive *mp = dynamic_cast<const MotionPrimitive *>(raw)) {
		if (motion) {
			// Nested MotionPrimitives aren't supported.
			LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: skipping "
				<< "nested MotionPrimitive (typeid: "
				<< typeid(*raw).name() << ")";
			++skippedCount;
			return;
		}
		CollectTriangleInfos(mp->GetInstance(), areaLight, mp, out, skippedCount);
		return;
	}

	if (const Aggregate *agg = dynamic_cast<const Aggregate *>(raw)) {
		// Aggregates report CanIntersect() == true unconditionally;
		// expand into its constituent primitives.
		vector<boost::shared_ptr<Primitive>> children;
		agg->GetPrimitives(children);
		for (size_t i = 0; i < children.size(); ++i)
			CollectTriangleInfos(children[i], areaLight, motion, out, skippedCount);
		return;
	}

	if (!prim->CanIntersect()) {
		// Not yet in an intersectable form! Refine it and recurse
		// into whatever comes out...
		vector<boost::shared_ptr<Primitive>> refined;
		const PrimitiveRefinementHints refineHints(false);
		prim->Refine(refined, refineHints, prim);
		if (refined.empty()) {
			LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: skipping "
				<< "primitive that produced no refined geometry "
				<< "(typeid: " << typeid(*raw).name() << ")";
			++skippedCount;
			return;
		}
		for (size_t i = 0; i < refined.size(); ++i)
			CollectTriangleInfos(refined[i], areaLight, motion, out, skippedCount);
		return;
	}

	// A genuinely unsupported leaf shape (e.g. a sphere or disk).
	LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: skipping non-triangle "
		<< "primitive (typeid: " << typeid(*raw).name() << ")";
	++skippedCount;
}

embree_accel::embree_accel(
	const vector<boost::shared_ptr<Primitive>> &p,
	bool highQuality, bool robust
)
{
	// Resolve each primitive to MeshBaryTriangles. Bucket 0 holds
	// every triangle with no motion. Each MotionPrimitive gets its own.
	primitives.clear();
	primitives.reserve(p.size());

	vector<vector<EmbreeTriangleInfo>> buckets(1);
	std::map<const MotionPrimitive *, size_t> motionBucketOf;

	u_int skippedCount = 0;
	for (u_int i = 0; i < p.size(); ++i) {
		vector<EmbreeTriangleInfo> collected;
		CollectTriangleInfos(p[i], nullptr, nullptr, collected, skippedCount);

		if (collected.empty())
			continue;

		// Use the ORIGINAL primitive's WorldBound() rather than
		// leaf triangles. This captures MotionPrimitive's bounds
		// correctly, instead of its triangles at rest.
		worldBound = Union(worldBound, p[i]->WorldBound());

		for (size_t k = 0; k < collected.size(); ++k) {
			const EmbreeTriangleInfo &info = collected[k];

			// A MotionPrimitive whose MotionSystem is IsStatic() 
			// has a constant transform which rtcSetGeometryTimeRange()
			// rejects. Bake its fixed transform directly into the
			// vertex data.
			const bool isRealMotion = info.motion && !info.motion->GetMotionSystem().IsStatic();

			size_t bucket = 0;
			if (isRealMotion) {
				auto it = motionBucketOf.find(info.motion);
				if (it == motionBucketOf.end()) {
					bucket = buckets.size();
					buckets.push_back(vector<EmbreeTriangleInfo>());
					motionBucketOf[info.motion] = bucket;
				} else {
					bucket = it->second;
				}
			}

			buckets[bucket].push_back(info);
		}
	}
	if (skippedCount > 0) {
		LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: " << skippedCount
			<< " primitive(s) skipped.";
	}

	m_dev = rtcNewDevice(nullptr);
	rtcSetDeviceErrorFunction(m_dev, errorFunction, NULL);

	m_scene = rtcNewScene(m_dev);
	m_triInfo.clear();

	u_int motionGeomCount = 0;
	for (size_t b = 0; b < buckets.size(); ++b) {
		const vector<EmbreeTriangleInfo> &triInfos = buckets[b];
		if (triInfos.empty())
			continue;

		const bool isMotionBucket = (b != 0);
		const size_t triCount = triInfos.size();

		RTCGeometry geom = rtcNewGeometry(m_dev, RTC_GEOMETRY_TYPE_TRIANGLE);

		uint32_t *indices = (uint32_t*)rtcSetNewGeometryBuffer(
			geom, RTC_BUFFER_TYPE_INDEX, 0,
			RTC_FORMAT_UINT3, sizeof(uint32_t)*3, triCount
		);
		for (size_t t = 0; t < triCount; ++t) {
			indices[t*3+0] = (uint32_t)(t*3+0);
			indices[t*3+1] = (uint32_t)(t*3+1);
			indices[t*3+2] = (uint32_t)(t*3+2);
		}

		if (!isMotionBucket) {
			float *verts = (float*)rtcSetNewGeometryBuffer(
				geom, RTC_BUFFER_TYPE_VERTEX, 0,
				RTC_FORMAT_FLOAT3, sizeof(float)*3, triCount*3
			);
			for (size_t t = 0; t < triCount; ++t) {
				const EmbreeTriangleInfo &ti = triInfos[t];
				const MeshBaryTriangle *tri = ti.triangle;

				// Triangles reached through a MotionPrimitive whose
				// MotionSystem is static still need afixed transform.
				Transform fixedXform;
				const bool hasFixedXform = (ti.motion != nullptr);
				if (hasFixedXform) {
					const MotionSystem &ms = ti.motion->GetMotionSystem();
					fixedXform = Transform(ms.Sample(ms.StartTime()));
				}

				for (size_t j = 0; j < 3; ++j) {
					Point pt = tri->GetP(j);
					if (hasFixedXform)
						pt = fixedXform * pt;
					verts[(t*3+j)*3+0] = pt.x;
					verts[(t*3+j)*3+1] = pt.y;
					verts[(t*3+j)*3+2] = pt.z;
				}
			}
		} else {
			// Motion blur: sample the motion path at StartTime()/EndTime().
			// Let Embree interpolate vertex positions between them.
			const MotionPrimitive *motion = triInfos[0].motion;
			const MotionSystem &ms = motion->GetMotionSystem();
			const float t0 = ms.StartTime();
			const float t1 = ms.EndTime();

			rtcSetGeometryTimeStepCount(geom, 2);
			rtcSetGeometryTimeRange(geom, t0, t1);

			const Transform xform0(ms.Sample(t0));
			const Transform xform1(ms.Sample(t1));

			for (int step = 0; step < 2; ++step) {
				const Transform &xform = (step == 0) ? xform0 : xform1;
				float *verts = (float*)rtcSetNewGeometryBuffer(
					geom, RTC_BUFFER_TYPE_VERTEX, step,
					RTC_FORMAT_FLOAT3, sizeof(float)*3, triCount*3
				);
				for (size_t t = 0; t < triCount; ++t) {
					const MeshBaryTriangle *tri = triInfos[t].triangle;
					for (size_t j = 0; j < 3; ++j) {
						const Point pt = xform * tri->GetP(j);
						verts[(t*3+j)*3+0] = pt.x;
						verts[(t*3+j)*3+1] = pt.y;
						verts[(t*3+j)*3+2] = pt.z;
					}
				}
			}
			++motionGeomCount;
		}

		rtcCommitGeometry(geom);
		unsigned int geomID = rtcAttachGeometry(m_scene, geom);
		rtcReleaseGeometry(geom);

		if (geomID >= m_triInfo.size())
			m_triInfo.resize(geomID + 1);
		m_triInfo[geomID] = triInfos;
	}

	rtcSetSceneBuildQuality(m_scene,
		highQuality ? RTC_BUILD_QUALITY_HIGH : RTC_BUILD_QUALITY_MEDIUM);
	if(robust)
		rtcSetSceneFlags(m_scene, RTC_SCENE_FLAG_ROBUST);

	rtcCommitScene(m_scene);

	LOG(LUX_INFO, LUX_NOERROR) << "Using Embree for ray intersection.";

	const bool robustConfirmed =
		(rtcGetSceneFlags(m_scene) & RTC_SCENE_FLAG_ROBUST) != 0;
	LOG(LUX_INFO, LUX_NOERROR) << "Using "
		<< (highQuality ? "HIGH" : "MEDIUM")
		<< " scene builder quality. Robust scene build "
		<< (robustConfirmed ? "ENABLED" : "DISABLED")
		<< ". " << motionGeomCount << " motion-blurred geometry group(s).";
}

embree_accel::~embree_accel()
{

}

RTCRay embree_accel::fill_rtc_ray(const Ray &ray) const
{
	struct RTCRay r;
	r.org_x = ray.o.x;
	r.org_y = ray.o.y;
	r.org_z = ray.o.z;
	r.dir_x = ray.d.x;
	r.dir_y = ray.d.y;
	r.dir_z = ray.d.z;
	r.tnear = ray.mint;
	r.tfar  = ray.maxt;
	r.time  = ray.time;
	r.mask  = -1;
	r.flags =  0;
	return r;
}

BBox embree_accel::WorldBound() const
{
	return worldBound;
}

bool embree_accel::CanIntersect() const
{
	return true;
}

DifferentialGeometry embree_accel::ComputeDifferentialGeometry(
	const MeshBaryTriangle *triangle, float b1, float b2,
	const Transform *motionXform) const
{
	const float b0 = 1.0f - b1 - b2;

	Point p0 = triangle->GetP(0);
	Point p1 = triangle->GetP(1);
	Point p2 = triangle->GetP(2);
	if (motionXform) {
		p0 = (*motionXform) * p0;
		p1 = (*motionXform) * p1;
		p2 = (*motionXform) * p2;
	}

	const Point o = p0;

	const Vector e1 = p1 - p0;
	const Vector e2 = p2 - p0;
	const Normal nn(Normalize(Cross(e1, e2)));
	const Point pp(o + b1 * e1 + b2 * e2);

	// Compute triangle partial derivatives
	Vector dpdu, dpdv;
	float uvs[3][2];
	triangle->GetUVs(uvs);

	// Compute deltas for triangle partial derivatives
	const float du1 = uvs[0][0] - uvs[2][0];
	const float du2 = uvs[1][0] - uvs[2][0];
	const float dv1 = uvs[0][1] - uvs[2][1];
	const float dv2 = uvs[1][1] - uvs[2][1];
	const Vector dp1(p0 - p2), dp2(p1 - p2);

	const float determinant = du1 * dv2 - dv1 * du2;
	if (determinant == 0.f) {
			// Handle zero determinant for triangle partial derivative matrix
		CoordinateSystem(Vector(nn), &dpdu, &dpdv);
		} else {
			const float invdet = 1.f / determinant;
			dpdu = ( dv2 * dp1 - dv1 * dp2) * invdet;
			dpdv = (-du2 * dp1 + du1 * dp2) * invdet;
	}

	// Interpolate $(u,v)$ triangle parametric coordinates
	const float tu = b0 * uvs[0][0] + b1 * uvs[1][0] + b2 * uvs[2][0];
	const float tv = b0 * uvs[0][1] + b1 * uvs[1][1] + b2 * uvs[2][1];

	DifferentialGeometry dg(pp, nn, dpdu, dpdv,
		Normal(0, 0, 0), Normal(0, 0, 0), tu, tv, triangle);
	dg.iData.baryTriangle.coords[0] = b0;
	dg.iData.baryTriangle.coords[1] = b1;
	dg.iData.baryTriangle.coords[2] = b2;
	return dg;
}

bool embree_accel::Intersect(const Ray &ray, Intersection *isect) const
{
	struct RTCRayHit hit;
	hit.ray = fill_rtc_ray(ray);
	hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
	hit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
	rtcIntersect1(m_scene, &hit);

	if(hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
		return false;

	ray.maxt = hit.ray.tfar;

	const EmbreeTriangleInfo &info = m_triInfo[hit.hit.geomID][hit.hit.primID];
	const MeshBaryTriangle *triangle = info.triangle;

	Transform motionXform;
	const Transform *motionXformPtr = nullptr;
	if (info.motion) {
		// Sample the MotionPrimitive transform for this ray time.
		motionXform = Transform(info.motion->GetMotionSystem().Sample(ray.time));
		motionXformPtr = &motionXform;
	}

	isect->dg = ComputeDifferentialGeometry(triangle, hit.hit.u, hit.hit.v, motionXformPtr);

	const Transform objectToWorld = motionXformPtr
		? (*motionXformPtr) * triangle->mesh->ObjectToWorld
		: triangle->mesh->ObjectToWorld;

	// If this triangle came from a MotionPrimitive, report that decorator
	// as the hit primitive; otherwise report the triangle directly.
	// Either way, pass through AreaLights.
	isect->Set(objectToWorld,
		info.motion ? static_cast<const Primitive *>(info.motion)
		            : static_cast<const Primitive *>(triangle),
		triangle->mesh->GetMaterial(),
		triangle->mesh->GetExterior(),
		triangle->mesh->GetInterior(),
		info.areaLight);

	if (info.motion)
		isect->dg.handle = info.motion;

	return true;
}

bool embree_accel::IntersectP(const Ray &ray) const
{
	struct RTCRayHit hit;
	hit.ray = fill_rtc_ray(ray);
	hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
	hit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
	rtcIntersect1(m_scene, &hit);

	if(hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
		return false;

	return true;
}

Transform embree_accel::GetLocalToWorld(float time) const
{
	return Transform();
}

void embree_accel::GetPrimitives(vector<boost::shared_ptr<Primitive>> &prims) const
{
	prims.reserve(primitives.size());
	for(u_int i = 0; i < primitives.size(); ++i)
		prims.push_back(primitives[i]);
}

Aggregate *embree_accel::CreateAccelerator(
	const vector<boost::shared_ptr<Primitive>> &prims, const ParamSet &ps
)
{
	// On by default; improves traversal speed.
	bool highQuality = ps.FindOneBool("highquality", true);
	// Off by default; degrades traversal speed. Mostly for edge cases.
	bool robust = ps.FindOneBool("robust", false);
	return new embree_accel(prims, highQuality, robust);
}

static lux::DynamicLoader::RegisterAccelerator<lux::embree_accel> r("embree");
