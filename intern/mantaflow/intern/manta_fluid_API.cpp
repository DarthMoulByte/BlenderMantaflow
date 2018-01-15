/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Sebastian Barschkis (sebbas)
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file mantaflow/intern/manta_smoke_API.cpp
 *  \ingroup mantaflow
 */

#include <cmath>

#include "FLUID.h"
#include "manta_fluid_API.h"

extern "C" FLUID *smoke_init(int *res, struct SmokeModifierData *smd)
{
	FLUID *smoke = new FLUID(res, smd);
	return smoke;
}

extern "C" void smoke_free(FLUID *smoke)
{
	delete smoke;
	smoke = NULL;
}

extern "C" size_t smoke_get_index(int x, int max_x, int y, int max_y, int z /*, int max_z */)
{
	return x + y * max_x + z * max_x*max_y;
}

extern "C" size_t smoke_get_index2d(int x, int max_x, int y /*, int max_y, int z, int max_z */)
{
	return x + y * max_x;
}

extern "C" void smoke_manta_export(FLUID* smoke, SmokeModifierData *smd)
{
	if (!smoke || !smd) return;
	smoke->exportSmokeScript(smd);
	smoke->exportSmokeData(smd);
}

extern "C" void smoke_step(FLUID *fluid, int framenr)
{
	fluid->step(framenr);
	fluid->updatePointers();
	if (fluid->usingHighRes())
		fluid->updatePointersHigh();
}

static void data_dissolve(float *density, float *heat, float *r, float *g, float *b, int total_cells, int speed, int log)
{
	if (log) {
		/* max density/speed = dydx */
		float fac = 1.0f - (1.0f / (float)speed);

		for(size_t i = 0; i < total_cells; i++)
		{
			/* density */
			density[i] *= fac;

			/* heat */
			if (heat) {
				heat[i] *= fac;
			}

			/* color */
			if (r) {
				r[i] *= fac;
				g[i] *= fac;
				b[i] *= fac;
			}
		}
	}
	else // linear falloff
	{
		/* max density/speed = dydx */
		float dydx = 1.0f / (float)speed;

		for(size_t i = 0; i < total_cells; i++)
		{
			float d = density[i];
			/* density */
			density[i] -= dydx;
			if (density[i] < 0.0f)
				density[i] = 0.0f;

			/* heat */
			if (heat) {
				if      (fabs(heat[i]) < dydx) heat[i] = 0.0f;
				else if (heat[i] > 0.0f) heat[i] -= dydx;
				else if (heat[i] < 0.0f) heat[i] += dydx;
			}

			/* color */
			if (r && d) {
				r[i] *= (density[i]/d);
				g[i] *= (density[i]/d);
				b[i] *= (density[i]/d);
			}
		}
	}
}

extern "C" void smoke_dissolve(FLUID *smoke, int speed, int log)
{
	data_dissolve(smoke->getDensity(), smoke->getHeat(), smoke->getColorR(), smoke->getColorG(), smoke->getColorB(), smoke->getTotalCells(), speed, log);
}

extern "C" void smoke_dissolve_wavelet(FLUID *smoke, int speed, int log)
{
	data_dissolve(smoke->getDensityHigh(), 0, smoke->getColorRHigh(), smoke->getColorGHigh(), smoke->getColorBHigh(), smoke->getTotalCellsHigh(), speed, log);
}

extern "C" void smoke_export(FLUID *smoke, float *dt, float *dx, float **dens, float **react, float **flame, float **fuel, float **heat, 
							 float **vx, float **vy, float **vz, float **r, float **g, float **b, int **obstacle)
{
	if (dens)
		*dens = smoke->getDensity();
	if (fuel)
		*fuel = smoke->getFuel();
	if (react)
		*react = smoke->getReact();
	if (flame)
		*flame = smoke->getFlame();
	if (heat)
		*heat = smoke->getHeat();
	*vx = smoke->getVelocityX();
	*vy = smoke->getVelocityY();
	*vz = smoke->getVelocityZ();
	if (r)
		*r = smoke->getColorR();
	if (g)
		*g = smoke->getColorG();
	if (b)
		*b = smoke->getColorB();
	*obstacle = smoke->getObstacle();
	*dt = 1; //dummy value, not needed for smoke
	*dx = 1; //dummy value, not needed for smoke
}

