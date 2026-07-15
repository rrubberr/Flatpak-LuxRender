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
#include "bidirectional.h"
#include "reflection/bxdf.h"
#include "light.h"
#include "camera.h"
#include "sampling.h"
#include "scene.h"
#include "paramset.h"
#include "dynload.h"
#include "core/partialcontribution.h"

using namespace lux;

static const u_int passThroughLimit = 10000;
static const u_int rrStart = 3;

class lux::BidirVertex {
public:
	BidirVertex() : pdf(0.f), pdfR(0.f), tPdf(1.f), tPdfR(1.f),
		dAWeight(0.f), dARWeight(0.f), rr(1.f), rrR(1.f),
		flux(0.f), bsdf(NULL), flags(BxDFType(0)) {}

	bool EyeConnect(const Sample &sample, const XYZColor &color,
		float alpha, float distance, float weight,
		u_int bufferId, u_int groupId) const {
		float x, y;
		if (!sample.camera->GetSamplePosition(p, wi, distance, &x, &y))
			return false;
		sample.AddContribution(x, y, color, alpha, distance, weight,
			bufferId, groupId);
		return true;
	}

	// cosi: cosine of the angle between the shading normal and the direction towards the light
	// coso: cosine of the angle between the shading normal and the direction towards the eye
	// pdf: probability of sampling wo knowing wi
	// pdfR: probability of sampling wi knowing wo
	// tPdf: probability of reaching the next vertex towards the eye without scattering
	// tPdfR: probability of reaching the next vertex towards the light without scattering
	// dAWeight, dARWeight: weighting factors for MIS
	// rr: russian roulette probability in direction wo
	// rrR: russian roulette probability in direction wi
	// d2: squared distance towards the next vertex (depends on the direction)
	float cosi, coso, pdf, pdfR, tPdf, tPdfR, dAWeight, dARWeight, rr, rrR, d2, padding;
	// flux: flux from the beginning of the path up to this vertex, excluding scattering at this vertex, probability weighted
	SWCSpectrum flux;
	// bsdf: the BSDF at that vertex
	BSDF *bsdf;
	// flags: scattering flags at this vertex describing the direction sampling process
	BxDFType flags;
	// wi: direction towards the light
	// wo: direcion towards the eye
	Vector wi, wo;
	// p: the location of this vertex
	Point p;
	// single: flag indicating if only a single wavelength remains at this vertex due to dispersion
	bool single;
};

// Bidirectional Method Definitions
void BidirIntegrator::RequestSamples(Sampler *sampler, const Scene &scene)
{
	directSamplingCount = lightDirectStrategy->GetSamplingLimit(scene);
	pathSamplingCount = lightPathStrategy->GetSamplingLimit(scene);
	lightNumOffset = sampler->Add1D(pathSamplingCount);
	lightPortalOffset = sampler->Add1D(pathSamplingCount * lightRayCount);
	lightPosOffset = sampler->Add2D(pathSamplingCount * lightRayCount);
	vector<u_int> structure;
	// Direct lighting samples
	for (u_int i = 0; i < directSamplingCount; ++i) {
		structure.push_back(1); // light source sample
		for (u_int j = 0; j < shadowRayCount; ++j) {
			structure.push_back(2);	//light position
			structure.push_back(1);	//light portal
		}
	}
	sampleDirectOffset = sampler->AddxD(structure, maxEyeDepth);
	structure.clear();
	// Eye subpath samples
	structure.push_back(1);	//continue eye
	structure.push_back(2);	//bsdf sampling for eye path
	structure.push_back(1);	//bsdf component for eye path
	structure.push_back(1); //scattering
	sampleEyeOffset = sampler->AddxD(structure, maxEyeDepth);
	structure.clear();
	// Light subpath samples
	const bool initOffsets = sampleLightOffsets.empty();
	structure.push_back(1); //continue light
	structure.push_back(2); //bsdf sampling for light path
	structure.push_back(1); //bsdf component for light path
	structure.push_back(1); //scattering
	for (u_int i = 0; i < pathSamplingCount * lightRayCount; ++i) {
		const u_int lightOffset = sampler->AddxD(structure, maxLightDepth);
		if (initOffsets) {
			// only initialize once, in case thread is added after rendering has started
			sampleLightOffsets.push_back(lightOffset);
		}
	}
}
void BidirIntegrator::Preprocess(const RandomGenerator &rng, const Scene &scene)
{
	// Prepare image buffers
	BufferOutputConfig config = BUF_FRAMEBUFFER;
	if (debug)
		config = BufferOutputConfig(config | BUF_STANDALONE);
	BufferType type = BUF_TYPE_PER_PIXEL;
	scene.sampler->GetBufferType(&type);
	eyeBufferId = scene.camera()->film->RequestBuffer(type, config, "eye");
	lightBufferId = scene.camera()->film->RequestBuffer(BUF_TYPE_PER_SCREEN,
		config, "light");
	lightDirectStrategy->Init(scene);
	lightPathStrategy->Init(scene);
}

