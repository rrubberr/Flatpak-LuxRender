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

// densitygrid.h*
#include "lux.h"
#include "luxrays/core/color/swcspectrum.h"
#include "texture.h"
#include "geometry/raydifferential.h"
#include "paramset.h"
#include "filedata.h"
#include <algorithm>
#include <numeric>
#include <fstream>

namespace lux
{

// DensityGridTexture Declarations
class DensityGridTexture : public Texture<float> {
public:
	enum WrapMode { WRAP_REPEAT, WRAP_BLACK, WRAP_WHITE, WRAP_CLAMP };
	// DensityGridTexture Public Methods
	DensityGridTexture(int x, int y, int z, const float *d,
		enum WrapMode w, TextureMapping3D *map) :
		Texture("DensityGridTexture-" + boost::lexical_cast<string>(this)),
		nx(x), ny(y), nz(z), wrapMode(w), mapping(map) {
		density.assign(d, d + nx * ny * nz);
		dMin = *std::min_element(density.begin(), density.end());
		dMax = *std::max_element(density.begin(), density.end());
		dMean = std::accumulate(density.begin(), density.end(), 0.f) /
			density.size();
	}
	virtual ~DensityGridTexture() { delete mapping; }
	virtual float Evaluate(const SpectrumWavelengths &sw,
		const DifferentialGeometry &dg) const {
		const Point P(mapping->Map(dg));
		float x, y, z;
		int vx, vy, vz;
		switch (wrapMode) {
		case WRAP_REPEAT:
			x = P.x * nx;
			vx = luxrays::Floor2Int(x);
			x -= vx;
			vx = luxrays::Mod(vx, nx);
			y = P.y * ny;
			vy = luxrays::Floor2Int(y);
			y -= vy;
			vy = luxrays::Mod(vy, ny);
			z = P.z * nz;
			vz = luxrays::Floor2Int(z);
			z -= vz;
			vz = luxrays::Mod(vz, nz);
			break;
		case WRAP_BLACK:
			if (P.x < 0.f || P.x >= 1.f ||
				P.y < 0.f || P.y >= 1.f ||
				P.z < 0.f || P.z >= 1.f)
				return 0.f;
			x = P.x * nx;
			vx = luxrays::Floor2Int(x);
			x -= vx;
			y = P.y * ny;
			vy = luxrays::Floor2Int(y);
			y -= vy;
			z = P.z * nz;
			vz = luxrays::Floor2Int(z);
			z -= vz;
			break;
		case WRAP_WHITE:
			if (P.x < 0.f || P.x >= 1.f ||
				P.y < 0.f || P.y >= 1.f ||
				P.z < 0.f || P.z >= 1.f)
				return 1.f;
			x = P.x * nx;
			vx = luxrays::Floor2Int(x);
			x -= vx;
			y = P.y * ny;
			vy = luxrays::Floor2Int(y);
			y -= vy;
			z = P.z * nz;
			vz = luxrays::Floor2Int(z);
			z -= vz;
			break;
		case WRAP_CLAMP:
			x = luxrays::Clamp(P.x, 0.f, 1.f) * nx;
			vx = min(luxrays::Floor2Int(x), nx - 1);
			x -= vx;
			y = luxrays::Clamp(P.y, 0.f, 1.f) * ny;
			vy = min(luxrays::Floor2Int(P.y * ny), ny - 1);
			y -= vy;
			z = luxrays::Clamp(P.z, 0.f, 1.f) * nz;
			vz = min(luxrays::Floor2Int(P.z * nz), nz - 1);
			z -= vz;
			break;
		default:
			return 0.f;
		}
		// Trilinear interpolation of the grid element
		return luxrays::Lerp(z,
			luxrays::Lerp(y, luxrays::Lerp(x, D(vx, vy, vz), D(vx + 1, vy, vz)),
			luxrays::Lerp(x, D(vx, vy + 1, vz), D(vx + 1, vy + 1, vz))),
			luxrays::Lerp(y, luxrays::Lerp(x, D(vx, vy, vz + 1), D(vx + 1, vy, vz + 1)),
			luxrays::Lerp(x, D(vx, vy + 1, vz + 1), D(vx + 1, vy + 1, vz + 1))));
	}
	virtual float Y() const { return dMean; }
	virtual void GetDuv(const SpectrumWavelengths &sw,
		const DifferentialGeometry &dg, float delta,
		float *du, float *dv) const {
		//FIXME: Generic derivative computation, replace with exact
		DifferentialGeometry dgTemp = dg;
		// Calculate bump map value at intersection point
		const float base = Evaluate(sw, dg);

		// Compute offset positions and evaluate displacement texture
		const Point origP(dgTemp.p);
		const Normal origN(dgTemp.nn);
		const float origU = dgTemp.u;

		// Shift _dgTemp_ _du_ in the $u$ direction and calculate value
		const float uu = delta / dgTemp.dpdu.Length();
		dgTemp.p += uu * dgTemp.dpdu;
		dgTemp.u += uu;
		dgTemp.nn = Normalize(origN + uu * dgTemp.dndu);
		*du = (Evaluate(sw, dgTemp) - base) / uu;

		// Shift _dgTemp_ _dv_ in the $v$ direction and calculate value
		const float vv = delta / dgTemp.dpdv.Length();
		dgTemp.p = origP + vv * dgTemp.dpdv;
		dgTemp.u = origU;
		dgTemp.v += vv;
		dgTemp.nn = Normalize(origN + vv * dgTemp.dndv);
		*dv = (Evaluate(sw, dgTemp) - base) / vv;
	}
	virtual void GetMinMaxFloat(float *minValue, float *maxValue) const {
		*minValue = dMin;
		*maxValue = dMax;
	}