extern "C" void liquid_export(FLUID *liquid, float **phi, float **pp, float **pvel, float **ppSnd, float **pvelSnd, float **plifeSnd)
{
	if (phi)
		*phi = liquid->getPhi();
	if (pp)
		*pp = liquid->getFlipParticleData();
	if (pvel)
		*pvel = liquid->getFlipParticleVelocity();
	if (ppSnd)
		*ppSnd = liquid->getSndParticleData();
	if (pvelSnd)
		*pvelSnd = liquid->getSndParticleVelocity();
	if (plifeSnd)
		*plifeSnd = liquid->getSndParticleLife();
}

extern "C" void smoke_turbulence_export(FLUID *smoke, float **dens, float **react, float **flame, float **fuel,
                                        float **r, float **g, float **b , float **tcu, float **tcv, float **tcw, float **tcu2, float **tcv2, float **tcw2)
{
	if (!smoke && !(smoke->usingHighRes()))
		return;

	*dens = smoke->getDensityHigh();
	if (fuel)
		*fuel = smoke->getFuelHigh();
	if (react)
		*react = smoke->getReactHigh();
	if (flame)
		*flame = smoke->getFlameHigh();
	if (r)
		*r = smoke->getColorRHigh();
	if (g)
		*g = smoke->getColorGHigh();
	if (b)
		*b = smoke->getColorBHigh();
	*tcu = smoke->getTextureU();
	*tcv = smoke->getTextureV();
	*tcw = smoke->getTextureW();
	
	*tcu2 = smoke->getTextureU2();
	*tcv2 = smoke->getTextureV2();
	*tcw2 = smoke->getTextureW2();
}

extern "C" float *smoke_get_density(FLUID *smoke)
{
	return smoke->getDensity();
}

extern "C" float *smoke_get_fuel(FLUID *smoke)
{
	return smoke->getFuel();
}

extern "C" float *smoke_get_react(FLUID *smoke)
{
	return smoke->getReact();
}

extern "C" float *smoke_get_heat(FLUID *smoke)
{
	return smoke->getHeat();
}

extern "C" float *smoke_get_velocity_x(FLUID *smoke)
{
	return smoke->getVelocityX();
}

extern "C" float *smoke_get_velocity_y(FLUID *smoke)
{
	return smoke->getVelocityY();
}

extern "C" float *smoke_get_velocity_z(FLUID *smoke)
{
	return smoke->getVelocityZ();
}

extern "C" float *smoke_get_ob_velocity_x(FLUID *fluid)
{
	return fluid->getObVelocityX();
}

extern "C" float *smoke_get_ob_velocity_y(FLUID *fluid)
{
	return fluid->getObVelocityY();
}

extern "C" float *smoke_get_ob_velocity_z(FLUID *fluid)
{
	return fluid->getObVelocityZ();
}

extern "C" float *smoke_get_guide_velocity_x(FLUID *smoke)
{
	return smoke->getGuideVelocityX();
}

extern "C" float *smoke_get_guide_velocity_y(FLUID *smoke)
{
	return smoke->getGuideVelocityY();
}

extern "C" float *smoke_get_guide_velocity_z(FLUID *smoke)
{
	return smoke->getGuideVelocityZ();
}

extern "C" float *smoke_get_in_velocity_x(FLUID *fluid)
{
	return fluid->getInVelocityX();
}

extern "C" float *smoke_get_in_velocity_y(FLUID *fluid)
{
	return fluid->getInVelocityY();
}

extern "C" float *smoke_get_in_velocity_z(FLUID *fluid)
{
	return fluid->getInVelocityZ();
}

extern "C" float *smoke_get_force_x(FLUID *smoke)
{
	return smoke->getForceX();
}

extern "C" float *smoke_get_force_y(FLUID *smoke)
{
	return smoke->getForceY();
}

extern "C" float *smoke_get_force_z(FLUID *smoke)
{
	return smoke->getForceZ();
}

