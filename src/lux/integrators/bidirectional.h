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

// bidirectional.cpp*
#include "lux.h"
#include "sampling.h"
#include "transport.h"
#include "reflection/bxdf.h"
#include "renderinghints.h"


namespace lux
{

class BidirVertex;

// Bidirectional Local Declarations
class BidirIntegrator : public SurfaceIntegrator {
public:
	BidirIntegrator(u_int ed, u_int ld, float et, float lt,
		LightsSamplingStrategy *lds, u_int src,
		LightsSamplingStrategy *lps, u_int lrc,
		bool d) : SurfaceIntegrator(),
		maxEyeDepth(ed), maxLightDepth(ld),
		eyeThreshold(et), lightThreshold(lt),
		lightDirectStrategy(lds), lightPathStrategy(lps),
		shadowRayCount(src), lightRayCount(lrc),
		debug(d) {
		directSamplingCount = 0;
		pathSamplingCount = 0;
		eyeBufferId = 0;
		lightBufferId = 0;
		AddStringConstant(*this, "name", "Name of current surface integrator", "bidirectional");
		AddIntAttribute(*this, "maxEyeDepth", "Eye path max. depth", &BidirIntegrator::GetMaxEyeDepth);
		AddIntAttribute(*this, "maxLightDepth", "Light path max. depth", &BidirIntegrator::GetMaxLightDepth);
	}
	virtual ~BidirIntegrator() { }
	// BidirIntegrator Public Methods
	virtual u_int Li(const Scene &scene, const Sample &sample) const;
	virtual void RequestSamples(Sampler *sample, const Scene &scene);
	virtual void Preprocess(const RandomGenerator &rng, const Scene &scene);

	static SurfaceIntegrator *CreateSurfaceIntegrator(const ParamSet &params);

	u_int maxEyeDepth, maxLightDepth;
	float eyeThreshold, lightThreshold;
	u_int sampleEyeOffset;
	u_int eyeBufferId, lightBufferId;
	vector<u_int> sampleLightOffsets;

private:
	// Used by Queryable interface
	u_int GetMaxEyeDepth() { return maxEyeDepth; }
	u_int GetMaxLightDepth() { return maxLightDepth; }

	/**
	 * Compute the weight of the given path for MIS.
	 * @param eye The eye path
	 * @param nEye The length of the eye path in case only a subpath is to be considered
	 * @param light The light path
	 * @param nLight The length of the light path in case only a subpath is to be considered
	 * @param pdfLightDirect The probability of sampling the light path origin with next event estimation
	 * @param isLightDirect Compute the weight for next event estimation when true
	 * @return The path weight for MIS
	 */
	float WeightPath(const vector<BidirVertex> &eye, u_int nEye,
		const vector<BidirVertex> &light, u_int nLight,
		float pdfLightDirect, bool isLightDirect) const;
	/**
	 * Evaluates a path contribution weigthed for MIS
	 * Modified fields (save them before the call if you need to preserve them):
	 * eyeV.flags
	 * lightV.flags
	 * eyeV.rr
	 * eyeV.rrR
	 * eyeV.dAWeight
	 * lightV.rr
	 * lightV.rrR
	 * lightV.dARWeight
	 * light[nLight - 2].dARWeight
	 * eyeV.wi
	 * eyeV.d2
	 * @param scene The scene being rendered
	 * @param sample The sample used for rendering
	 * @param eye The eye path
	 * @param nEye The length of the eye path in case only a subpath is to be considered
	 * @param light The light path
	 * @param nLight The length of the light path in case only a subpath is to be considered
	 * @param pdfLightDirect The probability of sampling the light path origin with next event estimation
	 * @param isLightDirect Compute the weight for next event estimation when true
	 * @param weight A pointer to a float to return the path weight
	 * @param L A pointer to a SWCSpectrum to return the path contribution
	 * @param single is true if the evaluated path is dispersed, false otherwise
	 * @return True if the path brings a contribution, false otherwise
	 */
	bool EvalPath(const Scene &scene, const Sample &sample,
		vector<BidirVertex> &eye, u_int nEye,
		vector<BidirVertex> &light, u_int nLight,
		float pdfLightDirect, bool isLightDirect, float *weight,
		SWCSpectrum *L, bool &single) const;
	/**
	 * Next event estimation to light an eye path
	 * @param scene The scene being rendered
	 * @param sample The sample used for rendering
	 * @param eyePath The eye path
	 * @param length The length of the eye path in case only a subpath is to be considered
	 * @param light a pointer to the light chosen for the next event
	 * @param u0 First random variable to sample the position on the light
	 * @param u1 Second random variable to sample the position on the light
	 * @param portal A random variable to sample the light portal if any
	 * @param lightWeight The probability of sampling the light as the source of a light path
	 * @param directWeight The probability of sampling the light as the source of the next event
	 * @param Ld A pointer to a SWCSpectrum to return the light contribution
	 * @param weight A pointer to a float to return the contribution weight
	 * @return True if sampling was successful in returning a contribution, false otherwise
	 */
	bool GetDirectLight(const Scene &scene, const Sample &sample,
		vector<BidirVertex> &eyePath, u_int length, const Light *light,
		float u0, float u1, float portal, float lightWeight,
		float directWeight, SWCSpectrum *Ld, float *weight) const;
	// BidirIntegrator Data
	LightsSamplingStrategy *lightDirectStrategy, *lightPathStrategy;
	u_int shadowRayCount, lightRayCount;
	u_int directSamplingCount, pathSamplingCount;
	u_int lightNumOffset, lightPortalOffset;
	u_int lightPosOffset, sampleDirectOffset;
	bool debug;
};

}//namespace lux
