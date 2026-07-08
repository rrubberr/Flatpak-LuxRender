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

embree_accel::embree_accel(
	const vector<boost::shared_ptr<Primitive>> &p
)
{
	vector<boost::shared_ptr<Primitive>> vPrims = {};
	const PrimitiveRefinementHints refineHints(false);
	for(u_int i = 0; i < p.size(); ++i)
	{
		if(p[i]->CanIntersect())
			vPrims.push_back(p[i]);
		else
			p[i]->Refine(vPrims, refineHints, p[i]);
	}

	size_t tri_count = vPrims.size();
	u_int nPrims = vPrims.size();
	primitives = vPrims;

	for(u_int i = 0; i < nPrims; ++i)
		worldBound = Union(worldBound, vPrims[i]->WorldBound());

	m_dev = rtcNewDevice(nullptr);
	rtcSetDeviceErrorFunction(m_dev, errorFunction, NULL);

	m_scene = rtcNewScene(m_dev);
	m_geo = rtcNewGeometry(m_dev, RTC_GEOMETRY_TYPE_TRIANGLE);
	m_verts = (float*)rtcSetNewGeometryBuffer(
		m_geo, RTC_BUFFER_TYPE_VERTEX, 0,
		RTC_FORMAT_FLOAT3, sizeof(float)*3, tri_count*3
	);
	m_indices = (uint32_t*)rtcSetNewGeometryBuffer(
		m_geo, RTC_BUFFER_TYPE_INDEX, 0,
		RTC_FORMAT_UINT3, sizeof(uint32_t)*3, tri_count
	);

	size_t p_index = 0;
	size_t i_index = 0;
	for(size_t i = 0; i < vPrims.size(); i++)
	{
		const MeshBaryTriangle* t = static_cast<const MeshBaryTriangle*>(vPrims[i].get());
		for(size_t j = 0; j < 3; j++)
		{
			m_verts[p_index+0] = t->GetP(j).x;
			m_verts[p_index+1] = t->GetP(j).y;
			m_verts[p_index+2] = t->GetP(j).z;
			p_index += 3;
		}
		m_indices[i_index+0] = i_index + 0;
		m_indices[i_index+1] = i_index + 1;
		m_indices[i_index+2] = i_index + 2;
		i_index += 3;
	}

	rtcCommitGeometry(m_geo);
	rtcAttachGeometry(m_scene, m_geo);
	rtcReleaseGeometry(m_geo);
	rtcCommitScene(m_scene);

	LOG(LUX_INFO, LUX_NOERROR) << "Using Embree for ray intersection.";
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
	const MeshBaryTriangle *triangle, float b1, float b2) const
{
	const float b0 = 1.0f - b1 - b2;

	const Point p0 = triangle->GetP(0);
	const Point p1 = triangle->GetP(1);
	const Point p2 = triangle->GetP(2);

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
	const Vector dp1(triangle->GetP(0) - triangle->GetP(2)),
		dp2(triangle->GetP(1) - triangle->GetP(2));

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

	// Propagate the hit distance back to the caller's ray.
	ray.maxt = hit.ray.tfar;

	const MeshBaryTriangle *triangle(static_cast<const MeshBaryTriangle *>(primitives[
		hit.hit.primID
	].get()));

	isect->dg = ComputeDifferentialGeometry(triangle, hit.hit.u, hit.hit.v);

	isect->Set(triangle->mesh->ObjectToWorld, triangle,
		triangle->mesh->GetMaterial(),
		triangle->mesh->GetExterior(),
		triangle->mesh->GetInterior());

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
	return new embree_accel(prims);
}

static lux::DynamicLoader::RegisterAccelerator<lux::embree_accel> r("embree");