extern "C" float *smoke_get_flame(FLUID *smoke)
{
	return smoke->getFlame();
}

extern "C" float *smoke_get_color_r(FLUID *smoke)
{
	return smoke->getColorR();
}

extern "C" float *smoke_get_color_g(FLUID *smoke)
{
	return smoke->getColorG();
}

extern "C" float *smoke_get_color_b(FLUID *smoke)
{
	return smoke->getColorB();
}

static void get_rgba(float *r, float *g, float *b, float *a, int total_cells, float *data, int sequential)
{
	int i;
	int m = 4, i_g = 1, i_b = 2, i_a = 3;
	/* sequential data */
	if (sequential) {
		m = 1;
		i_g *= total_cells;
		i_b *= total_cells;
		i_a *= total_cells;
	}

	for (i=0; i<total_cells; i++) {
		float alpha = a[i];
		if (alpha) {
			data[i*m  ] = r[i];
			data[i*m+i_g] = g[i];
			data[i*m+i_b] = b[i];
		}
		else {
			data[i*m  ] = data[i*m+i_g] = data[i*m+i_b] = 0.0f;
		}
		data[i*m+i_a] = alpha;
	}
}

extern "C" void smoke_get_rgba(FLUID *smoke, float *data, int sequential)
{
	get_rgba(smoke->getColorR(), smoke->getColorG(), smoke->getColorB(), smoke->getDensity(), smoke->getTotalCells(), data, sequential);
}

extern "C" void smoke_turbulence_get_rgba(FLUID *smoke, float *data, int sequential)
{
	get_rgba(smoke->getColorRHigh(), smoke->getColorGHigh(), smoke->getColorBHigh(), smoke->getDensityHigh(), smoke->getTotalCellsHigh(), data, sequential);
}

/* get a single color premultiplied voxel grid */
static void get_rgba_from_density(float color[3], float *a, int total_cells, float *data, int sequential)
{
	int i;
	int m = 4, i_g = 1, i_b = 2, i_a = 3;
	/* sequential data */
	if (sequential) {
		m = 1;
		i_g *= total_cells;
		i_b *= total_cells;
		i_a *= total_cells;
	}

	for (i=0; i<total_cells; i++) {
		float alpha = a[i];
		if (alpha) {
			data[i*m  ] = color[0] * alpha;
			data[i*m+i_g] = color[1] * alpha;
			data[i*m+i_b] = color[2] * alpha;
		}
		else {
			data[i*m  ] = data[i*m+i_g] = data[i*m+i_b] = 0.0f;
		}
		data[i*m+i_a] = alpha;
	}
}

extern "C" void smoke_get_rgba_from_density(FLUID *smoke, float color[3], float *data, int sequential)
{
	get_rgba_from_density(color, smoke->getDensity(), smoke->getTotalCells(), data, sequential);
}

extern "C" void smoke_turbulence_get_rgba_from_density(FLUID *smoke, float color[3], float *data, int sequential)
{
	get_rgba_from_density(color, smoke->getDensityHigh(), smoke->getTotalCellsHigh(), data, sequential);
}

extern "C" float *smoke_turbulence_get_density(FLUID *smoke)
{
	return (smoke && smoke->usingHighRes()) ? smoke->getDensityHigh() : NULL;
}

extern "C" float *smoke_turbulence_get_fuel(FLUID *smoke)
{
	return (smoke && smoke->usingHighRes()) ? smoke->getFuelHigh() : NULL;
}

extern "C" float *smoke_turbulence_get_react(FLUID *smoke)
{
	return (smoke && smoke->usingHighRes()) ? smoke->getReactHigh() : NULL;
}

extern "C" float *smoke_turbulence_get_color_r(FLUID *smoke)
{
	return (smoke && smoke->usingHighRes()) ? smoke->getColorRHigh() : NULL;
}

extern "C" float *smoke_turbulence_get_color_g(FLUID *smoke)
{
	return (smoke && smoke->usingHighRes()) ? smoke->getColorGHigh() : NULL;
}