	const TextureMapping3D *GetTextureMapping3D() const { return mapping; }

	static Texture<float> * CreateFloatTexture(const Transform &tex2world, const ParamSet &tp);
private:
	float D(int x, int y, int z) const {
		return density[((luxrays::Clamp(z, 0, nz - 1) * ny) + luxrays::Clamp(y, 0, ny - 1)) * nx + luxrays::Clamp(x, 0, nx - 1)];
	}
	// DensityGridTexture Private Data
	int nx, ny, nz;
	enum WrapMode wrapMode;
	vector<float> density;
	TextureMapping3D *mapping;
	float dMin, dMax, dMean;
};


// DensityGridTexture Method Definitions
inline Texture<float> * DensityGridTexture::CreateFloatTexture(const Transform &tex2world,
	const ParamSet &tp)
{
	int nx = tp.FindOneInt("nx", 1);
	int ny = tp.FindOneInt("ny", 1);
	int nz = tp.FindOneInt("nz", 1);

	// Attempt decode of embedded data (base64+zlib in "filename_data"),
	// mirroring tabulateddata. No-op when no embedded data is present.
	FileData::decode(tp, "filename");

	// Read density values: either from an external .dg file (filename)
	// or inlined in the paramset (density)
	vector<float> fileData;
	const float *data = NULL;
	u_int nItems = 0;

	const string filename = AdjustFilename(tp.FindOneString("filename", ""));
	if (!filename.empty()) {
		std::ifstream fs(filename.c_str(), std::ios::in | std::ios::binary);
		if (!fs.is_open()) {
			LOG(LUX_ERROR, LUX_BADFILE) << "Unable to open density grid file '" << filename << "'";
			return NULL;
		}

		// Parse header: "DG" magic (2), version (1), nx/ny/nz (4 each),
		// channel name (16, null-padded)
		char magic[2];
		unsigned char version;
		unsigned int fnx, fny, fnz;
		char channel[16];
		fs.read(magic, 2);
		fs.read(reinterpret_cast<char*>(&version), 1);
		fs.read(reinterpret_cast<char*>(&fnx), 4);
		fs.read(reinterpret_cast<char*>(&fny), 4);
		fs.read(reinterpret_cast<char*>(&fnz), 4);
		fs.read(channel, 16);
		(void)version; // consumed to advance the stream; not yet used
		(void)channel; // consumed to advance the stream; not yet used
		if (!fs.good() || magic[0] != 'D' || magic[1] != 'G') {
			LOG(LUX_ERROR, LUX_BADFILE) << "Invalid density grid file header in '" << filename << "'";
			return NULL;
		}

		// Prefer the dimensions declared in the file header
		nx = fnx; ny = fny; nz = fnz;
		nItems = static_cast<u_int>(nx * ny * nz);
		fileData.resize(nItems);
		fs.read(reinterpret_cast<char*>(&fileData[0]),
			static_cast<std::streamsize>(nItems * sizeof(float)));
		if (!fs.good() || fileData.size() != nItems) {
			LOG(LUX_ERROR, LUX_BADFILE) << "Unable to read density data from '" << filename << "'";
			return NULL;
		}
		data = &fileData[0];
		LOG(LUX_DEBUG, LUX_NOERROR) << "Read " << nItems << " density values from '" << filename << "'";
	} else {
		data = tp.FindFloat("density", &nItems);
		if (!data) {
			LOG(LUX_ERROR, LUX_MISSINGDATA) << "No \"density\" values provided for density grid?";
			return NULL;
		}
	}

	if (nItems != static_cast<u_int>(nx * ny * nz)) {
		LOG(LUX_ERROR, LUX_CONSISTENCY) <<
			"DensityGrid has " << nItems <<
			" density values but nx*ny*nz = " << nx * ny * nz;
		return NULL;
	}
	// Read wrap mode
	enum WrapMode wrapMode = WRAP_REPEAT;
	string wrapString = tp.FindOneString("wrap", "repeat");
	if (wrapString == "repeat")
		wrapMode = WRAP_REPEAT;
	else if (wrapString == "clamp")
		wrapMode = WRAP_CLAMP;
	else if (wrapString == "black")
		wrapMode = WRAP_BLACK;
	else if (wrapString == "white")
		wrapMode = WRAP_WHITE;
	// Read mapping coordinates
	TextureMapping3D *imap = TextureMapping3D::Create(tex2world, tp);
	return new DensityGridTexture(nx, ny, nz, data, wrapMode, imap);
}

}//namespace lux