// Weighting of path with regard to alternate methods of obtaining it
float BidirIntegrator::WeightPath(const vector<BidirVertex> &eye, u_int nEye,
	const vector<BidirVertex> &light, u_int nLight,
	float pdfLightDirect, bool isLightDirect) const
{
	// Weight of the current path without direct sampling
	// Used as a reference to extend eye or light subpaths
	// Current path weight is 1
	const float pBase = (nLight == 1 && isLightDirect) ?
		fabsf(light[0].dAWeight) / pdfLightDirect : 1.f;
	float weight = 1.f, p = pBase;
	// If direct lighting and the light isn't unidirectional
	// the path can also be obtained through connection to
	// the light vertex with normal sampling
	if (nLight == 1) {
		if (isLightDirect) {
			if ((light[0].flags & BSDF_SPECULAR) == 0 && maxLightDepth > 0 &&
				light[0].dAWeight > 0.f)
				weight += pBase * pBase;
		} else if (light[0].dAWeight > 0.f) {
			// The eye-hits-light strategy is impossible for delta lights
			// (negative dAWeight), so only add it for non-delta lights.
			const float pDirect = pdfLightDirect / fabsf(light[0].dAWeight);
			weight += pDirect * pDirect;
		}
	}
	// Check for direct path when the eye path hit a light
	// The eye path has at least 2 vertices
	// The light vertex cannot be specular otherwise
	// the eye path wouldn't have received any light
	if (nLight == 0 && (eye[nEye - 2].flags & BSDF_SPECULAR) == 0) {
		float pDirect = pdfLightDirect / eye[nEye - 1].dARWeight;
		if (nEye > rrStart + 1)
			pDirect /= eye[nEye - 2].rrR;
		weight += pDirect * pDirect;
	}
	// Find other paths by extending light path toward eye path
	const u_int nLightExt = min(nEye, maxLightDepth - min(maxLightDepth, nLight));
	for (u_int i = 1; i <= nLightExt; ++i) {
		// Exit if the path is impossible
		if (!(eye[nEye - i].dARWeight > 0.f && eye[nEye - i].dAWeight > 0.f))
			break;
		// Compute new path relative probability
		p *= eye[nEye - i].dAWeight / eye[nEye - i].dARWeight;
		// Adjust for round robin termination
		if (nEye - i > rrStart)
			p /= eye[nEye - i - 1].rrR;
		if (nLight + i > rrStart + 1) {
			if (i == 1)
				p *= light[nLight - 1].rr;
			else
				p *= eye[nEye - i + 1].rr;
		}
		// The path can only be obtained if none of the vertices
		// is specular
		if ((eye[nEye - i].flags & BSDF_SPECULAR) == 0 &&
			(i == nEye || (eye[nEye - i - 1].flags & BSDF_SPECULAR) == 0))
			weight += p * p;
	}
	// Reinitialize p to search paths in the other direction
	p = pBase;
	// Find other paths by extending eye path toward light path
	u_int nEyeExt = min(nLight, maxEyeDepth - min(maxEyeDepth, nEye));
	for (u_int i = 1; i <= nEyeExt; ++i) {
		// Exit if the path is impossible
		if (!(light[nLight - i].dARWeight > 0.f && light[nLight - i].dAWeight > 0.f))
				break;
		// Compute new path relative probability
		p *= light[nLight - i].dARWeight / light[nLight - i].dAWeight;
		// Adjust for round robin termination
		if (nLight - i > rrStart)
			p /= light[nLight - i - 1].rr;
		if (nEye + i > rrStart + 1) {
			if (i == 1)
				p *= eye[nEye - 1].rrR;
			else
				p *= light[nLight - i + 1].rrR;
		}
		// The path can only be obtained if none of the vertices
		// is specular
		if ((light[nLight - i].flags & BSDF_SPECULAR) == 0 &&
			(i == nLight || (light[nLight - i - 1].flags & BSDF_SPECULAR) == 0))
			weight += p * p;
		// Check for direct path
		// Light path has at least 2 vertices here
		// The path can only be obtained if the second vertex
		// is not specular.
		// Even if the light source vertex is specular,
		// the special sampling for direct lighting will get it
		if (i == nLight - 1 && (light[1].flags & BSDF_SPECULAR) == 0 &&
			light[0].dAWeight > 0.f) {
			// Exclude delta lights: the eye cannot hit them directly.
			const float pDirect = p * pdfLightDirect / fabsf(light[0].dAWeight);
			weight += pDirect * pDirect;
		}
	}
	return weight;
}

/*
 * Modified fields:
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
 */