extern "C" float *smoke_turbulence_get_color_b(FLUID *smoke)
{
	return (smoke && smoke->usingHighRes()) ? smoke->getColorBHigh() : NULL;
}

extern "C" float *smoke_turbulence_get_flame(FLUID *smoke)
{
	return (smoke && smoke->usingHighRes()) ? smoke->getFlameHigh() : NULL;
}

extern "C" void smoke_turbulence_get_res(FLUID *smoke, int *res)
{
	if (smoke && smoke->usingHighRes()) {
		res[0] = smoke->getResXHigh();
		res[1] = smoke->getResYHigh();
		res[2] = smoke->getResZHigh();
	}
}

extern "C" int smoke_turbulence_get_cells(FLUID *smoke)
{
	int total_cells_high = smoke->getResXHigh() * smoke->getResYHigh() * smoke->getResZHigh();
	return (smoke && smoke->usingHighRes()) ? total_cells_high : 0;
}

extern "C" int *smoke_get_obstacle(FLUID *smoke)
{
	return smoke->getObstacle();
}

extern "C" void smoke_get_ob_velocity(FLUID *smoke, float **x, float **y, float **z)
{
	*x = smoke->getObVelocityX();
	*y = smoke->getObVelocityY();
	*z = smoke->getObVelocityZ();
}

extern "C" int *fluid_get_num_obstacle(FLUID *fluid)
{
	return fluid->getNumObstacle();
}

extern "C" int *fluid_get_num_guide(FLUID *fluid)
{
	return fluid->getNumGuide();
}

extern "C" int smoke_has_heat(FLUID *smoke)
{
	return (smoke->getHeat()) ? 1 : 0;
}

extern "C" int smoke_has_fuel(FLUID *smoke)
{
	return (smoke->getFuel()) ? 1 : 0;
}

extern "C" int smoke_has_colors(FLUID *smoke)
{
	return (smoke->getColorR() && smoke->getColorG() && smoke->getColorB()) ? 1 : 0;
}

extern "C" int smoke_turbulence_has_fuel(FLUID *smoke)
{
	return (smoke->getFuelHigh()) ? 1 : 0;
}

extern "C" int smoke_turbulence_has_colors(FLUID *smoke)
{
	return (smoke->getColorRHigh() && smoke->getColorGHigh() && smoke->getColorBHigh()) ? 1 : 0;
}

/* additional field initialization */
extern "C" void smoke_ensure_heat(FLUID *smoke, struct SmokeModifierData *smd)
{
	if (smoke) {
		smoke->initHeat(smd);
		smoke->updatePointers();
	}
}

extern "C" void smoke_ensure_fire(FLUID *smoke, struct SmokeModifierData *smd)
{
	if (smoke) {
		smoke->initFire(smd);
		smoke->updatePointers();
	}
	if (smoke && smoke->usingHighRes()) {
		smoke->initFireHigh(smd);
		smoke->updatePointersHigh();
	}
}

extern "C" void smoke_ensure_colors(FLUID *smoke, struct SmokeModifierData *smd)
{
	if (smoke) {
		smoke->initColors(smd);
		smoke->updatePointers();
	}
	if (smoke && smoke->usingHighRes()) {
		smoke->initColorsHigh(smd);
		smoke->updatePointersHigh();
	}
}

extern "C" void liquid_ensure_init(FLUID *smoke, struct SmokeModifierData *smd)
{
	if (smoke) {
		smoke->initLiquid(smd);
		smoke->updatePointers();
	}
}

extern "C" void fluid_ensure_sndparts(FLUID *fluid, struct SmokeModifierData *smd)
{
	if (fluid) {
		fluid->initSndParts(smd);
		fluid->updatePointers();
	}
}

extern "C" void fluid_ensure_obstacle(FLUID *fluid, struct SmokeModifierData *smd)
{
	if (fluid) {
		fluid->initObstacle(smd);
		fluid->updatePointers();
	}
}

extern "C" void fluid_ensure_guiding(FLUID *fluid, struct SmokeModifierData *smd)
{
	if (fluid) {
		fluid->initGuiding(smd);
		fluid->updatePointers();
	}
}

