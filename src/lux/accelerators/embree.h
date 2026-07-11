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

#pragma once

#include "lux.h"
#include "primitive.h"
#include "embree4/rtcore.h"
#include "embree4/rtcore_common.h"

namespace lux
{
	class MeshBaryTriangle;
	class MotionPrimitive;
	class AreaLightPrimitive;
	class AreaLight;

	// Embree error callback: forwards Embree's internal errors
	// to stdout. Defined in embree.cpp.
	void errorFunction(void* userPtr, enum RTCError error, const char* str);

	// Per-triangle metadata resolved at construction time, by
	// unwrapping AreaLightPrimitive/MotionPrimitive to
	// MeshBaryTriangle.
	struct EmbreeTriangleInfo
	{
		const MeshBaryTriangle *triangle = nullptr;
		// Non-null if this triangle was reached through an AreaLightPrimitive.
		const AreaLight *areaLight = nullptr;
		// Non-null if this triangle was reached through a MotionPrimitive.
		// All triangles sharing a MotionPrimitive are grouped into
		// the same RTCGeometry.
		const MotionPrimitive *motion = nullptr;
	};

	class embree_accel : public Aggregate
	{
		public:
			embree_accel(
				const vector<boost::shared_ptr<Primitive>> &p,
				bool highQuality = true, bool robust = false
			);

			~embree_accel();

			// Builds an RTCRay from a Lux Ray for use with rtcIntersect1().
			RTCRay fill_rtc_ray(const Ray &ray) const;

			BBox WorldBound() const;

			bool CanIntersect() const;

			bool Intersect(const Ray &ray, Intersection *isect) const;

			bool IntersectP(const Ray &ray) const;

			Transform GetLocalToWorld(float time) const;

			void GetPrimitives(vector<boost::shared_ptr<Primitive>> &prims) const;

			static Aggregate *CreateAccelerator(
				const vector<boost::shared_ptr<Primitive>> &prims, const ParamSet &ps
			);

		protected:
			// Builds the DifferentialGeometry for a hit on the
			// given triangle. If motionXform is non-null, the
			// vertices are transformed by it first.
			DifferentialGeometry ComputeDifferentialGeometry(
				const MeshBaryTriangle *triangle, float b1, float b2,
				const Transform *motionXform = nullptr) const;

			// Recursively resolve prim down to MeshBaryTriangle leaves,
			// appending one EmbreeTriangleInfo per leaf.

			// Anything left over after that is logged and dropped.
			void CollectTriangleInfos(const boost::shared_ptr<Primitive> &prim,
				const AreaLight *areaLight, const MotionPrimitive *motion,
				vector<EmbreeTriangleInfo> &out, u_int &skippedCount);

			vector<boost::shared_ptr<Primitive>> primitives = {};

			// Indexed as [geomID][primID]. geomIDs are assigned by
			// rtcAttachGeometry in the order geometries are built.
			// Create one static geometry, plus one additional
			// per MotionPrimitive among the input.
			vector<vector<EmbreeTriangleInfo>> m_triInfo;

			RTCScene m_scene  = 0;
			RTCDevice m_dev   = 0;
			BBox worldBound;
	};
};
