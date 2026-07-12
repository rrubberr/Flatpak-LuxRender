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
	class MeshQuadrilateral;
	class MotionPrimitive;
	class AreaLightPrimitive;
	class AreaLight;

	// Embree error callback: forwards Embree's internal errors
	// to stdout. Defined in embree.cpp.
	void errorFunction(void* userPtr, enum RTCError error, const char* str);

	// Per-leaf metadata resolved at construction time, by unwrapping
	// Area and Motion Primitive down to a triangle or quad.
	struct EmbreeLeafInfo
	{
		const MeshBaryTriangle *triangle = nullptr;
		const MeshQuadrilateral *quad = nullptr;
		// Non-null if this leaf was reached through an AreaLightPrimitive.
		const AreaLight *areaLight = nullptr;
		// Non-null if this leaf was reached through a MotionPrimitive.
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
			// Builds the DifferentialGeometry for a hit on the given
			// triangle. If motionXform is non-null, the vertices are
			// transformed by it first.
			DifferentialGeometry ComputeDifferentialGeometry(
				const MeshBaryTriangle *triangle, float b1, float b2,
				const Transform *motionXform = nullptr) const;

			// Same, for a hit on a quad.
			DifferentialGeometry ComputeDifferentialGeometry(
				const MeshQuadrilateral *quad, float u, float v,
				const Transform *motionXform = nullptr) const;

			// Recursively resolve down to MeshBaryTriangle or
			// MeshQuadrilateral and append one EmbreeLeafInfo per
			// leaf.
			
			// Anything left over is logged and dropped!
			void CollectLeafInfos(const boost::shared_ptr<Primitive> &prim,
				const AreaLight *areaLight, const MotionPrimitive *motion,
				vector<EmbreeLeafInfo> &out, u_int &skippedCount);

			// Builds one RTCGeometry per non-empty bucket (bucket 0 is
			// static; then one per MotionPrimitive), of the Embree 
			// geometry type, and records the metadata into
			// m_leafInfo indexed by geomID.
			
			// Shared between the triangle and quad passes in the
			// constructor so the static-vs-motion logic
			// only exists once.
			void BuildGeometryBuckets(
				const vector<vector<EmbreeLeafInfo>> &buckets,
				RTCGeometryType geomType, u_int vertsPerLeaf,
				u_int &motionGeomCount);

			vector<boost::shared_ptr<Primitive>> primitives = {};

			// Indexed as [geomID][primID]. geomIDs are assigned by
			// rtcAttachGeometry in the order geometries are built.
			vector<vector<EmbreeLeafInfo>> m_leafInfo;

			RTCScene m_scene  = 0;
			RTCDevice m_dev   = 0;
			BBox worldBound;
	};
};