extern "C" void fluid_ensure_invelocity(FLUID *fluid, struct SmokeModifierData *smd)
{
	if (fluid) {
		fluid->initInVelocity(smd);
		fluid->updatePointers();
	}
}

extern "C" float *fluid_get_phiguidein(FLUID *fluid)
{
	return fluid->getPhiGuideIn();
}

extern "C" float *liquid_get_phiin(FLUID *liquid)
{
	return liquid->getPhiIn();
}

extern "C" float *liquid_get_phiobsin(FLUID *liquid)
{
	return liquid->getPhiObsIn();
}

extern "C" float *liquid_get_phioutin(FLUID *liquid)
{
	return liquid->getPhiOutIn();
}

extern "C" void liquid_save_mesh(FLUID *liquid, char *filename)
{
	if (liquid) {
		liquid->saveMesh(filename);
	}
}

extern "C" void liquid_save_particles(FLUID *liquid, char *filename)
{
	if (liquid) {
		liquid->saveParticles(filename);
	}
}

extern "C" void liquid_save_particle_velocities(FLUID *liquid, char *filename)
{
	if (liquid) {
		liquid->saveParticleVelocities(filename);
	}
}

extern "C" void liquid_save_mesh_high(FLUID *liquid, char *filename)
{
	if (liquid) {
		liquid->saveMeshHigh(filename);
	}
}

extern "C" void liquid_save_data(FLUID *liquid, char *pathname)
{
	if (liquid) {
		liquid->saveLiquidData(pathname);
	}
}

extern "C" void liquid_save_data_high(FLUID *liquid, char *pathname)
{
	if (liquid) {
		liquid->saveLiquidDataHigh(pathname);
	}
}

extern "C" void liquid_load_data(FLUID *liquid, char *pathname)
{
	if (liquid) {
		liquid->loadLiquidData(pathname);
	}
}

extern "C" void liquid_load_data_high(FLUID *liquid, char *pathname)
{
	if (liquid) {
		liquid->loadLiquidDataHigh(pathname);
	}
}

extern "C" int liquid_get_num_verts(FLUID *liquid)
{
	return liquid->getNumVertices();
}

extern "C" int liquid_get_num_normals(FLUID *liquid)
{
	return liquid->getNumNormals();
}

extern "C" int liquid_get_num_triangles(FLUID *liquid)
{
	return liquid->getNumTriangles();
}

extern "C" float liquid_get_vertex_x_at(FLUID *liquid, int i)
{
	return liquid->getVertexXAt(i);
}

extern "C" float liquid_get_vertex_y_at(FLUID *liquid, int i)
{
	return liquid->getVertexYAt(i);
}

extern "C" float liquid_get_vertex_z_at(FLUID *liquid, int i)
{
	return liquid->getVertexZAt(i);
}

extern "C" float liquid_get_normal_x_at(FLUID *liquid, int i)
{
	return liquid->getNormalXAt(i);
}

extern "C" float liquid_get_normal_y_at(FLUID *liquid, int i)
{
	return liquid->getNormalYAt(i);
}

extern "C" float liquid_get_normal_z_at(FLUID *liquid, int i)
{
	return liquid->getNormalZAt(i);
}

extern "C" float liquid_get_triangle_x_at(FLUID *liquid, int i)
{
	return liquid->getTriangleXAt(i);
}

extern "C" float liquid_get_triangle_y_at(FLUID *liquid, int i)
{
	return liquid->getTriangleYAt(i);
}

extern "C" float liquid_get_triangle_z_at(FLUID *liquid, int i)
{
	return liquid->getTriangleZAt(i);
}

extern "C" int liquid_get_num_flip_particles(FLUID *liquid)
{
	return liquid->getNumFlipParticles();
}

extern "C" int liquid_get_num_snd_particles(FLUID *liquid)
{
	return liquid->getNumSndParticles();
}

extern "C" int liquid_get_flip_particle_flag_at(FLUID *liquid, int i)
{
	return liquid->getFlipParticleFlagAt(i);
}

extern "C" float liquid_get_flip_particle_position_x_at(FLUID *liquid, int i)
{
	return liquid->getFlipParticlePositionXAt(i);
}

