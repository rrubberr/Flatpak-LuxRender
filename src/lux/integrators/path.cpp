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
 *   Lux Renderer website : http://www.luxrender.net                       *
 ***************************************************************************/

// path.cpp*
#include "sampling.h"
#include "scene.h"
#include "bxdf.h"
#include "light.h"
#include "camera.h"
#include "paramset.h"
#include "dynload.h"
#include "path.h"
#include "core/partialcontribution.h"

using namespace lux;

static const u_int passThroughLimit = 10000;

// PathIntegrator Method Definitions
void PathIntegrator::RequestSamples(Sampler *sampler, const Scene &scene)
{
	vector<u_int> structure;
	structure.push_back(2);	// bsdf direction sample for path
	structure.push_back(1);	// bsdf component sample for path
	structure.push_back(1); // scattering
	if (rrStrategy != RR_NONE)
		structure.push_back(1);	// continue sample

	sampleOffset = sampler->AddxD(structure, maxDepth + 1);

	if (enableDirectLightSampling) {
		// Allocate and request samples for light sampling, RR, etc.
		hints.RequestSamples(sampler, scene, maxDepth + 1);
	}
}

void PathIntegrator::Preprocess(const RandomGenerator &rng, const Scene &scene)
{
	// Prepare image buffers
	BufferType type = BUF_TYPE_PER_PIXEL;
	scene.sampler->GetBufferType(&type);
	bufferId = scene.camera()->film->RequestBuffer(type, BUF_FRAMEBUFFER, "eye");

	hints.InitStrategies(scene);
}

//------------------------------------------------------------------------------
// SamplerRenderer integrator code
//------------------------------------------------------------------------------

