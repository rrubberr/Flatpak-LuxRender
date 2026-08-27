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

#ifndef LUX_LUX_H
#define LUX_LUX_H

// lux.h*
// Global Include Files
#include <cmath>

#ifdef __CYGWIN__
#include <ieeefp.h>
#endif
#include <string>
using std::string;
#include <vector>
using std::vector;
#include <iostream>
using std::ostream;
#include <algorithm>
using std::min;
using std::max;
using std::swap;
using std::sort;

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/version.hpp>

#include "luxrays/luxrays.h"
#include "luxrays/core/epsilon.h"
#include "luxrays/core/geometry/transform.h"
#include "luxrays/core/geometry/motionsystem.h"
#include "luxrays/core/color/swcspectrum.h"
#include "luxrays/core/utils.h"
#include "luxrays/utils/memory.h"

// boost version starting from 1.50 defined TIME_UTC_ instead of TIME_UTC because of a conflict with libc and c++ 2011
// https://svn.boost.org/trac/boost/ticket/6940
// glibc > 1.16 includes a TIME_UTC macro, so boost renamed to TIME_UTC_
//
// This hook allows to build with boost < 1.50 and glibc < 1.16
//
#if (BOOST_VERSION < 105000)
#define TIME_UTC_ TIME_UTC
#endif

// Platform-specific definitions

#if defined (__INTEL_COMPILER)
// Dade - to fix a problem with expf undefined with Intel CC
inline float expf(float a) { return exp(a); }
#endif

// Global Constants
#define LUX_PATH_SEP ":"

// Global Type Declarations
#define BC_GRID_SIZE 40
typedef vector<int> SampleGrid[BC_GRID_SIZE][BC_GRID_SIZE];
#define GRID(v) (int((v) * BC_GRID_SIZE))

// Global Forward Declarations
class Timer;

namespace lux
{
  using luxrays::MemoryArena;
  using luxrays::AllocAligned;
  using luxrays::FreeAligned;
  using luxrays::BlockedArray;
  using luxrays::Float2UInt;
  using luxrays::Floor2Int;
  using luxrays::Floor2UInt;
  using luxrays::Ceil2Int;
  using luxrays::Ceil2UInt;
  using luxrays::Round2Int;
  using luxrays::Round2UInt;
  using luxrays::Clamp;
  using luxrays::Lerp;
  using luxrays::Log2;
  using luxrays::Radians;
  using luxrays::IsPowerOf2;
  using luxrays::RoundUpPow2;
  class ParamSet;
  template <class T> struct ParamSetItem;
  using luxrays::BBox;
  using luxrays::MachineEpsilon;
  using luxrays::Matrix4x4;
  using luxrays::Normal;
  using luxrays::Point;
  using luxrays::Ray;
  using luxrays::Transform;
  using luxrays::Vector;
  class RayDifferential;
  class DifferentialGeometry;
  class Renderer;
  class Scene;
  class Primitive;
  class AreaLightPrimitive;
  class InstancePrimitive;
  class MotionPrimitive;
  class Aggregate;
  class Intersection;
  class ImageData;
  class MIPMap;
  using luxrays::SWCSpectrum;
  using luxrays::SpectrumWavelengths;
  using luxrays::RGBColor;
  using luxrays::RGBAColor;
  using luxrays::XYZColor;
  using luxrays::ColorSystem;
  using luxrays::ColorAdaptator;
  using luxrays::SPD;  
  class Camera;
  class CameraResponse;
  class ProjectiveCamera;
  class Sampler;
  class PixelSampler;
  class IntegrationSampler;
  class Sample;
  class Filter;
  class Film;
  class ToneMap;
  class BxDF;
  class BRDF;
  class BTDF;
  class Fresnel;
  class FresnelConductor;
  class FresnelDielectric;
  class FresnelGeneral;
  class FresnelGeneric;
  class FresnelNoOp;
  class FresnelSlick;
  class SpecularReflection;
  class SpecularTransmission;
  class Lambertian;
  class OrenNayar;
  class Microfacet;
  class MicrofacetDistribution;
  class BSDF;
  class Material;
  struct CompositingParams;
  class TextureMapping2D;
  class UVMapping2D;
  class SphericalMapping2D;
  class CylindricalMapping2D;
  class PlanarMapping2D;
  class TextureMapping3D;
  class IdentityMapping3D;
  class TriangleMesh;
  class PlyMesh;
  template <class T> class Texture;
  class Volume;
  class Region;
  class Light;
  struct VisibilityTester;
  class AreaLight;
  class Shape;
  class PrimitiveSet;
  class Integrator;
  class SurfaceIntegrator;
  class VolumeIntegrator;
  class RandomGenerator;
  class RenderFarm;
  class Contribution;
  class ContributionBuffer;
  class ContributionPool;
  class ContributionSystem;
  class InterpolatedTransform;
  using luxrays::MotionSystem;
  using luxrays::MotionTransform;
  class SampleableSphericalFunction;

  class Context;

// Global Function Declarations
  //string hashing function
  unsigned int DJBHash(const std::string& str);

  // accepts platform-specific filenames and performs fallback
  ImageData *ReadImage(const string &name);

  // converts paths to portable format and 
  // provides fallback mechanism for missing files
  string AdjustFilename(const string filename, bool silent = false);

}

#endif // LUX_LUX_H