extern "C" float liquid_get_flip_particle_position_y_at(FLUID *liquid, int i)
{
	return liquid->getFlipParticlePositionYAt(i);
}

extern "C" float liquid_get_flip_particle_position_z_at(FLUID *liquid, int i)
{
	return liquid->getFlipParticlePositionZAt(i);
}

extern "C" float liquid_get_flip_particle_velocity_x_at(FLUID *liquid, int i)
{
	return liquid->getFlipParticleVelocityXAt(i);
}

extern "C" float liquid_get_flip_particle_velocity_y_at(FLUID *liquid, int i)
{
	return liquid->getFlipParticleVelocityYAt(i);
}

extern "C" float liquid_get_flip_particle_velocity_z_at(FLUID *liquid, int i)
{
	return liquid->getFlipParticleVelocityZAt(i);
}

extern "C" int liquid_get_snd_particle_flag_at(FLUID *liquid, int i)
{
	return liquid->getSndParticleFlagAt(i);
}

extern "C" float liquid_get_snd_particle_position_x_at(FLUID *liquid, int i)
{
	return liquid->getSndParticlePositionXAt(i);
}

extern "C" float liquid_get_snd_particle_position_y_at(FLUID *liquid, int i)
{
	return liquid->getSndParticlePositionYAt(i);
}

extern "C" float liquid_get_snd_particle_position_z_at(FLUID *liquid, int i)
{
	return liquid->getSndParticlePositionZAt(i);
}

extern "C" float liquid_get_snd_particle_velocity_x_at(FLUID *liquid, int i)
{
	return liquid->getSndParticleVelocityXAt(i);
}

extern "C" float liquid_get_snd_particle_velocity_y_at(FLUID *liquid, int i)
{
	return liquid->getSndParticleVelocityYAt(i);
}

extern "C" float liquid_get_snd_particle_velocity_z_at(FLUID *liquid, int i)
{
	return liquid->getSndParticleVelocityZAt(i);
}

extern "C" float* liquid_get_kinetic_energy_potential(FLUID *liquid)
{
	return liquid->getKineticEnergyPotential();
}

extern "C" float* liquid_get_trapped_air_potential(FLUID *liquid)
{
	return liquid->getTrappedAirPotential();
}

extern "C" float* liquid_get_wave_crest_potential(FLUID *liquid)
{
	return liquid->getWaveCrestPotential();
}

extern "C" void liquid_update_mesh_data(FLUID *liquid, char* filename)
{
	liquid->updateMeshData(filename);
}

extern "C" void liquid_manta_export(FLUID* liquid, SmokeModifierData *smd)
{
	if (!liquid || !smd) return;
	liquid->exportLiquidScript(smd);
	liquid->exportLiquidData(smd);
}

extern "C" void liquid_set_flip_particle_data(FLUID* liquid, float* buffer, int numParts)
{
	liquid->setFlipParticleData(buffer, numParts);
}

extern "C" void liquid_set_flip_particle_velocity(FLUID* liquid, float* buffer, int numParts)
{
	liquid->setFlipParticleVelocity(buffer, numParts);
}

extern "C" void liquid_set_snd_particle_data(FLUID* liquid, float* buffer, int numParts)
{
	liquid->setSndParticleData(buffer, numParts);
}

extern "C" void liquid_set_snd_particle_velocity(FLUID* liquid, float* buffer, int numParts)
{
	liquid->setSndParticleVelocity(buffer, numParts);
}

extern "C" void liquid_set_snd_particle_life(FLUID* liquid, float* buffer, int numParts)
{
	liquid->setSndParticleLife(buffer, numParts);
}

extern "C" float *fluid_get_inflow(FLUID* fluid)
{
	return fluid->getInflow();
}

extern "C" int fluid_get_res_x(FLUID* fluid)
{
	return fluid->getResX();
}

extern "C" int fluid_get_res_y(FLUID* fluid)
{
	return fluid->getResY();
}

extern "C" int fluid_get_res_z(FLUID* fluid)
{
	return fluid->getResZ();
}