bool BidirIntegrator::EvalPath(const Scene &scene, const Sample &sample,
	vector<BidirVertex> &eye, u_int nEye,
	vector<BidirVertex> &light, u_int nLight,
	float pdfLightDirect, bool isLightDirect, float *weight,
	SWCSpectrum *L, bool &single) const
{
	static const float epsilon = MachineEpsilon::E(1.f);
	// If each path has at least 1 vertex, connect them
	if (nLight <= 0 || nEye <= 0)
		return false;
	const SpectrumWavelengths &sw(sample.swl);
	*weight = 0.f;
	// Be carefull, eye and light last vertex can be modified here
	BidirVertex &eyeV(eye[nEye - 1]);
	BidirVertex &lightV(light[nLight - 1]);

	// Locally revert the single flag
	// This function may change it (by doing the bidirectional connections) and
	// this have an influence on future evaluation of the single flag.
	//
	// This context manager ensure that the flag will be restored to its
	// current state at the end of the function
	ContextSingle ctx(sw);
	sw.single = false;

	// Check Connectability
	eyeV.flags = BxDFType(~BSDF_SPECULAR);
	const Vector ewi(Normalize(lightV.p - eyeV.p));
	const SWCSpectrum ef(eyeV.bsdf->F(sw, ewi, eyeV.wo, true, eyeV.flags));
	if (ef.Black())
		return false;
	lightV.flags = BxDFType(~BSDF_SPECULAR);
	const Vector lwo(-ewi);
	const SWCSpectrum lf(lightV.bsdf->F(sw, lightV.wi, lwo, false, lightV.flags));
	if (lf.Black())
		return false;
	const float epdfR = eyeV.bsdf->Pdf(sw, eyeV.wo, ewi, eyeV.flags);
	const float lpdf = lightV.bsdf->Pdf(sw, lightV.wi, lwo, lightV.flags);
	float ltPdf = 1.f;
	float etPdfR = 1.f;
	const Volume *volume = eyeV.bsdf->GetVolume(ewi);
	if (!volume)
		volume = lightV.bsdf->GetVolume(lwo);
	const bool eScat = eyeV.bsdf->dgShading.scattered;
	const bool lScat = lightV.bsdf->dgShading.scattered;
	if (!scene.Connect(sample, volume, eScat, lScat, eyeV.p, lightV.p,
		nEye == 1, L, &ltPdf, &etPdfR))
		return false;
	const float d2 = DistanceSquared(eyeV.p, lightV.p);
	if (d2 < max(MachineEpsilon::E(eyeV.p), MachineEpsilon::E(lightV.p)))
		return false;
	// Connect eye and light vertices
	*L *= lightV.flux * lf * ef * eyeV.flux / d2;
	if (L->Black())
		return false;
	// Evaluate factors for eye path weighting
	const float ecosi = AbsDot(ewi, eyeV.bsdf->ng);
	const float epdf = eyeV.bsdf->Pdf(sw, ewi, eyeV.wo, eyeV.flags);
	if (nEye == 1)
		eyeV.rr = 1.f;
	else if (ecosi * epdf > epsilon)
		eyeV.rr = min(1.f, max(lightThreshold, ef.Filter(sw) *
			eyeV.coso / (ecosi * epdf)));
	else
		eyeV.rr = 0.f;
	if (epdfR > epsilon)
		eyeV.rrR = min(1.f, max(eyeThreshold, ef.Filter(sw) / epdfR));
	else
		eyeV.rrR = 0.f;
	eyeV.dAWeight = lpdf * ltPdf / d2;
	if (!eScat)
		eyeV.dAWeight *= ecosi;
	const float eWeight = nEye > 1 ? eye[nEye - 2].dAWeight : 0.f;
	if (nEye > 1) {
		eye[nEye - 2].dAWeight = epdf * eyeV.tPdf / eye[nEye - 2].d2;
		if (!eye[nEye - 2].bsdf->dgShading.scattered)
			eye[nEye - 2].dAWeight *= eye[nEye - 2].cosi;
	}
	// Evaluate factors for light path weighting
	const float lcoso = AbsDot(lwo, lightV.bsdf->ng);
	const float lpdfR = lightV.bsdf->Pdf(sw, lwo, lightV.wi, lightV.flags);
	if (lpdf > epsilon)
		lightV.rr = min(1.f, max(lightThreshold, lf.Filter(sw) / lpdf));
	else
		lightV.rr = 0.f;
	if (nLight == 1)
		lightV.rrR = 1.f;
	else if (lcoso * lpdfR > epsilon)
		lightV.rrR = min(1.f, max(eyeThreshold, lf.Filter(sw) *
			lightV.cosi / (lcoso * lpdfR)));
	else
		lightV.rrR = 0.f;
	lightV.dARWeight = epdfR * etPdfR / d2;
	if (!lScat)
		lightV.dARWeight *= lcoso;
	// Save the original dARWeight of the second-to-last light vertex so it
	// can be restored after WeightPath, which reads it for eye-path
	// extensions (i == 2 term).  Leaving the connection weight in place
	// corrupts the MIS weight of every subsequent eye-vertex connection,
	// producing non-converging fireflies.
	const float lWeight = nLight > 1 ? light[nLight - 2].dARWeight : 0.f;
	if (nLight > 1) {
		light[nLight - 2].dARWeight = lpdfR * lightV.tPdfR /
			light[nLight - 2].d2;
		if (!light[nLight - 2].bsdf->dgShading.scattered)
			light[nLight - 2].dARWeight *= light[nLight - 2].coso;
	}
	const float w = 1.f / WeightPath(eye, nEye, light, nLight,
		pdfLightDirect, isLightDirect);
	*weight = w;
	*L *= w;
	if (nEye > 1)
		eye[nEye - 2].dAWeight = eWeight;
	// Restore the original dARWeight of the second-to-last light vertex
	if (nLight > 1)
		light[nLight - 2].dARWeight = lWeight;
	// return back some eye data
	eyeV.wi = ewi;
	eyeV.d2 = d2;

	// The state depends on the state of both vertices + the flag of the F
	// function evaluation (which is stored on sw.single)
	single = sw.single || eyeV.single || lightV.single;

	return true;
}