u_int PathIntegrator::Li(const Scene &scene, const Sample &sample) const
{
	u_int nrContribs = 0;
	// Declare common path integration variables
	const SpectrumWavelengths &sw(sample.swl);
	Ray ray;
	float xi, yi;
	float rayWeight = sample.camera->GenerateRay(scene, sample, &ray, &xi, &yi);

	const float nLights = scene.lights.size();
	const u_int lightGroupCount = scene.lightGroups.size();
	// Direct lighting
	vector<SWCSpectrum> Ld(lightGroupCount, 0.f);
	// Direct lighting samples variance
	vector<float> Vd(lightGroupCount, 0.f);
	SWCSpectrum pathThroughput(1.0f);

	PartialContribution partialContribution(lightGroupCount);

	float VContrib = .1f;
	bool specularBounce = true, specular = true, scattered = false;
	float alpha = 1.f;
	float distance = INFINITY;
	u_int vertexIndex = 0;
	const Volume *volume = NULL;

	for (u_int pathLength = 0; ; ++pathLength) {
		const SWCSpectrum prevThroughput(pathThroughput);
		const float *data = sample.sampler->GetLazyValues(sample,
			sampleOffset, pathLength);
		// Find next vertex of path
		Intersection isect;
		BSDF *bsdf;
		float spdf;
		if (!scene.Intersect(sample, volume, scattered, ray, data[3], &isect,
			&bsdf, &spdf, NULL, &pathThroughput)) {
			pathThroughput /= spdf;
			// Dade - now I know ray.maxt and I can call volumeIntegrator
			SWCSpectrum Lv;
			u_int g = scene.volumeIntegrator->Li(scene, ray, sample,
				&Lv, &alpha);
			if (!Lv.Black()) {
				Lv *= prevThroughput;
				partialContribution.Add(sw, Lv, g, VContrib);
				++nrContribs;
			}

			// Stop path sampling since no intersection was found
			// Possibly add horizon in render & reflections
			if (!enableDirectLightSampling || (
					(includeEnvironment || vertexIndex > 0) && specularBounce)) {
				BSDF *ibsdf;
				for (u_int i = 0; i < nLights; ++i) {
					SWCSpectrum Le(pathThroughput);
					if (scene.lights[i]->Le(scene, sample,
						ray, &ibsdf, NULL, NULL, &Le)) {
						partialContribution.Add(sw, Le, scene.lights[i]->group, VContrib);
						++nrContribs;
					}
				}
			}

			// Set alpha channel
			if (vertexIndex == 0)
				alpha = 0.f;
			break;
		}
		scattered = bsdf->dgShading.scattered;
		pathThroughput /= spdf;
		if (vertexIndex == 0) {
			distance = ray.maxt * ray.d.Length();
		}

		SWCSpectrum Lv;
		const u_int g = scene.volumeIntegrator->Li(scene, ray, sample,
			&Lv, &alpha);
		if (!Lv.Black()) {
			Lv *= prevThroughput;
			partialContribution.Add(sw, Lv, g, VContrib);
			++nrContribs;
		}

		// Possibly add emitted light at path vertex
		Vector wo(-ray.d);
		if (specularBounce) {
			SWCSpectrum Le(pathThroughput);
			BSDF *ibsdf;
			if (isect.Le(sample, ray, &ibsdf, NULL, NULL, &Le)) {
				partialContribution.Add(sw, Le, isect.arealight->group, VContrib);
				++nrContribs;
			}
		}
		if (pathLength == maxDepth)
			break;
		// Evaluate BSDF at hit point

		// Estimate direct lighting
		const Point &p = bsdf->dgShading.p;
		const Normal &n = bsdf->dgShading.nn;
		if (enableDirectLightSampling && (nLights > 0)) {
			for (u_int i = 0; i < lightGroupCount; ++i) {
				Ld[i] = 0.f;
				Vd[i] = 0.f;
			}

			nrContribs += hints.SampleLights(scene, sample, p, n,
				wo, bsdf, pathLength, pathThroughput, Ld, &Vd);

			for (u_int i = 0; i < lightGroupCount; ++i) {
				partialContribution.AddUnFiltered(sw, Ld[i], i, Vd[i] * VContrib);
			}
		}

		// Sample BSDF to get new path direction
		Vector wi;
		float pdf;
		BxDFType flags;
		SWCSpectrum f;
		if (!bsdf->SampleF(sw, wo, &wi, data[0], data[1], data[2], &f,
			&pdf, BSDF_ALL, &flags, NULL, true))
			break;

		if (flags != (BSDF_TRANSMISSION | BSDF_SPECULAR) ||
			!(bsdf->Pdf(sw, wi, wo, BxDFType(BSDF_TRANSMISSION | BSDF_SPECULAR)) > 0.f)) {
			// Possibly terminate the path
			if (vertexIndex > 3) {
				if (rrStrategy == RR_EFFICIENCY) { // use efficiency optimized RR
					const float q = min<float>(1.f, f.Filter(sw));
					if (q < data[4])
						break;
					// increase path contribution
					pathThroughput /= q;
				} else if (rrStrategy == RR_PROBABILITY) { // use normal/probability RR
					if (continueProbability < data[4])
						break;
					// increase path contribution
					pathThroughput /= continueProbability;
				}
			}
			++vertexIndex;

			specularBounce = (flags & BSDF_SPECULAR) != 0;
			specular = specular && specularBounce;
		}
		pathThroughput *= f;
		if (!specular)
			VContrib += AbsDot(wi, n) / pdf;

		ray = Ray(p, wi);
		ray.time = sample.realTime;
		volume = bsdf->GetVolume(wi);
	}
	partialContribution.Splat(sw, sample, xi, yi, distance, alpha, bufferId, rayWeight);

	return nrContribs;
}

//------------------------------------------------------------------------------
// Integrator parsing code
//------------------------------------------------------------------------------

SurfaceIntegrator* PathIntegrator::CreateSurfaceIntegrator(const ParamSet &params)
{
	// general
	int maxDepth = params.FindOneInt("maxdepth", 16);

	float RRcontinueProb = params.FindOneFloat("rrcontinueprob", .65f);			// continueprobability for plain RR (0.0-1.0)
	RRStrategy rstrategy;
	string rst = params.FindOneString("rrstrategy", "efficiency");
	if (rst == "efficiency") rstrategy = RR_EFFICIENCY;
	else if (rst == "probability") rstrategy = RR_PROBABILITY;
	else if (rst == "none") rstrategy = RR_NONE;
	else {
		LOG(LUX_WARNING,LUX_BADTOKEN)<<"Strategy  '" << rst <<"' for russian roulette path termination unknown. Using \"efficiency\".";
		rstrategy = RR_EFFICIENCY;
	}
	bool include_environment = params.FindOneBool("includeenvironment", true);
	bool directLightSampling = params.FindOneBool("directlightsampling", true);

	PathIntegrator *pi = new PathIntegrator(rstrategy, max(maxDepth, 0), RRcontinueProb,
			include_environment, directLightSampling);
	// Initialize the rendering hints
	pi->hints.InitParam(params);

	return pi;
}

static DynamicLoader::RegisterSurfaceIntegrator<PathIntegrator> r("path");