bool BidirIntegrator::GetDirectLight(const Scene &scene, const Sample &sample,
	vector<BidirVertex> &eyePath, u_int length, const Light *light,
	float u0, float u1, float portal, float lightWeight, float directWeight,
	SWCSpectrum *Ld, float *weight) const
{
	vector<BidirVertex> lightPath(1);
	BidirVertex &vE(eyePath[length - 1]);
	BidirVertex &vL(lightPath[0]);
	float ePdfDirect;
	// Sample the chosen light
	if (!light->SampleL(scene, sample, vE.p, u0, u1, portal,
		&vL.bsdf, &vL.dAWeight, &ePdfDirect, Ld))
		return false;
	vL.p = vL.bsdf->dgShading.p;
	vL.wi = Vector(vL.bsdf->dgShading.nn);
	vL.cosi = AbsDot(vL.wi, vL.bsdf->ng);
	vL.dAWeight *= lightWeight;
	vL.flux = SWCSpectrum(1.f / directWeight);
	vL.tPdf = 1.f;
	vL.tPdfR = 1.f;
	vL.single = sample.swl.single;
	if (light->IsDeltaLight()) {
		// Negative dAWeight signals a delta light to WeightPath; mirror it on
		// dARWeight so the eye-hits-light strategy is correctly excluded.
		vL.dAWeight = -vL.dAWeight;
		vL.dARWeight = -fabsf(vL.dAWeight);
	} else {
		// For a non-delta light the eye can hit it directly; dARWeight is the
		// area-measure pdf, equal to dAWeight here (no prior vertex).
		vL.dARWeight = fabsf(vL.dAWeight);
	}
	ePdfDirect *= directWeight;
	vE.tPdf = ePdfDirect;
	bool single; // TODO: where is this used
	if (!EvalPath(scene, sample, eyePath, length, lightPath, 1,
		ePdfDirect, true, weight, Ld, single))
		return false;
	return true;
}

u_int BidirIntegrator::Li(const Scene &scene, const Sample &sample) const
{
	u_int nrContribs = 0;
	// Allocate at least 1 eye vertex slot for the camera vertex so that
	// light-to-camera connections work even in pure light-tracing mode
	// (maxEyeDepth == 0).  The slot at index 0 is always the camera vertex;
	// indices 1..maxEyeDepth-1 are surface intersections.
	vector<BidirVertex> eyePath(max(maxEyeDepth, 1u)), lightPath(max(maxLightDepth, 1u));
	const u_int nGroups = scene.lightGroups.size();
	const u_int numberOfLights = scene.lights.size();
	// If there are no lights, the scene is black
	//FIXME: unless there are emissive volumes
	if (numberOfLights == 0)
		return nrContribs;
	const SpectrumWavelengths &sw(sample.swl);

	PartialContribution partialContribution(nGroups);
	float alpha = 1.f;

	// Sample eye subpath origin
	const float posX = sample.camera->IsLensBased() ? sample.lensU : sample.imageX;
	const float posY = sample.camera->IsLensBased() ? sample.lensV : sample.imageY;
	// Sample the camera vertex — always needed for light-to-camera connections,
	// regardless of maxEyeDepth.  Without a valid camera BSDF we cannot
	// evaluate any contribution, so return if sampling fails.
	if (!sample.camera->SampleW(sample.arena, sw, scene,
		posX, posY, .5f, &eyePath[0].bsdf, &eyePath[0].dARWeight,
		&eyePath[0].flux))
		return nrContribs;
	BidirVertex &eye0(eyePath[0]);
	// Initialize eye vertex
	eye0.p = eye0.bsdf->dgShading.p;
	eye0.wo = Vector(eye0.bsdf->dgShading.nn);
	eye0.coso = AbsDot(eye0.wo, eye0.bsdf->ng);
	// The camera has no prior vertex through which a light path could
	// extend, so dARWeight is zero (prevents WeightPath from counting
	// an impossible "camera pass-through" strategy).
	eye0.dARWeight = 0.f;
	eye0.single = sw.single;
	u_int nEye = 1;

	// Eye-path direct lighting — uses sampleDirectOffset which is only
	// allocated for maxEyeDepth > 0 samples.  Skip entirely in light-
	// tracing-only mode; the light loop below handles all contributions.
	if (maxEyeDepth > 0) {
	// Do eye vertex direct lighting
	const float *directData0 = sample.sampler->GetLazyValues(sample,
		sampleDirectOffset, 0);
	for (u_int l = 0; l < directSamplingCount; ++l) {
		const u_int offset = l * (1 + shadowRayCount * 3);
		SWCSpectrum Ld;
		float dWeight, dPdf;
		float portal = directData0[offset];
		const Light *light = lightDirectStrategy->SampleLight(scene, l,
			&portal, &dPdf);
		if (!light)
			break;
		dPdf *= shadowRayCount;
		const float lPdf = lightPathStrategy->Pdf(scene, light) *
			lightRayCount;
		for (u_int s = 0;s < shadowRayCount; ++s) {
			const u_int offset2 = offset + s * 3 + 1;
			if (GetDirectLight(scene, sample, eyePath, 1, light,
				directData0[offset2], directData0[offset2 + 1],
				directData0[offset2 + 2], lPdf, dPdf,
				&Ld, &dWeight)) {
				if (light->IsEnvironmental()) {
					// Here sw.single is correct
					if (eye0.EyeConnect(sample,
						XYZColor(sw, Ld), 0.f, INFINITY,
						dWeight, lightBufferId,
						light->group))
						++nrContribs;
				} else {
					if (eye0.EyeConnect(sample,
						XYZColor(sw, Ld), 1.f,
						sqrtf(eye0.d2), dWeight,
						lightBufferId, light->group))
						++nrContribs;
				}
			}
		}
	}
	} // end if (maxEyeDepth > 0) direct lighting

	// Track whether a first-bounce eye ray was successfully sampled.
	// Only when this is true can we splat eye-buffer contributions and
	// use eye0.wi / eye0.d2 without reading uninitialised data.
	bool eyePathTraced = false;
	float d = INFINITY;

	// Sample eye subpath initial direction and finish vertex initialization
	const float lensU = sample.camera->IsLensBased() ? sample.imageX : sample.lensU;
	const float lensV = sample.camera->IsLensBased() ? sample.imageY : sample.lensV;

	// Eye-path tracing — only possible when maxEyeDepth > 1 (at least one
	// surface vertex) AND sampleEyeOffset samples are allocated.
	if (maxEyeDepth > 1) {
		SWCSpectrum f0;
		if (eye0.bsdf->SampleF(sw, eye0.wo, &eye0.wi, lensU, lensV, .5f,
			&f0, &eye0.pdfR, BSDF_ALL, &eye0.flags, &eye0.pdf, true)) {
		eye0.cosi = AbsDot(eye0.wi, eye0.bsdf->ng);
		eye0.rr = min(1.f, max(lightThreshold,
			f0.Filter(sw) * eye0.coso / eye0.cosi));
		eye0.rrR = min(1.f, max(eyeThreshold, f0.Filter(sw)));
		Ray ray(eye0.p, eye0.wi);
		ray.time = sample.realTime;
		sample.camera->ClampRay(ray);
		Intersection isect;
		eyePath[nEye].flux = eye0.flux * f0;

		// Trace eye subpath and do direct lighting
		const Volume *volume = eye0.bsdf->GetVolume(ray.d);
		bool scattered = eye0.bsdf->dgShading.scattered;
		for (u_int sampleIndex = 1; sampleIndex < maxEyeDepth; ++sampleIndex) {
			const float *data = sample.sampler->GetLazyValues(sample,
				sampleEyeOffset, sampleIndex);
			BidirVertex &v = eyePath[nEye];
			BidirVertex &vp = eyePath[nEye - 1];
			float spdf, spdfR;
			if (!scene.Intersect(sample, volume, scattered, ray, data[4],
				&isect, &v.bsdf, &spdfR, &spdf, &v.flux)) {
				v.flux /= spdfR;
				vector<BidirVertex> path(0);
				// Reinitalize ray origin to the previous
				// non passthrough intersection
				ray.o = vp.p;
				for (u_int lightNumber = 0; lightNumber < scene.lights.size(); ++lightNumber) {
					const Light *light = scene.lights[lightNumber].get();
					if (!light->IsEnvironmental())
						continue;
					float ePdfDirect;
					SWCSpectrum Le(v.flux);
					// No check for dAWeight > 0
					// in the case of portal, the eye path can hit
					// the light outside portals
					if (!light->Le(scene, sample, ray, &v.bsdf,
						&v.dAWeight, &ePdfDirect, &Le))
						continue;
					v.wo = -ray.d;
					v.flags = BxDFType(~BSDF_SPECULAR);
					v.p = v.bsdf->dgShading.p;
					v.coso = AbsDot(v.wo, v.bsdf->ng);
					vp.d2 = DistanceSquared(vp.p, v.p);
					// Evaluate factors for path weighting
					v.dARWeight = vp.pdfR * vp.tPdfR *
						spdfR / vp.d2;
					if (!v.bsdf->dgShading.scattered)
						v.dARWeight *= v.coso;
					v.pdf = v.bsdf->Pdf(sw, Vector(v.bsdf->dgShading.nn),
						v.wo);
					// No check for pdf > 0
					// in the case of portal, the eye path can hit
					// the light outside portals
					v.dAWeight *= lightPathStrategy->Pdf(scene,
						lightNumber) * lightRayCount;
					ePdfDirect *= lightDirectStrategy->Pdf(scene,
						lightNumber) * shadowRayCount;
					vp.dAWeight = v.pdf * v.tPdf *
						spdf / vp.d2;
					if (!vp.bsdf->dgShading.scattered)
						vp.dAWeight *= vp.cosi;
					vector<BidirVertex> path(0);
					const float w = WeightPath(eyePath,
						nEye + 1, path, 0,
						ePdfDirect, false);
					Le /= w;
					partialContribution.Add(sw, Le, light->group, 1.0f / w);
					++nrContribs;
				}
				if (nEye == 1) {
					// Remove directly visible environment
					// to allow compositing
					alpha = 0.f;
					// Tweak intersection distance for Z buffer
					eye0.d2 = INFINITY;
				}
				// End eye path tracing
				break;
			}

			// Initialize new intersection vertex
			scattered = v.bsdf->dgShading.scattered;
			v.flux /= spdfR;
			vp.tPdfR *= spdfR;
			v.tPdf *= spdf;
			v.wo = -ray.d;
			v.p = isect.dg.p;
			v.coso = AbsDot(v.wo, v.bsdf->ng);
			vp.d2 = DistanceSquared(vp.p, v.p);
			v.dARWeight = vp.pdfR * vp.tPdfR / vp.d2;
			if (!scattered)
				v.dARWeight *= v.coso;
			v.single = sw.single;
			++nEye;

			// Test intersection with a light source
			SWCSpectrum Ll(v.flux);
			BSDF *eBsdf;
			float ePdfDirect;
			if (isect.Le(sample, ray, &eBsdf, &v.dAWeight,
				&ePdfDirect, &Ll)) {
				// Reinitalize ray origin to the previous
				// non passthrough intersection
				ray.o = vp.p;
				v.flags = BxDFType(~BSDF_SPECULAR);
				v.pdf = eBsdf->Pdf(sw,
					Vector(eBsdf->dgShading.nn), v.wo,
					v.flags);
				// Evaluate factors for path weighting
				v.dAWeight *= lightPathStrategy->Pdf(scene,
					isect.arealight) * lightRayCount;
				ePdfDirect *= lightDirectStrategy->Pdf(scene,
					isect.arealight) * shadowRayCount;
				vp.dAWeight = v.pdf * v.tPdf / vp.d2;
				if (!vp.bsdf->dgShading.scattered)
					vp.dAWeight *= vp.cosi;
				vector<BidirVertex> path(0);
				const float w = WeightPath(eyePath, nEye, path,
					0, ePdfDirect, false);
				Ll /= w;
				partialContribution.Add(sw, Ll, isect.arealight->group, 1.0f / w);
				++nrContribs;
			}

			// Do direct lighting
			const float *directData = sample.sampler->GetLazyValues(sample,
				sampleDirectOffset, sampleIndex);
			for (u_int l = 0; l < directSamplingCount; ++l) {
				const u_int offset = l * (1 + shadowRayCount * 3);
				SWCSpectrum Ld;
				float dWeight, dPdf;
				float portal = directData[offset];
				const Light *directLight =
					lightDirectStrategy->SampleLight(scene,
					l, &portal, &dPdf);
				if (!directLight)
					break;
				dPdf *= shadowRayCount;
				const float lPdf = lightPathStrategy->Pdf(scene,
					directLight) * lightRayCount;
				for (u_int s = 0; s < shadowRayCount; ++s) {
					const u_int offset2 = offset + s * 3 + 1;
					if (GetDirectLight(scene, sample,
						eyePath, nEye, directLight,
						directData[offset2],
						directData[offset2 + 1],
						directData[offset2 + 2],
						lPdf, dPdf, &Ld, &dWeight)) {
						partialContribution.Add(sw, Ld, directLight->group, dWeight);
						++nrContribs;
					}
				}
			}

			// Possibly terminate path sampling
			if (nEye == maxEyeDepth)
				break;

			SWCSpectrum f;
			if (!v.bsdf->SampleF(sw, v.wo, &v.wi, data[1], data[2],
				data[3], &f, &v.pdfR, BSDF_ALL, &v.flags, &v.pdf, true))
				break;

			// Check if the scattering is a passthrough event
			if (v.flags != (BSDF_TRANSMISSION | BSDF_SPECULAR) ||
				!(v.bsdf->Pdf(sw, v.wo, v.wi, BxDFType(BSDF_TRANSMISSION | BSDF_SPECULAR)) > 0.f)) {
				vp.dAWeight = v.pdf * v.tPdf /
					vp.d2;
				if (!vp.bsdf->dgShading.scattered)
					vp.dAWeight *= vp.cosi;
				v.cosi = AbsDot(v.wi, v.bsdf->ng);
				v.rr = min(1.f, max(lightThreshold,
					f.Filter(sw) * v.coso / v.cosi));
				v.rrR = min(1.f, max(eyeThreshold, f.Filter(sw)));
				eyePath[nEye].flux = v.flux * f;
				if (nEye > rrStart) {
					if (v.rrR < data[0])
						break;
					eyePath[nEye].flux /= v.rrR;
				}
			} else {
				--nEye;
				v.flux *= f;
				vp.tPdfR *= v.pdfR;
				v.tPdf *= v.pdf;
				if (sampleIndex + 1 >= maxEyeDepth) {
					vp.rrR = 0.f;
					break;
				}
			}

			// Initialize _ray_ for next segment of path
			ray = Ray(v.p, v.wi);
			ray.time = sample.realTime;
			volume = v.bsdf->GetVolume(ray.d);
		}
		eyePathTraced = true;
		d = sqrtf(eye0.d2);
		} // end if (SampleF succeeded)
	} // end if (maxEyeDepth > 1)


	// Choose light
	for (u_int l = 0; l < pathSamplingCount; ++l) {
		// initialise a new sw, to restore the single flag
		sw.single = false;

		float component = sample.sampler->GetOneD(sample,
			lightNumOffset, l);
		float lPdf;
		const Light *light = lightPathStrategy->SampleLight(scene, l, &component, &lPdf);
		if (!light)
			break;
		lPdf *= lightRayCount;
		const u_int lightGroup = light->group;
		const float directWeight = lightDirectStrategy->Pdf(scene, light) * shadowRayCount;
		for (u_int r = 0; r < lightRayCount; ++r) {
			component = sample.sampler->GetOneD(sample,
				lightPortalOffset, l * lightRayCount + r);
			float lightPos[2];
			sample.sampler->GetTwoD(sample, lightPosOffset,
				l * lightRayCount + r, lightPos);
			SWCSpectrum Le;

			// Sample light subpath origin
			if (maxLightDepth > 0 && light->SampleL(scene, sample,
				lightPos[0], lightPos[1], component,
				&lightPath[0].bsdf,
				&lightPath[0].dAWeight, &Le)) {
				BidirVertex &light0(lightPath[0]);
				u_int nLight = 0;
				float lightDirectPdf = 0.f;
				// Initialize light vertex
				light0.p = light0.bsdf->dgShading.p;
				light0.wi = Vector(light0.bsdf->dgShading.nn);
				light0.cosi = AbsDot(light0.wi, light0.bsdf->ng);
				// Give the light point probability
				// for the weighting if the light is not delta
				light0.dAWeight *= lPdf;
				// Divide by the light selection Pdf
				// (light position is already accounted for)
				Le /= lPdf;
				light0.flux = SWCSpectrum(1.f);
				// Initialize tPdf and tPdfR in case of multiple paths
				light0.tPdf = 1.f;
				light0.tPdfR = 1.f;
				light0.single = sw.single;

				// Trick to tell subsequent functions that the light is delta
				if (light->IsDeltaLight()) {
					light0.dAWeight = -light0.dAWeight;
					light0.dARWeight = -fabsf(light0.dAWeight);
				} else {
					light0.dARWeight = fabsf(light0.dAWeight);
				}
				nLight = 1;


				// Connect eye subpath to light vertex
				// Go through all eye vertices
				if (light0.bsdf->NumComponents(BxDFType(~BSDF_SPECULAR)) != 0) {
					for (u_int j = 0; j < nEye; ++j) {
						BidirVertex &vE(eyePath[j]);
						// Compute direct lighting pdf for first light vertex
						const float directPdf = light->Pdf(vE.p,
							light0.bsdf->dgShading) *
							directWeight;
						if (vE.bsdf->NumComponents(BxDFType(~BSDF_SPECULAR)) == 0)
							continue;
						SWCSpectrum Ll(Le);
						float weight;
						// Save data modified by EvalPath
						const BxDFType eflags = vE.flags;
						const float err = vE.rr;
						const float errR = vE.rrR;
						const float edAWeight = vE.dAWeight;
						const Vector ewi(vE.wi);
						const float ed2 = vE.d2;
						bool single;
						if (EvalPath(scene, sample, eyePath,
							j + 1, lightPath, nLight,
							directPdf, false, &weight,
							&Ll, single)) {
							if (j > 0) {
								partialContribution.Add(sw, Ll, lightGroup, weight, single);
								++nrContribs;
							} else if (vE.EyeConnect(sample,
								PartialContribution::toXYZColor(sw, Ll, single),
								light->IsEnvironmental() ? 0.f : 1.f,
								light->IsEnvironmental() ? INFINITY : sqrtf(vE.d2),
								weight, lightBufferId,
								lightGroup))
								++nrContribs;
						}
						// Restore modified data
						vE.flags = eflags;
						vE.rr = err;
						vE.rrR = errR;
						vE.dAWeight = edAWeight;
						vE.wi = ewi;
						vE.d2 = ed2;
					}
				}

				// Sample light subpath initial direction and
				// finish vertex initialization if needed
				const float *data = sample.sampler->GetLazyValues(sample, sampleLightOffsets[l * lightRayCount + r], 0);
				SWCSpectrum f0;
				if (maxLightDepth > 1 && light0.bsdf->SampleF(sw, light0.wi,
					&light0.wo, data[1], data[2], data[3],
					&f0, &light0.pdf, BSDF_ALL, &light0.flags,
					&light0.pdfR)) {
					light0.coso = AbsDot(light0.wo, light0.bsdf->ng);
					light0.rrR = min(1.f, max(eyeThreshold,
						f0.Filter(sw) * light0.cosi /
						light0.coso));
					light0.rr = min(1.f, max(lightThreshold,
						f0.Filter(sw)));
					Ray ray(light0.p, light0.wo);
					ray.time = sample.realTime;
					Intersection isect;
					lightPath[nLight].flux = light0.flux * f0;

					// Trace light subpath and connect to eye subpath
					const Volume *volume = light0.bsdf->GetVolume(ray.d);
					bool scattered = light0.bsdf->dgShading.scattered;
					for (u_int sampleIndex = 1; sampleIndex < maxLightDepth; ++sampleIndex) {
						// Initialize tPDf and tPdfR for
						// multiple paths
						// Use sampleIndex instead of nLight
						// to prevent overwriting values for
						// passthrough materials
						lightPath[sampleIndex].tPdf = 1.f;
						lightPath[sampleIndex].tPdfR = 1.f;
						data = sample.sampler->GetLazyValues(sample,
							sampleLightOffsets[l * lightRayCount + r], sampleIndex);
						BidirVertex &v = lightPath[nLight];
						BidirVertex &vp = lightPath[nLight - 1];
						float spdf, spdfR;
						if (!scene.Intersect(sample, volume, scattered,
							ray, data[4], &isect, &v.bsdf, &spdf,
							&spdfR, &v.flux))
							break;
						scattered = v.bsdf->dgShading.scattered;

						// Initialize new intersection vertex
						v.wi = -ray.d;
						v.p = isect.dg.p;
						v.cosi = AbsDot(v.wi, v.bsdf->ng);
						v.tPdfR *= spdfR;
						v.flux /= spdf;
						v.single = sw.single;
						++nLight;

						vp.tPdf *= spdf;
						vp.d2 = DistanceSquared(vp.p, v.p);
						v.dAWeight = vp.pdf * vp.tPdf / vp.d2;
						if (!scattered)
							v.dAWeight *= v.cosi;
						// Compute light direct Pdf between
						// the first 2 vertices
						if (nLight == 2)
							lightDirectPdf = light->Pdf(v.p,
								vp.bsdf->dgShading) *
								directWeight;

						// Connect eye subpath to light subpath
						// Go through all eye vertices
						if (v.bsdf->NumComponents(BxDFType(~BSDF_SPECULAR)) != 0) {
							for (u_int j = 0; j < nEye; ++j) {
								BidirVertex &vE(eyePath[j]);
								// Use general direct lighting pdf otherwise
								if (vE.bsdf->NumComponents(BxDFType(~BSDF_SPECULAR)) == 0)
									continue;
								SWCSpectrum Ll(Le);
								float weight;
								// Save data modified by EvalPath
								const BxDFType eflags = vE.flags;
								const float err = vE.rr;
								const float errR = vE.rrR;
								const float edAWeight = vE.dAWeight;
								const Vector ewi(vE.wi);
								const float ed2 = vE.d2;
								bool single;
								if (EvalPath(scene,
									sample, eyePath,
									j + 1,
									lightPath,
									nLight,
									lightDirectPdf,
									false, &weight,
									&Ll, single)) {
									if (j > 0) {
										partialContribution.Add(sw, Ll, lightGroup, weight, single);
										++nrContribs;
									} else if (eye0.EyeConnect(sample,
										PartialContribution::toXYZColor(sw, Ll, single),
										1.f, sqrtf(eye0.d2),
										weight, lightBufferId,
										lightGroup))
										++nrContribs;
								}
								// Restore modified data
								vE.flags = eflags;
								vE.rr = err;
								vE.rrR = errR;
								vE.dAWeight = edAWeight;
								vE.wi = ewi;
								vE.d2 = ed2;
							}
						}

						// Possibly terminate path sampling
						if (nLight == maxLightDepth)
							break;

						SWCSpectrum f;
						if (!v.bsdf->SampleF(sw, v.wi, &v.wo,
							data[1], data[2], data[3], &f,
							&v.pdf, BSDF_ALL, &v.flags,
							&v.pdfR))
							break;

						// Check if the scattering is a passthrough event
						if (v.flags != (BSDF_TRANSMISSION | BSDF_SPECULAR) ||
							!(v.bsdf->Pdf(sw, v.wi, v.wo, BxDFType(BSDF_TRANSMISSION | BSDF_SPECULAR)) > 0.f)) {
							vp.dARWeight = v.pdfR *
								v.tPdfR / vp.d2;
							if (!vp.bsdf->dgShading.scattered)
								vp.dARWeight *= vp.coso;
							v.coso = AbsDot(v.wo, v.bsdf->ng);
							v.rrR = min(1.f,
								max(eyeThreshold,
								f.Filter(sw) * v.cosi /
								v.coso));
							v.rr = min(1.f,
								max(lightThreshold,
								f.Filter(sw)));
							lightPath[nLight].flux = v.flux * f;
							if (nLight > rrStart) {
								if (v.rr < data[0])
									break;
								lightPath[nLight].flux /= v.rr;
							}
						} else {
							--nLight;
							v.flux *= f;
							vp.tPdf *= v.pdf;
							v.tPdfR *= v.pdfR;
							if (sampleIndex + 1 >= maxLightDepth) {
								vp.rr = 0.f;
								break;
							}
						}

						// Initialize _ray_ for next segment of path
						ray = Ray(v.p, v.wo);
						ray.time = sample.realTime;
						volume = v.bsdf->GetVolume(ray.d);
					}
				}
			}
		}
	}

	if (eyePathTraced) {
		float xl, yl;
		if (!sample.camera->GetSamplePosition(eyePath[0].p, eyePath[0].wi, d, &xl, &yl))
			return nrContribs;

		partialContribution.Splat(sw, sample, xl, yl, d, alpha, eyeBufferId);
	}
	return nrContribs;
}


//------------------------------------------------------------------------------
// Integrator parsing code
//------------------------------------------------------------------------------

SurfaceIntegrator* BidirIntegrator::CreateSurfaceIntegrator(const ParamSet &params)
{
	int eyeDepth = params.FindOneInt("eyedepth", 8);
	int lightDepth = params.FindOneInt("lightdepth", 8);
	float eyeThreshold = params.FindOneFloat("eyerrthreshold", 0.f);
	float lightThreshold = params.FindOneFloat("lightrrthreshold", 0.f);
	LightsSamplingStrategy *lds = LightsSamplingStrategy::Create("lightstrategy", params);
	int shadowRay = params.FindOneInt("shadowraycount", 1);
	int lightRay = params.FindOneInt("lightraycount", 1);
	LightsSamplingStrategy *lps = LightsSamplingStrategy::Create("lightpathstrategy", params);
	bool debug = params.FindOneBool("debug", false);

	return new BidirIntegrator(max(eyeDepth, 0), max(lightDepth, 0),
		eyeThreshold, lightThreshold, lds, max(1, shadowRay),
		lps, max(1, lightRay), debug);
}

static DynamicLoader::RegisterSurfaceIntegrator<BidirIntegrator> r("bidirectional");
