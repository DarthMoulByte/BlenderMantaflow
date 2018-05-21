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
 * The Original Code is Copyright (C) Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Daniel Genrich
 *                 Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/smoke.c
 *  \ingroup bke
 */


/* Part of the code copied from elbeem fluid library, copyright by Nils Thuerey */

#include "MEM_guardedalloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h> /* memset */

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_kdtree.h"
#include "BLI_kdopbvh.h"
#include "BLI_task.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"
#include "BLI_voxel.h"

#include "DNA_anim_types.h"
#include "DNA_armature_types.h"
#include "DNA_constraint_types.h"
#include "DNA_customdata_types.h"
#include "DNA_lamp_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_scene_types.h"
#include "DNA_smoke_types.h"

#include "BKE_appdir.h"
#include "BKE_animsys.h"
#include "BKE_armature.h"
#include "BKE_bvhutils.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_collision.h"
#include "BKE_colortools.h"
#include "BKE_constraint.h"
#include "BKE_customdata.h"
#include "BKE_deform.h"
#include "BKE_DerivedMesh.h"
#include "BKE_global.h"
#include "BKE_effect.h"
#include "BKE_main.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_particle.h"
#include "BKE_pointcache.h"
#include "BKE_scene.h"
#include "BKE_smoke.h"
#include "BKE_texture.h"

#include "RE_shader_ext.h"

#include "GPU_glew.h"

/* UNUSED so far, may be enabled later */
/* #define USE_SMOKE_COLLISION_DM */

//#define DEBUG_TIME

#ifdef DEBUG_TIME
#	include "PIL_time.h"
#endif

#ifdef WITH_MANTA
#	include "manta_fluid_API.h"
#endif

#ifdef WITH_MANTA

static ThreadMutex object_update_lock = BLI_MUTEX_INITIALIZER;

struct Object;
struct Scene;
struct DerivedMesh;
struct SmokeModifierData;

// timestep default value for nice appearance 0.1f
#define DT_DEFAULT 0.1f

#define ADD_IF_LOWER_POS(a, b) (min_ff((a) + (b), max_ff((a), (b))))
#define ADD_IF_LOWER_NEG(a, b) (max_ff((a) + (b), min_ff((a), (b))))
#define ADD_IF_LOWER(a, b) (((b) > 0) ? ADD_IF_LOWER_POS((a), (b)) : ADD_IF_LOWER_NEG((a), (b)))

#else /* WITH_MANTA */

/* Stubs to use when smoke is disabled */
struct WTURBULENCE *smoke_turbulence_init(int *UNUSED(res), int UNUSED(amplify), int UNUSED(noisetype), const char *UNUSED(noisefile_path), int UNUSED(use_fire), int UNUSED(use_colors)) { return NULL; }
//struct FLUID_3D *smoke_init(int *UNUSED(res), float *UNUSED(dx), float *UNUSED(dtdef), int UNUSED(use_heat), int UNUSED(use_fire), int UNUSED(use_colors)) { return NULL; }
void smoke_free(struct FLUID_3D *UNUSED(fluid)) {}
float *smoke_get_density(struct FLUID_3D *UNUSED(fluid)) { return NULL; }
void smoke_turbulence_free(struct WTURBULENCE *UNUSED(wt)) {}
void smoke_initWaveletBlenderRNA(struct WTURBULENCE *UNUSED(wt), float *UNUSED(strength)) {}
void smoke_initBlenderRNA(struct FLUID_3D *UNUSED(fluid), float *UNUSED(alpha), float *UNUSED(beta), float *UNUSED(dt_factor), float *UNUSED(vorticity),
                          int *UNUSED(border_colli), float *UNUSED(burning_rate), float *UNUSED(flame_smoke), float *UNUSED(flame_smoke_color),
                          float *UNUSED(flame_vorticity), float *UNUSED(flame_ignition_temp), float *UNUSED(flame_max_temp)) {}
struct DerivedMesh *smokeModifier_do(SmokeModifierData *UNUSED(smd), Scene *UNUSED(scene), Object *UNUSED(ob), DerivedMesh *UNUSED(dm)) { return NULL; }
float smoke_get_velocity_at(struct Object *UNUSED(ob), float UNUSED(position[3]), float UNUSED(velocity[3])) { return 0.0f; }

#endif /* WITH_MANTA */

#ifdef WITH_MANTA


void smoke_reallocate_fluid(SmokeDomainSettings *sds, int res[3], int free_old)
{
	if (free_old && sds->fluid)
		smoke_free(sds->fluid);
	if (!min_iii(res[0], res[1], res[2])) {
		sds->fluid = NULL;
		return;
	}
	
	sds->fluid = smoke_init(res, sds->smd);
}

void smoke_reallocate_highres_fluid(SmokeDomainSettings *sds, float dx, int res[3])
{
	sds->res_wt[0] = res[0] * (sds->amplify + 1);
	sds->res_wt[1] = res[1] * (sds->amplify + 1);
	sds->res_wt[2] = res[2] * (sds->amplify + 1);
	sds->dx_wt = dx / (sds->amplify + 1);
}

/* convert global position to domain cell space */
static void smoke_pos_to_cell(SmokeDomainSettings *sds, float pos[3])
{
	mul_m4_v3(sds->imat, pos);
	sub_v3_v3(pos, sds->p0);
	pos[0] *= 1.0f / sds->cell_size[0];
	pos[1] *= 1.0f / sds->cell_size[1];
	pos[2] *= 1.0f / sds->cell_size[2];
}

/* set domain transformations and base resolution from object derivedmesh */
static void smoke_set_domain_from_derivedmesh(SmokeDomainSettings *sds, Object *ob, DerivedMesh *dm, bool init_resolution)
{
	size_t i;
	float min[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
	float size[3];
	MVert *verts = dm->getVertArray(dm);
	float scale = 0.0;
	int res;

	res = sds->maxres;

	// get BB of domain
	for (i = 0; i < dm->getNumVerts(dm); i++)
	{
		// min BB
		min[0] = MIN2(min[0], verts[i].co[0]);
		min[1] = MIN2(min[1], verts[i].co[1]);
		min[2] = MIN2(min[2], verts[i].co[2]);

		// max BB
		max[0] = MAX2(max[0], verts[i].co[0]);
		max[1] = MAX2(max[1], verts[i].co[1]);
		max[2] = MAX2(max[2], verts[i].co[2]);
	}

	/* set domain bounds */
	copy_v3_v3(sds->p0, min);
	copy_v3_v3(sds->p1, max);
	sds->dx = 1.0f / res;

	/* calculate domain dimensions */
	sub_v3_v3v3(size, max, min);
	if (init_resolution) {
		zero_v3_int(sds->base_res);
		copy_v3_v3(sds->cell_size, size);
	}
	/* apply object scale */
	for (i = 0; i < 3; i++) {
		size[i] = fabsf(size[i] * ob->size[i]);
	}
	copy_v3_v3(sds->global_size, size);
	copy_v3_v3(sds->dp0, min);

	invert_m4_m4(sds->imat, ob->obmat);

	// prevent crash when initializing a plane as domain
	if (!init_resolution || (size[0] < FLT_EPSILON) || (size[1] < FLT_EPSILON) || (size[2] < FLT_EPSILON))
		return;

	/* define grid resolutions from longest domain side */
	if (size[0] >= MAX2(size[1], size[2])) {
		scale = res / size[0];
		sds->scale = size[0] / fabsf(ob->size[0]);
		sds->base_res[0] = res;
		sds->base_res[1] = max_ii((int)(size[1] * scale + 0.5f), 4);
		sds->base_res[2] = max_ii((int)(size[2] * scale + 0.5f), 4);
	}
	else if (size[1] >= MAX2(size[0], size[2])) {
		scale = res / size[1];
		sds->scale = size[1] / fabsf(ob->size[1]);
		sds->base_res[0] = max_ii((int)(size[0] * scale + 0.5f), 4);
		sds->base_res[1] = res;
		sds->base_res[2] = max_ii((int)(size[2] * scale + 0.5f), 4);
	}
	else {
		scale = res / size[2];
		sds->scale = size[2] / fabsf(ob->size[2]);
		sds->base_res[0] = max_ii((int)(size[0] * scale + 0.5f), 4);
		sds->base_res[1] = max_ii((int)(size[1] * scale + 0.5f), 4);
		sds->base_res[2] = res;
	}

	/* set cell size */
	sds->cell_size[0] /= (float)sds->base_res[0];
	sds->cell_size[1] /= (float)sds->base_res[1];
	sds->cell_size[2] /= (float)sds->base_res[2];
}

static void smoke_set_domain_gravity(Scene *scene, SmokeDomainSettings *sds)
{
	float gravity[3] = {0.0f, 0.0f, -1.0f};
	float gravity_mag;

	/* use global gravity if enabled */
	if (scene->physics_settings.flag & PHYS_GLOBAL_GRAVITY) {
		copy_v3_v3(gravity, scene->physics_settings.gravity);
		/* map default value to 1.0 */
		mul_v3_fl(gravity, 1.0f / 9.810f);
		
		/* convert gravity to domain space */
		gravity_mag = len_v3(gravity);
		mul_mat3_m4_v3(sds->imat, gravity);
		normalize_v3(gravity);
		mul_v3_fl(gravity, gravity_mag);
	
		sds->gravity[0] = gravity[0];
		sds->gravity[1] = gravity[1];
		sds->gravity[2] = gravity[2];
	}
}

static int smokeModifier_init(SmokeModifierData *smd, Object *ob, Scene *scene, DerivedMesh *dm)
{
	if ((smd->type & MOD_SMOKE_TYPE_DOMAIN) && smd->domain && !smd->domain->fluid)
	{
		SmokeDomainSettings *sds = smd->domain;
		int res[3];
		/* set domain dimensions from derivedmesh */
		smoke_set_domain_from_derivedmesh(sds, ob, dm, true);
		/* set domain gravity */
		smoke_set_domain_gravity(scene, sds);
		/* reset domain values */
		zero_v3_int(sds->shift);
		zero_v3(sds->shift_f);
		add_v3_fl(sds->shift_f, 0.5f);
		zero_v3(sds->prev_loc);
		mul_m4_v3(ob->obmat, sds->prev_loc);
		copy_m4_m4(sds->obmat, ob->obmat);

		/* set resolutions */
		if (smd->domain->flags & MOD_SMOKE_ADAPTIVE_DOMAIN) {
			res[0] = res[1] = res[2] = 1; /* use minimum res for adaptive init */
		}
		else {
			VECCOPY(res, sds->base_res);
		}
		VECCOPY(sds->res, res);
		sds->total_cells = sds->res[0] * sds->res[1] * sds->res[2];
		sds->res_min[0] = sds->res_min[1] = sds->res_min[2] = 0;
		VECCOPY(sds->res_max, res);

		/* allocate fluid */
		smoke_reallocate_fluid(sds, sds->res, 0);

		smd->time = scene->r.cfra;

		/* allocate highres fluid */
		if (sds->flags & MOD_SMOKE_HIGHRES) {
			smoke_reallocate_highres_fluid(sds, sds->dx, sds->res);
		}

		return 1;
	}
	else if ((smd->type & MOD_SMOKE_TYPE_FLOW) && smd->flow)
	{
		smd->time = scene->r.cfra;

		return 1;
	}
	else if ((smd->type & MOD_SMOKE_TYPE_EFFEC))
	{
		if (!smd->effec)
		{
			smokeModifier_createType(smd);
		}

		smd->time = scene->r.cfra;

		return 1;
	}

	return 2;
}

#endif /* WITH_MANTA */

static void smokeModifier_freeDomain(SmokeModifierData *smd)
{
	if (smd->domain)
	{
		if (smd->domain->fluid)
			smoke_free(smd->domain->fluid);

		if (smd->domain->fluid_mutex)
			BLI_rw_mutex_free(smd->domain->fluid_mutex);

		if (smd->domain->effector_weights)
			MEM_freeN(smd->domain->effector_weights);
		smd->domain->effector_weights = NULL;

		BKE_ptcache_free_list(&(smd->domain->ptcaches[0]));
		smd->domain->point_cache[0] = NULL;

		if (smd->domain->coba) {
			MEM_freeN(smd->domain->coba);
		}

		MEM_freeN(smd->domain);
		smd->domain = NULL;
	}
}

static void smokeModifier_freeFlow(SmokeModifierData *smd)
{
	if (smd->flow)
	{
		if (smd->flow->dm) smd->flow->dm->release(smd->flow->dm);
		if (smd->flow->verts_old) MEM_freeN(smd->flow->verts_old);
		MEM_freeN(smd->flow);
		smd->flow = NULL;
	}
}

static void smokeModifier_freeCollision(SmokeModifierData *smd)
{
	if (smd->effec)
	{
		SmokeCollSettings *scs = smd->effec;

		if (scs->numverts)
		{
			if (scs->verts_old)
			{
				MEM_freeN(scs->verts_old);
				scs->verts_old = NULL;
			}
		}

		if (smd->effec->dm)
			smd->effec->dm->release(smd->effec->dm);
		smd->effec->dm = NULL;

		MEM_freeN(smd->effec);
		smd->effec = NULL;
	}
}

static void smokeModifier_reset_ex(struct SmokeModifierData *smd, bool need_lock)
{
	if (smd)
	{
		if (smd->domain)
		{
			if (smd->domain->fluid)
			{
				if (need_lock)
					BLI_rw_mutex_lock(smd->domain->fluid_mutex, THREAD_LOCK_WRITE);

				smoke_free(smd->domain->fluid);
				smd->domain->fluid = NULL;

				if (need_lock)
					BLI_rw_mutex_unlock(smd->domain->fluid_mutex);
			}

			smd->time = -1;
			smd->domain->total_cells = 0;
			smd->domain->active_fields = 0;
		}
		else if (smd->flow)
		{
			if (smd->flow->verts_old) MEM_freeN(smd->flow->verts_old);
			smd->flow->verts_old = NULL;
			smd->flow->numverts = 0;
		}
		else if (smd->effec)
		{
			SmokeCollSettings *scs = smd->effec;

			if (scs->numverts && scs->verts_old)
			{
				MEM_freeN(scs->verts_old);
				scs->verts_old = NULL;
			}
		}
	}
}

void smokeModifier_reset(struct SmokeModifierData *smd)
{
	smokeModifier_reset_ex(smd, true);
}

void smokeModifier_free(SmokeModifierData *smd)
{
	if (smd)
	{
		smokeModifier_freeDomain(smd);
		smokeModifier_freeFlow(smd);
		smokeModifier_freeCollision(smd);
	}
}

void smokeModifier_createType(struct SmokeModifierData *smd)
{
	if (smd)
	{
		if (smd->type & MOD_SMOKE_TYPE_DOMAIN)
		{
			if (smd->domain)
				smokeModifier_freeDomain(smd);

			smd->domain = MEM_callocN(sizeof(SmokeDomainSettings), "SmokeDomain");

			smd->domain->smd = smd;

			smd->domain->point_cache[0] = BKE_ptcache_add(&(smd->domain->ptcaches[0]));
			smd->domain->point_cache[0]->flag |= PTCACHE_DISK_CACHE;
			smd->domain->point_cache[0]->step = 1;

			/* Deprecated */
			smd->domain->point_cache[1] = NULL;
			BLI_listbase_clear(&smd->domain->ptcaches[1]);
			/* set some standard values */
			smd->domain->fluid = NULL;
			smd->domain->fluid_mutex = BLI_rw_mutex_alloc();
			smd->domain->wt = NULL;
			smd->domain->eff_group = NULL;
			smd->domain->fluid_group = NULL;
			smd->domain->coll_group = NULL;
			smd->domain->maxres = 32;
			smd->domain->amplify = 1;
			smd->domain->alpha = -0.001;
			smd->domain->beta = 0.3;
			smd->domain->time_scale = 1.0;
			smd->domain->cfl_condition = 4.0;
			smd->domain->vorticity = 0;
			smd->domain->border_collisions = 0; // open domain
			smd->domain->flags = MOD_SMOKE_DISSOLVE_LOG | MOD_SMOKE_USE_VOLUME_CACHE | MOD_SMOKE_ADAPTIVE_TIME;
			smd->domain->highres_sampling = SM_HRES_FULLSAMPLE;
			smd->domain->strength = 1.0;
			smd->domain->noise = MOD_SMOKE_NOISEWAVE;
			smd->domain->diss_speed = 5;
			smd->domain->active_fields = 0;

			smd->domain->adapt_margin = 4;
			smd->domain->adapt_res = 0;
			smd->domain->adapt_threshold = 0.02f;

			smd->domain->burning_rate = 0.75f;
			smd->domain->flame_smoke = 1.0f;
			smd->domain->flame_vorticity = 0.5f;
			smd->domain->flame_ignition = 1.5f;
			smd->domain->flame_max_temp = 3.0f;
			/* color */
			smd->domain->flame_smoke_color[0] = 0.7f;
			smd->domain->flame_smoke_color[1] = 0.7f;
			smd->domain->flame_smoke_color[2] = 0.7f;

			/* Deprecated */
			smd->domain->viewsettings = 0;
			
			smd->domain->effector_weights = BKE_add_effector_weights(NULL);
			
			smd->domain->viewport_display_mode = SM_VIEWPORT_PREVIEW;
			smd->domain->render_display_mode = SM_VIEWPORT_FINAL;
			smd->domain->type = MOD_SMOKE_DOMAIN_TYPE_GAS;
			
			smd->domain->gravity[0] = 0.0f;
			smd->domain->gravity[1] = 0.0f;
			smd->domain->gravity[2] = -1.0f;

			/* liquid */
			smd->domain->particle_randomness = 0.1f;
			smd->domain->particle_number = 2;
			smd->domain->particle_minimum = 8;
			smd->domain->particle_maximum = 16;
			smd->domain->particle_radius = 1.0f;
			smd->domain->particle_band_width = 3.0f;
			smd->domain->particle_type = 0;

			smd->domain->particle_droplet_threshold = 2.0f;
			smd->domain->particle_droplet_amount = 2.5f;
			smd->domain->particle_droplet_life = 250.0f;
			smd->domain->particle_droplet_max = 4;

			smd->domain->particle_bubble_rise = 0.5f;
			smd->domain->particle_bubble_life = 250.0f;
			smd->domain->particle_bubble_max = 2;

			smd->domain->particle_floater_amount = 0.5f;
			smd->domain->particle_floater_life = 250.0f;
			smd->domain->particle_floater_max = 2;

			smd->domain->particle_tracer_amount = 0.5f;
			smd->domain->particle_tracer_life = 250.0f;
			smd->domain->particle_tracer_max = 2;

			smd->domain->surface_tension = 0.0f;
			smd->domain->viscosity_base = 1.0f;
			smd->domain->viscosity_exponent = 6.0f;
			smd->domain->domain_size = 0.5f;

			/* secondary particles */
			smd->domain->sndparticle_tau_min_wc = 2.0;
			smd->domain->sndparticle_tau_max_wc = 8.0;
			smd->domain->sndparticle_tau_min_ta = 5.0;
			smd->domain->sndparticle_tau_max_ta = 20.0;
			smd->domain->sndparticle_tau_min_k = 1.0;
			smd->domain->sndparticle_tau_max_k = 5.0;
			smd->domain->sndparticle_k_wc = 200;
			smd->domain->sndparticle_k_ta = 40;
			smd->domain->sndparticle_k_b = 0.5;
			smd->domain->sndparticle_k_d = 0.6;
			smd->domain->sndparticle_l_min = 10.0;
			smd->domain->sndparticle_l_max = 25.0;
			smd->domain->sndparticle_boundary = SNDPARTICLE_BOUNDARY_DELETE;
			smd->domain->sndparticle_potential_resolution = SNDPARTICLE_POTENTIAL_RESOLUTION_LOW;
			smd->domain->sndparticle_potential_quality = SNDPARTICLE_POTENTIAL_QUALITY_HIGH;
			smd->domain->sndparticle_potential_grid_save = SNDPARTICLE_POTENTIAL_GRID_SAVE_LOW;
			smd->domain->sndparticle_combined_export = SNDPARTICLE_COMBINED_EXPORT_OFF;


			/* guiding */
			smd->domain->guiding_alpha = 2.0f;
			smd->domain->guiding_beta = 5;

			/*mantaflow settings*/
			smd->domain->manta_solver_res = 3;
			smd->domain->noise_pos_scale = 2.0f;
			smd->domain->noise_time_anim = 0.1f;
			BLI_make_file_string("/", smd->domain->manta_filepath, BKE_tempdir_base(), "manta_scene.py");

#ifdef WITH_OPENVDB_BLOSC
			smd->domain->openvdb_comp = VDB_COMPRESSION_BLOSC;
#else
			smd->domain->openvdb_comp = VDB_COMPRESSION_ZIP;
#endif
			smd->domain->data_depth = 0;
			smd->domain->cache_surface_format = PTCACHE_FILE_OBJECT;
			smd->domain->cache_volume_format = PTCACHE_FILE_PTCACHE;
			smd->domain->cache_frame_start = 1;
			smd->domain->cache_frame_end = 250;
			BLI_path_make_safe(smd->domain->cache_directory);
			BLI_make_file_string("/", smd->domain->cache_directory, BKE_tempdir_base(), "");
			smd->domain->cache_flag = 0;

			smd->domain->display_thickness = 1.0f;
			smd->domain->slice_method = MOD_SMOKE_SLICE_AXIS_ALIGNED;
			smd->domain->axis_slice_method = AXIS_SLICE_FULL;
			smd->domain->slice_per_voxel = 5.0f;
			smd->domain->slice_depth = 0.5f;
			smd->domain->slice_axis = 0;
			smd->domain->vector_scale = 1.0f;
			smd->domain->coba_field_remap_offset = 0.0f;
			smd->domain->coba_field_remap_slope = 1.0f;

			smd->domain->coba = NULL;
			smd->domain->coba_field = FLUID_FIELD_DENSITY;
			smd->domain->coba_field_liquid = FLUID_FIELD_PRESSURE;
		}
		else if (smd->type & MOD_SMOKE_TYPE_FLOW)
		{
			if (smd->flow)
				smokeModifier_freeFlow(smd);

			smd->flow = MEM_callocN(sizeof(SmokeFlowSettings), "SmokeFlow");

			smd->flow->smd = smd;

			/* set some standard values */
			smd->flow->density = 1.0f;
			smd->flow->fuel_amount = 1.0f;
			smd->flow->temp = 1.0f;
			smd->flow->flags = MOD_SMOKE_FLOW_ABSOLUTE | MOD_SMOKE_FLOW_USE_PART_SIZE | MOD_SMOKE_FLOW_USE_INFLOW;
			smd->flow->vel_multi = 1.0f;
			smd->flow->volume_density = 0.0f;
			smd->flow->surface_distance = 1.5f;
			smd->flow->source = MOD_SMOKE_FLOW_SOURCE_MESH;
			smd->flow->texture_size = 1.0f;
			smd->flow->particle_size = 1.0f;
			smd->flow->subframes = 0;
			smd->flow->type = MOD_SMOKE_FLOW_TYPE_SMOKE;

			smd->flow->color[0] = 0.7f;
			smd->flow->color[1] = 0.7f;
			smd->flow->color[2] = 0.7f;

			smd->flow->dm = NULL;
			smd->flow->psys = NULL;

		}
		else if (smd->type & MOD_SMOKE_TYPE_EFFEC)
		{
			if (smd->effec)
				smokeModifier_freeCollision(smd);

			smd->effec = MEM_callocN(sizeof(SmokeCollSettings), "SmokeColl");

			smd->effec->smd = smd;
			smd->effec->verts_old = NULL;
			smd->effec->numverts = 0;
			smd->effec->type = 0; // static obstacle
			smd->effec->dm = NULL;
			smd->effec->surface_distance = 0.5f;
		}
	}
}

void smokeModifier_copy(struct SmokeModifierData *smd, struct SmokeModifierData *tsmd)
{
	tsmd->type = smd->type;
	tsmd->time = smd->time;

	smokeModifier_createType(tsmd);

	if (tsmd->domain) {
		tsmd->domain->fluid_group = smd->domain->fluid_group;
		tsmd->domain->coll_group = smd->domain->coll_group;

		tsmd->domain->adapt_margin = smd->domain->adapt_margin;
		tsmd->domain->adapt_res = smd->domain->adapt_res;
		tsmd->domain->adapt_threshold = smd->domain->adapt_threshold;

		tsmd->domain->alpha = smd->domain->alpha;
		tsmd->domain->beta = smd->domain->beta;
		tsmd->domain->amplify = smd->domain->amplify;
		tsmd->domain->maxres = smd->domain->maxres;
		tsmd->domain->flags = smd->domain->flags;
		tsmd->domain->highres_sampling = smd->domain->highres_sampling;
		tsmd->domain->viewsettings = smd->domain->viewsettings;
		tsmd->domain->noise = smd->domain->noise;
		tsmd->domain->diss_speed = smd->domain->diss_speed;
		tsmd->domain->strength = smd->domain->strength;

		tsmd->domain->border_collisions = smd->domain->border_collisions;
		tsmd->domain->vorticity = smd->domain->vorticity;
		tsmd->domain->time_scale = smd->domain->time_scale;
		tsmd->domain->cfl_condition = smd->domain->cfl_condition;

		tsmd->domain->burning_rate = smd->domain->burning_rate;
		tsmd->domain->flame_smoke = smd->domain->flame_smoke;
		tsmd->domain->flame_vorticity = smd->domain->flame_vorticity;
		tsmd->domain->flame_ignition = smd->domain->flame_ignition;
		tsmd->domain->flame_max_temp = smd->domain->flame_max_temp;
		
		tsmd->domain->gravity[0] = smd->domain->gravity[0];
		tsmd->domain->gravity[1] = smd->domain->gravity[1];
		tsmd->domain->gravity[2] = smd->domain->gravity[2];

		tsmd->domain->particle_randomness = smd->domain->particle_randomness;
		tsmd->domain->particle_number = smd->domain->particle_number;
		tsmd->domain->particle_minimum = smd->domain->particle_minimum;
		tsmd->domain->particle_maximum = smd->domain->particle_maximum;
		tsmd->domain->particle_radius = smd->domain->particle_radius;
		tsmd->domain->particle_band_width = smd->domain->particle_band_width;
		tsmd->domain->particle_droplet_threshold = smd->domain->particle_droplet_threshold;
		tsmd->domain->particle_droplet_amount = smd->domain->particle_droplet_amount;
		tsmd->domain->particle_droplet_life = smd->domain->particle_droplet_life;
		tsmd->domain->particle_droplet_max = smd->domain->particle_droplet_max;
		tsmd->domain->particle_bubble_rise = smd->domain->particle_bubble_rise;
		tsmd->domain->particle_bubble_life = smd->domain->particle_bubble_life;
		tsmd->domain->particle_bubble_max = smd->domain->particle_bubble_max;
		tsmd->domain->particle_floater_amount = smd->domain->particle_floater_amount;
		tsmd->domain->particle_floater_life = smd->domain->particle_floater_life;
		tsmd->domain->particle_floater_max = smd->domain->particle_floater_max;
		tsmd->domain->particle_tracer_amount = smd->domain->particle_tracer_amount;
		tsmd->domain->particle_tracer_life = smd->domain->particle_tracer_life;
		tsmd->domain->particle_tracer_max = smd->domain->particle_tracer_max;

		tsmd->domain->sndparticle_k_b = smd->domain->sndparticle_k_b;
		tsmd->domain->sndparticle_k_d = smd->domain->sndparticle_k_d;
		tsmd->domain->sndparticle_k_ta = smd->domain->sndparticle_k_ta;
		tsmd->domain->sndparticle_k_wc = smd->domain->sndparticle_k_wc;
		tsmd->domain->sndparticle_l_max = smd->domain->sndparticle_l_max;
		tsmd->domain->sndparticle_l_min = smd->domain->sndparticle_l_min;
		tsmd->domain->sndparticle_tau_max_k = smd->domain->sndparticle_tau_max_k;
		tsmd->domain->sndparticle_tau_max_ta = smd->domain->sndparticle_tau_max_ta;
		tsmd->domain->sndparticle_tau_max_wc = smd->domain->sndparticle_tau_max_wc;
		tsmd->domain->sndparticle_tau_min_k = smd->domain->sndparticle_tau_min_k;
		tsmd->domain->sndparticle_tau_min_ta = smd->domain->sndparticle_tau_min_ta;
		tsmd->domain->sndparticle_tau_min_wc = smd->domain->sndparticle_tau_min_wc;
		tsmd->domain->sndparticle_boundary = smd->domain->sndparticle_boundary;
		tsmd->domain->sndparticle_potential_resolution = smd->domain->sndparticle_potential_resolution;
		tsmd->domain->sndparticle_potential_quality = smd->domain->sndparticle_potential_quality;
		tsmd->domain->sndparticle_potential_grid_save = smd->domain->sndparticle_potential_grid_save;
		tsmd->domain->sndparticle_combined_export = smd->domain->sndparticle_combined_export;

		tsmd->domain->surface_tension = smd->domain->surface_tension;
		tsmd->domain->viscosity_base = smd->domain->viscosity_base;
		tsmd->domain->viscosity_exponent = smd->domain->viscosity_exponent;
		tsmd->domain->domain_size = smd->domain->domain_size;

		tsmd->domain->guiding_alpha = smd->domain->guiding_alpha;
		tsmd->domain->guiding_beta = smd->domain->guiding_beta;

		tsmd->domain->manta_solver_res = smd->domain->manta_solver_res;
		tsmd->domain->noise_pos_scale = smd->domain->noise_pos_scale;
		tsmd->domain->noise_time_anim = smd->domain->noise_time_anim;
		
		tsmd->domain->viewport_display_mode = smd->domain->viewport_display_mode;
		tsmd->domain->render_display_mode = smd->domain->render_display_mode;
		tsmd->domain->type = smd->domain->type;

		copy_v3_v3(tsmd->domain->flame_smoke_color, smd->domain->flame_smoke_color);

		MEM_freeN(tsmd->domain->effector_weights);
		tsmd->domain->effector_weights = MEM_dupallocN(smd->domain->effector_weights);
		tsmd->domain->openvdb_comp = smd->domain->openvdb_comp;
		tsmd->domain->data_depth = smd->domain->data_depth;
		tsmd->domain->cache_surface_format = smd->domain->cache_surface_format;
		tsmd->domain->cache_volume_format = smd->domain->cache_volume_format;

		tsmd->domain->slice_method = smd->domain->slice_method;
		tsmd->domain->axis_slice_method = smd->domain->axis_slice_method;
		tsmd->domain->slice_per_voxel = smd->domain->slice_per_voxel;
		tsmd->domain->slice_depth = smd->domain->slice_depth;
		tsmd->domain->slice_axis = smd->domain->slice_axis;
		tsmd->domain->draw_velocity = smd->domain->draw_velocity;
		tsmd->domain->vector_draw_type = smd->domain->vector_draw_type;
		tsmd->domain->vector_scale = smd->domain->vector_scale;
		tsmd->domain->coba_field_remap_offset = smd->domain->coba_field_remap_offset;
		tsmd->domain->coba_field_remap_slope = smd->domain->coba_field_remap_slope;

		if (smd->domain->coba) {
			tsmd->domain->coba = MEM_dupallocN(smd->domain->coba);
		}
	}
	else if (tsmd->flow) {
		tsmd->flow->psys = smd->flow->psys;
		tsmd->flow->noise_texture = smd->flow->noise_texture;

		tsmd->flow->vel_multi = smd->flow->vel_multi;
		tsmd->flow->vel_normal = smd->flow->vel_normal;
		tsmd->flow->vel_random = smd->flow->vel_random;

		tsmd->flow->density = smd->flow->density;
		copy_v3_v3(tsmd->flow->color, smd->flow->color);
		tsmd->flow->fuel_amount = smd->flow->fuel_amount;
		tsmd->flow->temp = smd->flow->temp;
		tsmd->flow->volume_density = smd->flow->volume_density;
		tsmd->flow->surface_distance = smd->flow->surface_distance;
		tsmd->flow->particle_size = smd->flow->particle_size;
		tsmd->flow->subframes = smd->flow->subframes;

		tsmd->flow->texture_size = smd->flow->texture_size;
		tsmd->flow->texture_offset = smd->flow->texture_offset;
		BLI_strncpy(tsmd->flow->uvlayer_name, smd->flow->uvlayer_name, sizeof(tsmd->flow->uvlayer_name));
		tsmd->flow->vgroup_density = smd->flow->vgroup_density;

		tsmd->flow->type = smd->flow->type;
		tsmd->flow->source = smd->flow->source;
		tsmd->flow->texture_type = smd->flow->texture_type;
		tsmd->flow->flags = smd->flow->flags;
	}
	else if (tsmd->effec) {
		tsmd->effec->type = smd->effec->type;
		tsmd->effec->surface_distance = smd->effec->surface_distance;
	}
}

#ifdef WITH_MANTA

// forward decleration
static void smoke_calc_transparency(SmokeDomainSettings *sds, Scene *scene);
static float calc_voxel_transp(float *result, float *input, int res[3], int *pixel, float *tRay, float correct);
static void update_mesh_distances(int index, float *mesh_distances, BVHTreeFromMesh *treeData, const float ray_start[3], float surface_thickness);

static int get_lamp(Scene *scene, float *light)
{
	Base *base_tmp = NULL;
	int found_lamp = 0;

	// try to find a lamp, preferably local
	for (base_tmp = scene->base.first; base_tmp; base_tmp = base_tmp->next) {
		if (base_tmp->object->type == OB_LAMP) {
			Lamp *la = base_tmp->object->data;

			if (la->type == LA_LOCAL) {
				copy_v3_v3(light, base_tmp->object->obmat[3]);
				return 1;
			}
			else if (!found_lamp) {
				copy_v3_v3(light, base_tmp->object->obmat[3]);
				found_lamp = 1;
			}
		}
	}

	return found_lamp;
}

/**********************************************************
 *	Obstacles
 **********************************************************/

typedef struct ObstaclesFromDMData {
	SmokeDomainSettings *sds;
	const MVert *mvert;
	const MLoop *mloop;
	const MLoopTri *looptri;
	BVHTreeFromMesh *tree;

	bool has_velocity;
	float *vert_vel;
	float *velocityX, *velocityY, *velocityZ;
	int *num_objects;
	float *distances_map;
	float surface_thickness;
} ObstaclesFromDMData;

static void obstacles_from_derivedmesh_task_cb(
        void *__restrict userdata,
        const int z,
        const ParallelRangeTLS *__restrict UNUSED(tls))
{
	ObstaclesFromDMData *data = userdata;
	SmokeDomainSettings *sds = data->sds;

	/* slightly rounded-up sqrt(3 * (0.5)^2) == max. distance of cell boundary along the diagonal */
	const float surface_distance = 2.0f; //0.867f;
	/* Note: Use larger surface distance to cover larger area with obvel. Manta will use these obvels and extrapolate them (inside and outside obstacle) */

	for (int x = sds->res_min[0]; x < sds->res_max[0]; x++) {
		for (int y = sds->res_min[1]; y < sds->res_max[1]; y++) {
			const int index = smoke_get_index(x - sds->res_min[0], sds->res[0], y - sds->res_min[1], sds->res[1], z - sds->res_min[2]);

			float ray_start[3] = {(float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f};
			BVHTreeNearest nearest = {0};
			nearest.index = -1;
			nearest.dist_sq = surface_distance * surface_distance; /* find_nearest uses squared distance */
			bool hasIncObj = false;

			/* find the nearest point on the mesh */
			if (BLI_bvhtree_find_nearest(data->tree->tree, ray_start, &nearest, data->tree->nearest_callback, data->tree) != -1) {
				const MLoopTri *lt = &data->looptri[nearest.index];
				float weights[3];
				int v1, v2, v3;

				/* calculate barycentric weights for nearest point */
				v1 = data->mloop[lt->tri[0]].v;
				v2 = data->mloop[lt->tri[1]].v;
				v3 = data->mloop[lt->tri[2]].v;
				interp_weights_tri_v3(weights, data->mvert[v1].co, data->mvert[v2].co, data->mvert[v3].co, nearest.co);

				// DG TODO
				if (data->has_velocity)
				{
					/* apply object velocity */
					float hit_vel[3];
					interp_v3_v3v3v3(hit_vel, &data->vert_vel[v1 * 3], &data->vert_vel[v2 * 3], &data->vert_vel[v3 * 3], weights);
					data->velocityX[index] += hit_vel[0];
					data->velocityY[index] += hit_vel[1];
					data->velocityZ[index] += hit_vel[2];
					// printf("adding obvel: [%f, %f, %f], dx is: %f\n", hit_vel[0], hit_vel[1], hit_vel[2], sds->dx);

					/* increase object count */
					data->num_objects[index]++;
					hasIncObj = true;
				}
			}

			/* Get distance to mesh surface from both within and outside grid (mantaflow phi grid) */
			if (data->distances_map) {
				update_mesh_distances(index, data->distances_map, data->tree, ray_start, data->surface_thickness);

				/* Ensure that num objects are also counted inside object. But dont count twice (see object inc for nearest point) */
				if (data->distances_map[index] < 0 && !hasIncObj) {
					data->num_objects[index]++;
				}
			}
		}
	}
}

static void obstacles_from_derivedmesh(
        Object *coll_ob, SmokeDomainSettings *sds, SmokeCollSettings *scs,
        float *distances_map, float *velocityX, float *velocityY, float *velocityZ, int *num_objects, float dt)
{
	if (!scs->dm) return;
	{
		DerivedMesh *dm = NULL;
		MVert *mvert = NULL;
		const MLoopTri *looptri;
		const MLoop *mloop;
		BVHTreeFromMesh treeData = {NULL};
		int numverts, i;

		float *vert_vel = NULL;
		bool has_velocity = false;

		dm = CDDM_copy(scs->dm);
		CDDM_calc_normals(dm);
		mvert = dm->getVertArray(dm);
		mloop = dm->getLoopArray(dm);
		looptri = dm->getLoopTriArray(dm);
		numverts = dm->getNumVerts(dm);

		// DG TODO
		// if (scs->type > SM_COLL_STATIC)
		// if line above is used, the code is in trouble if the object moves but is declared as "does not move"

		{
			vert_vel = MEM_callocN(sizeof(float) * numverts * 3, "smoke_obs_velocity");

			if (scs->numverts != numverts || !scs->verts_old) {
				if (scs->verts_old) MEM_freeN(scs->verts_old);
				scs->verts_old = MEM_callocN(sizeof(float) * numverts * 3, "smoke_obs_verts_old");
				scs->numverts = numverts;
			}
			else {
				has_velocity = true;
			}
		}

		/*	Transform collider vertices to
		 *   domain grid space for fast lookups */
		for (i = 0; i < numverts; i++) {
			float n[3];
			float co[3];

			/* vert pos */
			mul_m4_v3(coll_ob->obmat, mvert[i].co);
			smoke_pos_to_cell(sds, mvert[i].co);

			/* vert normal */
			normal_short_to_float_v3(n, mvert[i].no);
			mul_mat3_m4_v3(coll_ob->obmat, n);
			mul_mat3_m4_v3(sds->imat, n);
			normalize_v3(n);
			normal_float_to_short_v3(mvert[i].no, n);

			/* vert velocity */
			VECADD(co, mvert[i].co, sds->shift);
			if (has_velocity) {
				sub_v3_v3v3(&vert_vel[i * 3], co, &scs->verts_old[i * 3]);
				mul_v3_fl(&vert_vel[i * 3], sds->dx / dt);
			}
			copy_v3_v3(&scs->verts_old[i * 3], co);
		}

		if (bvhtree_from_mesh_looptri(&treeData, dm, 0.0f, 4, 6)) {
			ObstaclesFromDMData data = {
			    .sds = sds, .mvert = mvert, .mloop = mloop, .looptri = looptri,
			    .tree = &treeData, .has_velocity = has_velocity, .vert_vel = vert_vel,
			    .velocityX = velocityX, .velocityY = velocityY, .velocityZ = velocityZ,
			    .num_objects = num_objects, .distances_map = distances_map, .surface_thickness = scs->surface_distance
			};
			ParallelRangeSettings settings;
			BLI_parallel_range_settings_defaults(&settings);
			settings.scheduling_mode = TASK_SCHEDULING_DYNAMIC;
			BLI_task_parallel_range(sds->res_min[2], sds->res_max[2],
			                        &data,
			                        obstacles_from_derivedmesh_task_cb,
			                        &settings);
		}
		/* free bvh tree */
		free_bvhtree_from_mesh(&treeData);
		dm->release(dm);

		if (vert_vel) MEM_freeN(vert_vel);
	}
}

static void update_obstacleflags(SmokeDomainSettings *sds, Object **collobjs, int numcollobj)
{
	int active_fields = sds->active_fields;
	unsigned int collIndex;
	
	/* Monitor active fields based on flow settings */
	for (collIndex = 0; collIndex < numcollobj; collIndex++)
	{
		Object *collob = collobjs[collIndex];
		SmokeModifierData *smd2 = (SmokeModifierData *)modifiers_findByType(collob, eModifierType_Smoke);
		
		if ((smd2->type & MOD_SMOKE_TYPE_EFFEC) && smd2->effec) {
			SmokeCollSettings *scs = smd2->effec;
			if (!scs) break;
			if (scs->type == SM_EFFECTOR_COLLISION) {
				active_fields |= SM_ACTIVE_OBSTACLE;
			}
			if (scs->type == SM_EFFECTOR_GUIDE) {
				active_fields |= SM_ACTIVE_GUIDING;
			}
		}
	}
	/* Finally, initialize new data fields if any */
	if (active_fields & SM_ACTIVE_OBSTACLE) {
		fluid_ensure_obstacle(sds->fluid, sds->smd);
	}
	if (active_fields & SM_ACTIVE_GUIDING) {
		fluid_ensure_guiding(sds->fluid, sds->smd);
	}
	sds->active_fields = active_fields;
}

/* Animated obstacles: dx_step = ((x_new - x_old) / totalsteps) * substep */
static void update_obstacles(Scene *scene, Object *ob, SmokeDomainSettings *sds, float dt)
{
	Object **collobjs = NULL;
	unsigned int numcollobj = 0, collIndex = 0;

	collobjs = get_collisionobjects(scene, ob, sds->coll_group, &numcollobj, eModifierType_Smoke);

	/* Update all flow related flags and ensure that corresponding grids get initialized */
	update_obstacleflags(sds, collobjs, numcollobj);

	float *velx = smoke_get_ob_velocity_x(sds->fluid);
	float *vely = smoke_get_ob_velocity_y(sds->fluid);
	float *velz = smoke_get_ob_velocity_z(sds->fluid);
	float *velxGuide = smoke_get_guide_velocity_x(sds->fluid);
	float *velyGuide = smoke_get_guide_velocity_y(sds->fluid);
	float *velzGuide = smoke_get_guide_velocity_z(sds->fluid);
	float *velxOrig = smoke_get_velocity_x(sds->fluid);
	float *velyOrig = smoke_get_velocity_y(sds->fluid);
	float *velzOrig = smoke_get_velocity_z(sds->fluid);
	float *density = smoke_get_density(sds->fluid);
	float *fuel = smoke_get_fuel(sds->fluid);
	float *flame = smoke_get_flame(sds->fluid);
	float *r = smoke_get_color_r(sds->fluid);
	float *g = smoke_get_color_g(sds->fluid);
	float *b = smoke_get_color_b(sds->fluid);
	float *phiObsIn = liquid_get_phiobsin(sds->fluid);
	float *phiGuideIn = fluid_get_phiguidein(sds->fluid);
	int *obstacles = smoke_get_obstacle(sds->fluid);
	int *num_obstacles = fluid_get_num_obstacle(sds->fluid);
	int *num_guides = fluid_get_num_guide(sds->fluid);
	unsigned int z;

	/* Grid reset before writing again */
	for (z = 0; z < sds->res[0] * sds->res[1] * sds->res[2]; z++)
	{
		if (phiObsIn)
			phiObsIn[z] = 9999;
		if (phiGuideIn)
			phiGuideIn[z] = 9999;
		if (num_obstacles)
			num_obstacles[z] = 0;
		if (num_guides)
			num_guides[z] = 0;

		if (velx && vely && velz) {
			velx[z] = 0.0f;
			vely[z] = 0.0f;
			velz[z] = 0.0f;
		}
		if (velxGuide && velyGuide && velzGuide) {
			velxGuide[z] = 0.0f;
			velyGuide[z] = 0.0f;
			velzGuide[z] = 0.0f;
		}
	}

	/* Prepare grids from effector objects */
	for (collIndex = 0; collIndex < numcollobj; collIndex++)
	{
		Object *collob = collobjs[collIndex];
		SmokeModifierData *smd2 = (SmokeModifierData *)modifiers_findByType(collob, eModifierType_Smoke);

		// DG TODO: check if modifier is active?

		if ((smd2->type & MOD_SMOKE_TYPE_EFFEC) && smd2->effec) {
			SmokeCollSettings *scs = smd2->effec;
			if (scs && (scs->type == SM_EFFECTOR_COLLISION)) {
				obstacles_from_derivedmesh(collob, sds, scs, phiObsIn, velx, vely, velz, num_obstacles, dt);
			}
			if (scs && (scs->type == SM_EFFECTOR_GUIDE)) {
				obstacles_from_derivedmesh(collob, sds, scs, phiGuideIn, velxGuide, velyGuide, velzGuide, num_guides, dt);
			}
		}
	}

	if (collobjs) MEM_freeN(collobjs);

	/* obstacle cells should not contain any velocity from the smoke simulation */
	for (z = 0; z < sds->res[0] * sds->res[1] * sds->res[2]; z++)
	{
		if (obstacles[z] & 2) // mantaflow convention: FlagObstacle
		{
			velxOrig[z] = 0;
			velyOrig[z] = 0;
			velzOrig[z] = 0;
			if (density) {
				density[z] = 0;
			}
			if (fuel) {
				fuel[z] = 0;
				flame[z] = 0;
			}
			if (r) {
				r[z] = 0;
				g[z] = 0;
				b[z] = 0;
			}
		}
		/* average velocities from multiple obstacles in one cell */
		if (num_obstacles && num_obstacles[z]) {
			velx[z] /= num_obstacles[z];
			vely[z] /= num_obstacles[z];
			velz[z] /= num_obstacles[z];
		}
		/* average velocities from multiple guides in one cell */
		if (num_guides && num_guides[z]) {
			velxGuide[z] /= num_guides[z];
			velyGuide[z] /= num_guides[z];
			velzGuide[z] /= num_guides[z];
		}
	}
}

/**********************************************************
 *	Object subframe update method from dynamicpaint.c
 **********************************************************/

/* set "ignore cache" flag for all caches on this object */
static void object_cacheIgnoreClear(Object *ob, bool state)
{
	ListBase pidlist;
	PTCacheID *pid;
	BKE_ptcache_ids_from_object(&pidlist, ob, NULL, 0);

	for (pid = pidlist.first; pid; pid = pid->next) {
		if (pid->cache) {
			if (state)
				pid->cache->flag |= PTCACHE_IGNORE_CLEAR;
			else
				pid->cache->flag &= ~PTCACHE_IGNORE_CLEAR;
		}
	}

	BLI_freelistN(&pidlist);
}

static bool subframe_updateObject(Scene *scene, Object *ob, int update_mesh, int parent_recursion, float frame, bool for_render)
{
	SmokeModifierData *smd = (SmokeModifierData *)modifiers_findByType(ob, eModifierType_Smoke);
	bConstraint *con;

	/* if other is dynamic paint canvas, don't update */
	if (smd && (smd->type & MOD_SMOKE_TYPE_DOMAIN))
		return true;

	/* if object has parents, update them too */
	if (parent_recursion) {
		int recursion = parent_recursion - 1;
		bool is_domain = false;
		if (ob->parent) is_domain |= subframe_updateObject(scene, ob->parent, 0, recursion, frame, for_render);
		if (ob->track) is_domain |= subframe_updateObject(scene, ob->track, 0, recursion, frame, for_render);

		/* skip subframe if object is parented
		 *  to vertex of a dynamic paint canvas */
		if (is_domain && (ob->partype == PARVERT1 || ob->partype == PARVERT3))
			return false;

		/* also update constraint targets */
		for (con = ob->constraints.first; con; con = con->next) {
			const bConstraintTypeInfo *cti = BKE_constraint_typeinfo_get(con);
			ListBase targets = {NULL, NULL};

			if (cti && cti->get_constraint_targets) {
				bConstraintTarget *ct;
				cti->get_constraint_targets(con, &targets);
				for (ct = targets.first; ct; ct = ct->next) {
					if (ct->tar)
						subframe_updateObject(scene, ct->tar, 0, recursion, frame, for_render);
				}
				/* free temp targets */
				if (cti->flush_constraint_targets)
					cti->flush_constraint_targets(con, &targets, 0);
			}
		}
	}

	/* was originally OB_RECALC_ALL - TODO - which flags are really needed??? */
	ob->recalc |= OB_RECALC_OB | OB_RECALC_DATA | OB_RECALC_TIME;
	BKE_animsys_evaluate_animdata(scene, &ob->id, ob->adt, frame, ADT_RECALC_ANIM);
	if (update_mesh) {
		/* ignore cache clear during subframe updates
		 *  to not mess up cache validity */
		object_cacheIgnoreClear(ob, 1);
		BKE_object_handle_update(G.main->eval_ctx, scene, ob);
		object_cacheIgnoreClear(ob, 0);
	}
	else
		BKE_object_where_is_calc_time(scene, ob, frame);

	/* for curve following objects, parented curve has to be updated too */
	if (ob->type == OB_CURVE) {
		Curve *cu = ob->data;
		BKE_animsys_evaluate_animdata(scene, &cu->id, cu->adt, frame, ADT_RECALC_ANIM);
	}
	/* and armatures... */
	if (ob->type == OB_ARMATURE) {
		bArmature *arm = ob->data;
		BKE_animsys_evaluate_animdata(scene, &arm->id, arm->adt, frame, ADT_RECALC_ANIM);
		BKE_pose_where_is(scene, ob);
	}

	return false;
}

/**********************************************************
 *	Flow emission code
 **********************************************************/

typedef struct EmissionMap {
	float *influence;
	float *influence_high;
	float *velocity;
	float* distances;
	float* distances_high;
	int min[3], max[3], res[3];
	int hmin[3], hmax[3], hres[3];
	int total_cells, valid;
} EmissionMap;

static void em_boundInsert(EmissionMap *em, float point[3])
{
	int i = 0;
	if (!em->valid) {
		for (; i < 3; i++) {
			em->min[i] = (int)floor(point[i]);
			em->max[i] = (int)ceil(point[i]);
		}
		em->valid = 1;
	}
	else {
		for (; i < 3; i++) {
			if (point[i] < em->min[i]) em->min[i] = (int)floor(point[i]);
			if (point[i] > em->max[i]) em->max[i] = (int)ceil(point[i]);
		}
	}
}

static void clampBoundsInDomain(SmokeDomainSettings *sds, int min[3], int max[3], float *min_vel, float *max_vel, int margin, float dt)
{
	int i;
	for (i = 0; i < 3; i++) {
		int adapt = (sds->flags & MOD_SMOKE_ADAPTIVE_DOMAIN) ? sds->adapt_res : 0;
		/* add margin */
		min[i] -= margin;
		max[i] += margin;

		/* adapt to velocity */
		if (min_vel && min_vel[i] < 0.0f) {
			min[i] += (int)floor(min_vel[i] * dt);
		}
		if (max_vel && max_vel[i] > 0.0f) {
			max[i] += (int)ceil(max_vel[i] * dt);
		}

		/* clamp within domain max size */
		CLAMP(min[i], -adapt, sds->base_res[i] + adapt);
		CLAMP(max[i], -adapt, sds->base_res[i] + adapt);
	}
}

static void em_allocateData(EmissionMap *em, bool use_velocity, int hires_mul)
{
	int i, res[3];

	for (i = 0; i < 3; i++) {
		res[i] = em->max[i] - em->min[i];
		if (res[i] <= 0)
			return;
	}
	em->total_cells = res[0] * res[1] * res[2];
	copy_v3_v3_int(em->res, res);


	em->influence = MEM_callocN(sizeof(float) * em->total_cells, "smoke_flow_influence");
	if (use_velocity)
		em->velocity = MEM_callocN(sizeof(float) * em->total_cells * 3, "smoke_flow_velocity");
	
	em->distances = MEM_callocN(sizeof(float) * em->total_cells, "fluid_flow_distances");
	memset(em->distances, 0x7f7f7f7f, sizeof(float) * em->total_cells); // init to inf

	/* allocate high resolution map if required */
	if (hires_mul > 1) {
		int total_cells_high = em->total_cells * (hires_mul * hires_mul * hires_mul);

		for (i = 0; i < 3; i++) {
			em->hmin[i] = em->min[i] * hires_mul;
			em->hmax[i] = em->max[i] * hires_mul;
			em->hres[i] = em->res[i] * hires_mul;
		}

		em->influence_high = MEM_callocN(sizeof(float) * total_cells_high, "smoke_flow_influence_high");
		em->distances_high = MEM_callocN(sizeof(float) * total_cells_high, "fluid_flow_distances_high");
		memset(em->distances_high, 0x7f7f7f7f, sizeof(float) * total_cells_high); // init to inf
	}
	em->valid = 1;
}

static void em_freeData(EmissionMap *em)
{
	if (em->influence)
		MEM_freeN(em->influence);
	if (em->influence_high)
		MEM_freeN(em->influence_high);
	if (em->velocity)
		MEM_freeN(em->velocity);
	if (em->distances)
		MEM_freeN(em->distances);
	if (em->distances_high)
		MEM_freeN(em->distances_high);
}

static void em_combineMaps(EmissionMap *output, EmissionMap *em2, int hires_multiplier, int additive, float sample_size)
{
	int i, x, y, z;

	/* copyfill input 1 struct and clear output for new allocation */
	EmissionMap em1;
	memcpy(&em1, output, sizeof(EmissionMap));
	memset(output, 0, sizeof(EmissionMap));

	for (i = 0; i < 3; i++) {
		if (em1.valid) {
			output->min[i] = MIN2(em1.min[i], em2->min[i]);
			output->max[i] = MAX2(em1.max[i], em2->max[i]);
		}
		else {
			output->min[i] = em2->min[i];
			output->max[i] = em2->max[i];
		}
	}
	/* allocate output map */
	em_allocateData(output, (em1.velocity || em2->velocity), hires_multiplier);

	/* base resolution inputs */
	for (x = output->min[0]; x < output->max[0]; x++)
		for (y = output->min[1]; y < output->max[1]; y++)
			for (z = output->min[2]; z < output->max[2]; z++) {
				int index_out = smoke_get_index(x - output->min[0], output->res[0], y - output->min[1], output->res[1], z - output->min[2]);

				/* initialize with first input if in range */
				if (x >= em1.min[0] && x < em1.max[0] &&
					y >= em1.min[1] && y < em1.max[1] &&
					z >= em1.min[2] && z < em1.max[2]) {
					int index_in = smoke_get_index(x - em1.min[0], em1.res[0], y - em1.min[1], em1.res[1], z - em1.min[2]);

					/* values */
					output->influence[index_out] = em1.influence[index_in];
					if (output->velocity && em1.velocity) {
						copy_v3_v3(&output->velocity[index_out * 3], &em1.velocity[index_in * 3]);
					}
				}

				/* apply second input if in range */
				if (x >= em2->min[0] && x < em2->max[0] &&
					y >= em2->min[1] && y < em2->max[1] &&
					z >= em2->min[2] && z < em2->max[2]) {
					int index_in = smoke_get_index(x - em2->min[0], em2->res[0], y - em2->min[1], em2->res[1], z - em2->min[2]);

					/* values */
					if (additive) {
						output->influence[index_out] += em2->influence[index_in] * sample_size;
						output->distances[index_out] += em2->distances[index_in] * sample_size;
					}
					else {
						output->influence[index_out] = MAX2(em2->influence[index_in], output->influence[index_out]);
						output->distances[index_out] = MAX2(em2->distances[index_in], output->distances[index_out]);
					}
					if (output->velocity && em2->velocity) {
						/* last sample replaces the velocity */
						output->velocity[index_out * 3]		= ADD_IF_LOWER(output->velocity[index_out * 3], em2->velocity[index_in * 3]);
						output->velocity[index_out * 3 + 1] = ADD_IF_LOWER(output->velocity[index_out * 3 + 1], em2->velocity[index_in * 3 + 1]);
						output->velocity[index_out * 3 + 2] = ADD_IF_LOWER(output->velocity[index_out * 3 + 2], em2->velocity[index_in * 3 + 2]);
					}
				}
	} // low res loop



	/* initialize high resolution input if available */
	if (output->influence_high) {
		for (x = output->hmin[0]; x < output->hmax[0]; x++)
			for (y = output->hmin[1]; y < output->hmax[1]; y++)
				for (z = output->hmin[2]; z < output->hmax[2]; z++) {
					int index_out = smoke_get_index(x - output->hmin[0], output->hres[0], y - output->hmin[1], output->hres[1], z - output->hmin[2]);

					/* initialize with first input if in range */
					if (x >= em1.hmin[0] && x < em1.hmax[0] &&
						y >= em1.hmin[1] && y < em1.hmax[1] &&
						z >= em1.hmin[2] && z < em1.hmax[2]) {
						int index_in = smoke_get_index(x - em1.hmin[0], em1.hres[0], y - em1.hmin[1], em1.hres[1], z - em1.hmin[2]);
						/* values */
						output->influence_high[index_out] = em1.influence_high[index_in];
					}

					/* apply second input if in range */
					if (x >= em2->hmin[0] && x < em2->hmax[0] &&
						y >= em2->hmin[1] && y < em2->hmax[1] &&
						z >= em2->hmin[2] && z < em2->hmax[2]) {
						int index_in = smoke_get_index(x - em2->hmin[0], em2->hres[0], y - em2->hmin[1], em2->hres[1], z - em2->hmin[2]);

						/* values */
						if (additive) {
							output->influence_high[index_out] += em2->distances_high[index_in] * sample_size;
							output->distances_high[index_out] += em2->distances_high[index_in] * sample_size;
						}
						else {
							output->distances_high[index_out] = MAX2(em2->distances_high[index_in], output->distances_high[index_out]);
							output->distances_high[index_out] = MAX2(em2->distances_high[index_in], output->distances_high[index_out]);
						}
					}
		} // high res loop
	}

	/* free original data */
	em_freeData(&em1);
}

typedef struct EmitFromParticlesData {
	SmokeFlowSettings *sfs;
	KDTree *tree;
	int hires_multiplier;

	EmissionMap *em;
	float *particle_vel;
	float hr;

	int *min, *max, *res;

	float solid;
	float smooth;
	float hr_smooth;
} EmitFromParticlesData;

static void emit_from_particles_task_cb(
        void *__restrict userdata,
        const int z,
        const ParallelRangeTLS *__restrict UNUSED(tls))
{
	EmitFromParticlesData *data = userdata;
	SmokeFlowSettings *sfs = data->sfs;
	EmissionMap *em = data->em;
	const int hires_multiplier = data->hires_multiplier;

	for (int x = data->min[0]; x < data->max[0]; x++) {
		for (int y = data->min[1]; y < data->max[1]; y++) {
			/* take low res samples where possible */
			if (hires_multiplier <= 1 || !(x % hires_multiplier || y % hires_multiplier || z % hires_multiplier)) {
				/* get low res space coordinates */
				const int lx = x / hires_multiplier;
				const int ly = y / hires_multiplier;
				const int lz = z / hires_multiplier;

				const int index = smoke_get_index(lx - em->min[0], em->res[0], ly - em->min[1], em->res[1], lz - em->min[2]);
				const float ray_start[3] = {((float)lx) + 0.5f, ((float)ly) + 0.5f, ((float)lz) + 0.5f};

				/* find particle distance from the kdtree */
				KDTreeNearest nearest;
				const float range = data->solid + data->smooth;
				BLI_kdtree_find_nearest(data->tree, ray_start, &nearest);

				if (nearest.dist < range) {
					em->influence[index] = (nearest.dist < data->solid) ?
					                       1.0f : (1.0f - (nearest.dist - data->solid) / data->smooth);
					/* Uses particle velocity as initial velocity for smoke */
					if (sfs->flags & MOD_SMOKE_FLOW_INITVELOCITY && (sfs->psys->part->phystype != PART_PHYS_NO)) {
						VECADDFAC(&em->velocity[index * 3], &em->velocity[index * 3],
						          &data->particle_vel[nearest.index * 3], sfs->vel_multi);
					}
				}
			}

			/* take high res samples if required */
			if (hires_multiplier > 1) {
				/* get low res space coordinates */
				const float lx = ((float)x) * data->hr;
				const float ly = ((float)y) * data->hr;
				const float lz = ((float)z) * data->hr;

				const int index = smoke_get_index(
				                      x - data->min[0], data->res[0], y - data->min[1], data->res[1], z - data->min[2]);
				const float ray_start[3] = {lx + 0.5f * data->hr, ly + 0.5f * data->hr, lz + 0.5f * data->hr};

				/* find particle distance from the kdtree */
				KDTreeNearest nearest;
				const float range = data->solid + data->hr_smooth;
				BLI_kdtree_find_nearest(data->tree, ray_start, &nearest);

				if (nearest.dist < range) {
					em->influence_high[index] = (nearest.dist < data->solid) ?
					                            1.0f : (1.0f - (nearest.dist - data->solid) / data->smooth);
				}
			}

		}
	}
}

static void emit_from_particles(
        Object *flow_ob, SmokeDomainSettings *sds, SmokeFlowSettings *sfs, EmissionMap *em, Scene *scene, float dt)
{
	if (sfs && sfs->psys && sfs->psys->part && ELEM(sfs->psys->part->type, PART_EMITTER, PART_FLUID)) // is particle system selected
	{
		ParticleSimulationData sim;
		ParticleSystem *psys = sfs->psys;
		float *particle_pos;
		float *particle_vel;
		int totpart = psys->totpart, totchild;
		int p = 0;
		int valid_particles = 0;
		int bounds_margin = 1;

		/* radius based flow */
		const float solid = sfs->particle_size * 0.5f;
		const float smooth = 0.5f; /* add 0.5 cells of linear falloff to reduce aliasing */
		int hires_multiplier = 1;
		KDTree *tree = NULL;

		sim.scene = scene;
		sim.ob = flow_ob;
		sim.psys = psys;

		/* prepare curvemapping tables */
		if ((psys->part->child_flag & PART_CHILD_USE_CLUMP_CURVE) && psys->part->clumpcurve)
			curvemapping_changed_all(psys->part->clumpcurve);
		if ((psys->part->child_flag & PART_CHILD_USE_ROUGH_CURVE) && psys->part->roughcurve)
			curvemapping_changed_all(psys->part->roughcurve);
		if ((psys->part->child_flag & PART_CHILD_USE_TWIST_CURVE) && psys->part->twistcurve)
			curvemapping_changed_all(psys->part->twistcurve);

		/* initialize particle cache */
		if (psys->part->type == PART_HAIR) {
			// TODO: PART_HAIR not supported whatsoever
			totchild = 0;
		}
		else {
			totchild = psys->totchild * psys->part->disp / 100;
		}

		particle_pos = MEM_callocN(sizeof(float) * (totpart + totchild) * 3, "smoke_flow_particles");
		particle_vel = MEM_callocN(sizeof(float) * (totpart + totchild) * 3, "smoke_flow_particles");

		/* setup particle radius emission if enabled */
		if (sfs->flags & MOD_SMOKE_FLOW_USE_PART_SIZE) {
			tree = BLI_kdtree_new(psys->totpart + psys->totchild);

			/* check need for high resolution map */
			if ((sds->flags & MOD_SMOKE_HIGHRES) && (sds->highres_sampling == SM_HRES_FULLSAMPLE)) {
				hires_multiplier = sds->amplify + 1;
			}

			bounds_margin = (int)ceil(solid + smooth);
		}

		/* calculate local position for each particle */
		for (p = 0; p < totpart + totchild; p++)
		{
			ParticleKey state;
			float *pos;
			if (p < totpart) {
				if (psys->particles[p].flag & (PARS_NO_DISP | PARS_UNEXIST))
					continue;
			}
			else {
				/* handle child particle */
				ChildParticle *cpa = &psys->child[p - totpart];
				if (psys->particles[cpa->parent].flag & (PARS_NO_DISP | PARS_UNEXIST))
					continue;
			}

			state.time = BKE_scene_frame_get(scene); /* use scene time */
			if (psys_get_particle_state(&sim, p, &state, 0) == 0)
				continue;

			/* location */
			pos = &particle_pos[valid_particles * 3];
			copy_v3_v3(pos, state.co);
			smoke_pos_to_cell(sds, pos);

			/* velocity */
			copy_v3_v3(&particle_vel[valid_particles * 3], state.vel);
			mul_mat3_m4_v3(sds->imat, &particle_vel[valid_particles * 3]);

			if (sfs->flags & MOD_SMOKE_FLOW_USE_PART_SIZE) {
				BLI_kdtree_insert(tree, valid_particles, pos);
			}

			/* calculate emission map bounds */
			em_boundInsert(em, pos);
			valid_particles++;
		}

		/* set emission map */
		clampBoundsInDomain(sds, em->min, em->max, NULL, NULL, bounds_margin, dt);
		em_allocateData(em, sfs->flags & MOD_SMOKE_FLOW_INITVELOCITY, hires_multiplier);

		if (!(sfs->flags & MOD_SMOKE_FLOW_USE_PART_SIZE)) {
			for (p = 0; p < valid_particles; p++)
			{
				int cell[3];
				size_t i = 0;
				size_t index = 0;
				int badcell = 0;

				/* 1. get corresponding cell */
				cell[0] = floor(particle_pos[p * 3]) - em->min[0];
				cell[1] = floor(particle_pos[p * 3 + 1]) - em->min[1];
				cell[2] = floor(particle_pos[p * 3 + 2]) - em->min[2];
				/* check if cell is valid (in the domain boundary) */
				for (i = 0; i < 3; i++) {
					if ((cell[i] > em->res[i] - 1) || (cell[i] < 0)) {
						badcell = 1;
						break;
					}
				}
				if (badcell)
					continue;
				/* get cell index */
				index = smoke_get_index(cell[0], em->res[0], cell[1], em->res[1], cell[2]);
				/* Add influence to emission map */
				em->influence[index] = 1.0f;
				/* Uses particle velocity as initial velocity for smoke */
				if (sfs->flags & MOD_SMOKE_FLOW_INITVELOCITY && (psys->part->phystype != PART_PHYS_NO))
				{
					VECADDFAC(&em->velocity[index * 3], &em->velocity[index * 3], &particle_vel[p * 3], sfs->vel_multi);
				}
			}   // particles loop
		}
		else if (valid_particles > 0) { // MOD_SMOKE_FLOW_USE_PART_SIZE
			int min[3], max[3], res[3];
			const float hr = 1.0f / ((float)hires_multiplier);
			/* slightly adjust high res antialias smoothness based on number of divisions
			 * to allow smaller details but yet not differing too much from the low res size */
			const float hr_smooth = smooth * powf(hr, 1.0f / 3.0f);

			/* setup loop bounds */
			for (int i = 0; i < 3; i++) {
				min[i] = em->min[i] * hires_multiplier;
				max[i] = em->max[i] * hires_multiplier;
				res[i] = em->res[i] * hires_multiplier;
			}

			BLI_kdtree_balance(tree);

			EmitFromParticlesData data = {
				.sfs = sfs, .tree = tree, .hires_multiplier = hires_multiplier, .hr = hr,
			    .em = em, .particle_vel = particle_vel, .min = min, .max = max, .res = res,
			    .solid = solid, .smooth = smooth, .hr_smooth = hr_smooth,
			};

			ParallelRangeSettings settings;
			BLI_parallel_range_settings_defaults(&settings);
			settings.scheduling_mode = TASK_SCHEDULING_DYNAMIC;
			BLI_task_parallel_range(min[2], max[2],
			                        &data,
			                        emit_from_particles_task_cb,
			                        &settings);
		}

		if (sfs->flags & MOD_SMOKE_FLOW_USE_PART_SIZE) {
			BLI_kdtree_free(tree);
		}

		/* free data */
		if (particle_pos)
			MEM_freeN(particle_pos);
		if (particle_vel)
			MEM_freeN(particle_vel);
	}
}

/* Calculate map of (minimum) distances to flow/obstacle surface. Distances outside mesh are positive, inside negative */
static void update_mesh_distances(int index, float *mesh_distances, BVHTreeFromMesh *treeData, const float ray_start[3], float surface_thickness) {

	float min_dist = 9999.f;

	/* Raycasts in 26 directions (6 main axis + 12 quadrant diagonals (2D) + 8 octant diagonals (3D)) */
	float ray_dirs[26][3] = { {  1.0f, 0.0f,  0.0f }, { 0.0f,  1.0f,  0.0f }, {  0.0f,  0.0f,  1.0f },
							  { -1.0f, 0.0f,  0.0f }, { 0.0f, -1.0f,  0.0f }, {  0.0f,  0.0f, -1.0f },
							  {  1.0f, 1.0f,  0.0f }, { 1.0f, -1.0f,  0.0f }, { -1.0f,  1.0f,  0.0f }, { -1.0f, -1.0f,  0.0f },
							  {  1.0f, 0.0f,  1.0f }, { 1.0f,  0.0f, -1.0f }, { -1.0f,  0.0f,  1.0f }, { -1.0f,  0.0f, -1.0f },
							  {  0.0f, 1.0f,  1.0f }, { 0.0f,  1.0f, -1.0f }, {  0.0f, -1.0f,  1.0f }, {  0.0f, -1.0f, -1.0f },
							  {  1.0f, 1.0f,  1.0f }, { 1.0f, -1.0f,  1.0f }, { -1.0f,  1.0f,  1.0f }, { -1.0f, -1.0f,  1.0f },
							  {  1.0f, 1.0f, -1.0f }, { 1.0f, -1.0f, -1.0f }, { -1.0f,  1.0f, -1.0f }, { -1.0f, -1.0f, -1.0f } };
	size_t ray_cnt = sizeof ray_dirs / sizeof ray_dirs[0];

	/* If stays true, a point is considered to be inside the mesh */
	bool inside = true;

	/* Check all ray directions */
	for (int i = 0; i < ray_cnt; i++) {
		BVHTreeRayHit hit_tree = {0};
		hit_tree.index = -1;
		hit_tree.dist = 9999;

		normalize_v3(ray_dirs[i]);
		BLI_bvhtree_ray_cast(treeData->tree, ray_start, ray_dirs[i], 0.0f, &hit_tree, treeData->raycast_callback, treeData);

		/* Ray did not hit mesh. Current point definitely not inside mesh. */
		if (hit_tree.index == -1) { inside = false; continue; }

		/* Save new minimum hit dist */
		min_dist = MIN2(min_dist, hit_tree.dist);

		/* Ray and normal are in opposing directions. Current point definitely not inside mesh. */
		if (dot_v3v3(ray_dirs[i], hit_tree.no) < 0) { inside = false; }
	}

	/* Levelset is negative inside mesh */
	if (inside) min_dist *= (-1.0f);

	/* Update mesh distance in map */
	mesh_distances[index] = MIN2(mesh_distances[index], min_dist-surface_thickness);
}

static void sample_derivedmesh(
        SmokeFlowSettings *sfs,
        const MVert *mvert, const MLoop *mloop, const MLoopTri *mlooptri, const MLoopUV *mloopuv,
        float *influence_map, float *velocity_map, int index, const int base_res[3], float flow_center[3],
        BVHTreeFromMesh *treeData, const float ray_start[3], const float *vert_vel,
        bool has_velocity, int defgrp_index, MDeformVert *dvert,
        float x, float y, float z)
{
	float ray_dir[3] = {1.0f, 0.0f, 0.0f};
	BVHTreeRayHit hit = {0};
	BVHTreeNearest nearest = {0};

	float volume_factor = 0.0f;
	float sample_str = 0.0f;

	hit.index = -1;
	hit.dist = 9999;
	nearest.index = -1;
	nearest.dist_sq = sfs->surface_distance * sfs->surface_distance; /* find_nearest uses squared distance */

	/* Check volume collision */
	if (sfs->volume_density) {
		if (BLI_bvhtree_ray_cast(treeData->tree, ray_start, ray_dir, 0.0f, &hit, treeData->raycast_callback, treeData) != -1) {
			float dot = ray_dir[0] * hit.no[0] + ray_dir[1] * hit.no[1] + ray_dir[2] * hit.no[2];
			/*  If ray and hit face normal are facing same direction
			 *	hit point is inside a closed mesh. */
			if (dot >= 0) {
				/* Also cast a ray in opposite direction to make sure
				 * point is at least surrounded by two faces */
				negate_v3(ray_dir);
				hit.index = -1;
				hit.dist = 9999;

				BLI_bvhtree_ray_cast(treeData->tree, ray_start, ray_dir, 0.0f, &hit, treeData->raycast_callback, treeData);
				if (hit.index != -1) {
					volume_factor = sfs->volume_density;
				}
			}
		}
	}

	/* find the nearest point on the mesh */
	if (BLI_bvhtree_find_nearest(treeData->tree, ray_start, &nearest, treeData->nearest_callback, treeData) != -1) {
		float weights[3];
		int v1, v2, v3, f_index = nearest.index;
		float n1[3], n2[3], n3[3], hit_normal[3];

		/* emit from surface based on distance */
		if (sfs->surface_distance) {
			sample_str = sqrtf(nearest.dist_sq) / sfs->surface_distance;
			CLAMP(sample_str, 0.0f, 1.0f);
			sample_str = pow(1.0f - sample_str, 0.5f);
		}
		else
			sample_str = 0.0f;

		/* calculate barycentric weights for nearest point */
		v1 = mloop[mlooptri[f_index].tri[0]].v;
		v2 = mloop[mlooptri[f_index].tri[1]].v;
		v3 = mloop[mlooptri[f_index].tri[2]].v;
		interp_weights_tri_v3(weights, mvert[v1].co, mvert[v2].co, mvert[v3].co, nearest.co);

		if (sfs->flags & MOD_SMOKE_FLOW_INITVELOCITY && velocity_map) {
			/* apply normal directional velocity */
			if (sfs->vel_normal) {
				/* interpolate vertex normal vectors to get nearest point normal */
				normal_short_to_float_v3(n1, mvert[v1].no);
				normal_short_to_float_v3(n2, mvert[v2].no);
				normal_short_to_float_v3(n3, mvert[v3].no);
				interp_v3_v3v3v3(hit_normal, n1, n2, n3, weights);
				normalize_v3(hit_normal);
				/* apply normal directional and random velocity
				 * - TODO: random disabled for now since it doesnt really work well as pressure calc smoothens it out... */
				velocity_map[index * 3]   += hit_normal[0] * sfs->vel_normal * 0.25f;
				velocity_map[index * 3 + 1] += hit_normal[1] * sfs->vel_normal * 0.25f;
				velocity_map[index * 3 + 2] += hit_normal[2] * sfs->vel_normal * 0.25f;
				/* TODO: for fire emitted from mesh surface we can use
				 *  Vf = Vs + (Ps/Pf - 1)*S to model gaseous expansion from solid to fuel */
			}
			/* apply object velocity */
			if (has_velocity && sfs->vel_multi) {
				float hit_vel[3];
				interp_v3_v3v3v3(hit_vel, &vert_vel[v1 * 3], &vert_vel[v2 * 3], &vert_vel[v3 * 3], weights);
				velocity_map[index * 3]   += hit_vel[0] * sfs->vel_multi;
				velocity_map[index * 3 + 1] += hit_vel[1] * sfs->vel_multi;
				velocity_map[index * 3 + 2] += hit_vel[2] * sfs->vel_multi;
			}
		}

		/* apply vertex group influence if used */
		if (defgrp_index != -1 && dvert) {
			float weight_mask = defvert_find_weight(&dvert[v1], defgrp_index) * weights[0] +
			                    defvert_find_weight(&dvert[v2], defgrp_index) * weights[1] +
			                    defvert_find_weight(&dvert[v3], defgrp_index) * weights[2];
			sample_str *= weight_mask;
		}

		/* apply emission texture */
		if ((sfs->flags & MOD_SMOKE_FLOW_TEXTUREEMIT) && sfs->noise_texture) {
			float tex_co[3] = {0};
			TexResult texres;

			if (sfs->texture_type == MOD_SMOKE_FLOW_TEXTURE_MAP_AUTO) {
				tex_co[0] = ((x - flow_center[0]) / base_res[0]) / sfs->texture_size;
				tex_co[1] = ((y - flow_center[1]) / base_res[1]) / sfs->texture_size;
				tex_co[2] = ((z - flow_center[2]) / base_res[2] - sfs->texture_offset) / sfs->texture_size;
			}
			else if (mloopuv) {
				const float *uv[3];
				uv[0] = mloopuv[mlooptri[f_index].tri[0]].uv;
				uv[1] = mloopuv[mlooptri[f_index].tri[1]].uv;
				uv[2] = mloopuv[mlooptri[f_index].tri[2]].uv;

				interp_v2_v2v2v2(tex_co, UNPACK3(uv), weights);

				/* map between -1.0f and 1.0f */
				tex_co[0] = tex_co[0] * 2.0f - 1.0f;
				tex_co[1] = tex_co[1] * 2.0f - 1.0f;
				tex_co[2] = sfs->texture_offset;
			}
			texres.nor = NULL;
			BKE_texture_get_value(NULL, sfs->noise_texture, tex_co, &texres, false);
			sample_str *= texres.tin;
		}
	}

	/* multiply initial velocity by emitter influence */
	if (sfs->flags & MOD_SMOKE_FLOW_INITVELOCITY && velocity_map) {
		mul_v3_fl(&velocity_map[index * 3], sample_str);
	}

	/* apply final influence based on volume factor */
	influence_map[index] = MAX2(volume_factor, sample_str);
}

typedef struct EmitFromDMData {
	SmokeDomainSettings *sds;
	SmokeFlowSettings *sfs;
	const MVert *mvert;
	const MLoop *mloop;
	const MLoopTri *mlooptri;
	const MLoopUV *mloopuv;
	MDeformVert *dvert;
	int defgrp_index;

	BVHTreeFromMesh *tree;
	int hires_multiplier;
	float hr;

	EmissionMap *em;
	bool has_velocity;
	float *vert_vel;

	float *flow_center;
	int *min, *max, *res;
} EmitFromDMData;

static void emit_from_derivedmesh_task_cb(
        void *__restrict userdata,
        const int z,
        const ParallelRangeTLS *__restrict UNUSED(tls))
{
	EmitFromDMData *data = userdata;
	EmissionMap *em = data->em;
	const int hires_multiplier = data->hires_multiplier;

	for (int x = data->min[0]; x < data->max[0]; x++) {
		for (int y = data->min[1]; y < data->max[1]; y++) {
			/* take low res samples where possible */
			if (hires_multiplier <= 1 || !(x % hires_multiplier || y % hires_multiplier || z % hires_multiplier)) {
				/* get low res space coordinates */
				const int lx = x / hires_multiplier;
				const int ly = y / hires_multiplier;
				const int lz = z / hires_multiplier;

				const int index = smoke_get_index(
				                      lx - em->min[0], em->res[0], ly - em->min[1], em->res[1], lz - em->min[2]);
				const float ray_start[3] = {((float)lx) + 0.5f, ((float)ly) + 0.5f, ((float)lz) + 0.5f};

				/* Emission for smoke and fire. Result in em->influence. Also, calculate invels */
				sample_derivedmesh(
						data->sfs, data->mvert, data->mloop, data->mlooptri, data->mloopuv,
						em->influence, em->velocity, index, data->sds->base_res, data->flow_center,
						data->tree, ray_start, data->vert_vel, data->has_velocity, data->defgrp_index, data->dvert,
						(float)lx, (float)ly, (float)lz);

				/* Calculate levelset from meshes. Result in em->distances */
				update_mesh_distances(index, em->distances, data->tree, ray_start, data->sfs->surface_distance);
			}

			/* take high res samples if required */
			if (hires_multiplier > 1) {
				/* get low res space coordinates */
				const float lx = ((float)x) * data->hr;
				const float ly = ((float)y) * data->hr;
				const float lz = ((float)z) * data->hr;

				const int index = smoke_get_index(
				                      x - data->min[0], data->res[0], y - data->min[1], data->res[1], z - data->min[2]);
				const float ray_start[3] = {lx + 0.5f * data->hr, ly + 0.5f * data->hr, lz + 0.5f * data->hr};

				/* Emission for smoke and fire high. Result in em->influence_high */
				if (data->sfs->type == MOD_SMOKE_FLOW_TYPE_SMOKE || data->sfs->type == MOD_SMOKE_FLOW_TYPE_FIRE || data->sfs->type == MOD_SMOKE_FLOW_TYPE_SMOKEFIRE) {
					sample_derivedmesh(
							data->sfs, data->mvert, data->mloop, data->mlooptri, data->mloopuv,
							em->influence_high, NULL, index, data->sds->base_res, data->flow_center,
							data->tree, ray_start, data->vert_vel, data->has_velocity, data->defgrp_index, data->dvert,
							/* x,y,z needs to be always lowres */
							lx, ly, lz);
				}
			}
		}
	}
}

static void emit_from_derivedmesh(Object *flow_ob, SmokeDomainSettings *sds, SmokeFlowSettings *sfs, EmissionMap *em, float dt)
{
//	clock_t start = clock();
	if (sfs->dm) {
		DerivedMesh *dm;
		int defgrp_index = sfs->vgroup_density - 1;
		MDeformVert *dvert = NULL;
		MVert *mvert = NULL;
		MVert *mvert_orig = NULL;
		const MLoopTri *mlooptri = NULL;
		const MLoopUV *mloopuv = NULL;
		const MLoop *mloop = NULL;
		BVHTreeFromMesh treeData = {NULL};
		int numOfVerts, i;
		float flow_center[3] = {0};

		float *vert_vel = NULL;
		int has_velocity = 0;
		int min[3], max[3], res[3];
		int hires_multiplier = 1;

		/* copy derivedmesh for thread safety because we modify it,
		 * main issue is its VertArray being modified, then replaced and freed
		 */
		dm = CDDM_copy(sfs->dm);

		CDDM_calc_normals(dm);
		mvert = dm->getVertArray(dm);
		mvert_orig = dm->dupVertArray(dm);  /* copy original mvert and restore when done */
		numOfVerts = dm->getNumVerts(dm);
		dvert = dm->getVertDataArray(dm, CD_MDEFORMVERT);
		mloopuv = CustomData_get_layer_named(&dm->loopData, CD_MLOOPUV, sfs->uvlayer_name);
		mloop = dm->getLoopArray(dm);
		mlooptri = dm->getLoopTriArray(dm);

		if (sfs->flags & MOD_SMOKE_FLOW_INITVELOCITY) {
			vert_vel = MEM_callocN(sizeof(float) * numOfVerts * 3, "smoke_flow_velocity");

			if (sfs->numverts != numOfVerts || !sfs->verts_old) {
				if (sfs->verts_old) MEM_freeN(sfs->verts_old);
				sfs->verts_old = MEM_callocN(sizeof(float) * numOfVerts * 3, "smoke_flow_verts_old");
				sfs->numverts = numOfVerts;
			}
			else {
				has_velocity = true;
			}
		}

		/*	Transform dm vertices to
		 *   domain grid space for fast lookups */
		for (i = 0; i < numOfVerts; i++) {
			float n[3];

			/* vert pos */
			mul_m4_v3(flow_ob->obmat, mvert[i].co);
			smoke_pos_to_cell(sds, mvert[i].co);

			/* vert normal */
			normal_short_to_float_v3(n, mvert[i].no);
			mul_mat3_m4_v3(flow_ob->obmat, n);
			mul_mat3_m4_v3(sds->imat, n);
			normalize_v3(n);
			normal_float_to_short_v3(mvert[i].no, n);

			/* vert velocity */
			if (sfs->flags & MOD_SMOKE_FLOW_INITVELOCITY) {
				float co[3];
				VECADD(co, mvert[i].co, sds->shift);
				if (has_velocity) {
					sub_v3_v3v3(&vert_vel[i * 3], co, &sfs->verts_old[i * 3]);
					mul_v3_fl(&vert_vel[i * 3], sds->dx / dt);
				}
				copy_v3_v3(&sfs->verts_old[i * 3], co);
			}

			/* calculate emission map bounds */
			em_boundInsert(em, mvert[i].co);
		}
		mul_m4_v3(flow_ob->obmat, flow_center);
		smoke_pos_to_cell(sds, flow_center);

		/* check need for high resolution map */
		if ((sds->flags & MOD_SMOKE_HIGHRES) && (sds->highres_sampling == SM_HRES_FULLSAMPLE)) {
			hires_multiplier = sds->amplify + 1;
		}

		/* set emission map */
		clampBoundsInDomain(sds, em->min, em->max, NULL, NULL, (int)ceil(sfs->surface_distance), dt);
		em_allocateData(em, sfs->flags & MOD_SMOKE_FLOW_INITVELOCITY, hires_multiplier);

		/* setup loop bounds */
		for (i = 0; i < 3; i++) {
			min[i] = em->min[i] * hires_multiplier;
			max[i] = em->max[i] * hires_multiplier;
			res[i] = em->res[i] * hires_multiplier;
		}

		if (bvhtree_from_mesh_looptri(&treeData, dm, 0.0f, 4, 6)) {
			const float hr = 1.0f / ((float)hires_multiplier);

			EmitFromDMData data = {
				.sds = sds, .sfs = sfs,
			    .mvert = mvert, .mloop = mloop, .mlooptri = mlooptri, .mloopuv = mloopuv,
			    .dvert = dvert, .defgrp_index = defgrp_index,
			    .tree = &treeData, .hires_multiplier = hires_multiplier, .hr = hr,
			    .em = em, .has_velocity = has_velocity, .vert_vel = vert_vel,
			    .flow_center = flow_center, .min = min, .max = max, .res = res,
			};

			ParallelRangeSettings settings;
			BLI_parallel_range_settings_defaults(&settings);
			settings.scheduling_mode = TASK_SCHEDULING_DYNAMIC;
			BLI_task_parallel_range(min[2], max[2],
			                        &data,
			                        emit_from_derivedmesh_task_cb,
			                        &settings);
		}
		/* free bvh tree */
		free_bvhtree_from_mesh(&treeData);
		/* restore original mverts */
		CustomData_set_layer(&dm->vertData, CD_MVERT, mvert_orig);

		if (mvert) {
			MEM_freeN(mvert);
		}
		if (vert_vel) {
			MEM_freeN(vert_vel);
		}

		dm->needsFree = 1;
		dm->release(dm);
	}
	
//	clock_t end = clock();
//	float seconds = (float)(end - start) / CLOCKS_PER_SEC;
//	printf("TIME FOR RECONSTRUCTING SDF: %f \n", seconds);
}

/**********************************************************
 *	Smoke step
 **********************************************************/

static void adjustDomainResolution(SmokeDomainSettings *sds, int new_shift[3], EmissionMap *emaps, unsigned int numflowobj, float dt)
{
	const int block_size = sds->amplify + 1;
	int min[3] = {32767, 32767, 32767}, max[3] = {-32767, -32767, -32767}, res[3];
	int total_cells = 1, res_changed = 0, shift_changed = 0;
	float min_vel[3], max_vel[3];
	int x, y, z;
	float *density = smoke_get_density(sds->fluid);
	float *fuel = smoke_get_fuel(sds->fluid);
	float *bigdensity = smoke_turbulence_get_density(sds->fluid);
	float *bigfuel = smoke_turbulence_get_fuel(sds->fluid);
	float *vx = smoke_get_velocity_x(sds->fluid);
	float *vy = smoke_get_velocity_y(sds->fluid);
	float *vz = smoke_get_velocity_z(sds->fluid);
	int wt_res[3];

	if (sds->flags & MOD_SMOKE_HIGHRES && sds->fluid) {
		smoke_turbulence_get_res(sds->fluid, wt_res);
	}

	INIT_MINMAX(min_vel, max_vel);

	/* Calculate bounds for current domain content */
	for (x = sds->res_min[0]; x <  sds->res_max[0]; x++)
		for (y =  sds->res_min[1]; y <  sds->res_max[1]; y++)
			for (z =  sds->res_min[2]; z <  sds->res_max[2]; z++)
			{
				int xn = x - new_shift[0];
				int yn = y - new_shift[1];
				int zn = z - new_shift[2];
				int index;
				float max_den;
				
				/* skip if cell already belongs to new area */
				if (xn >= min[0] && xn <= max[0] && yn >= min[1] && yn <= max[1] && zn >= min[2] && zn <= max[2])
					continue;

				index = smoke_get_index(x - sds->res_min[0], sds->res[0], y - sds->res_min[1], sds->res[1], z - sds->res_min[2]);
				max_den = (fuel) ? MAX2(density[index], fuel[index]) : density[index];

				/* check high resolution bounds if max density isnt already high enough */
				if (max_den < sds->adapt_threshold && sds->flags & MOD_SMOKE_HIGHRES && sds->fluid) {
					int i, j, k;
					/* high res grid index */
					int xx = (x - sds->res_min[0]) * block_size;
					int yy = (y - sds->res_min[1]) * block_size;
					int zz = (z - sds->res_min[2]) * block_size;

					for (i = 0; i < block_size; i++)
						for (j = 0; j < block_size; j++)
							for (k = 0; k < block_size; k++)
							{
								int big_index = smoke_get_index(xx + i, wt_res[0], yy + j, wt_res[1], zz + k);
								float den = (bigfuel) ? MAX2(bigdensity[big_index], bigfuel[big_index]) : bigdensity[big_index];
								if (den > max_den) {
									max_den = den;
								}
							}
				}

				/* content bounds (use shifted coordinates) */
				if (max_den >= sds->adapt_threshold) {
					if (min[0] > xn) min[0] = xn;
					if (min[1] > yn) min[1] = yn;
					if (min[2] > zn) min[2] = zn;
					if (max[0] < xn) max[0] = xn;
					if (max[1] < yn) max[1] = yn;
					if (max[2] < zn) max[2] = zn;
				}

				/* velocity bounds */
				if (min_vel[0] > vx[index]) min_vel[0] = vx[index];
				if (min_vel[1] > vy[index]) min_vel[1] = vy[index];
				if (min_vel[2] > vz[index]) min_vel[2] = vz[index];
				if (max_vel[0] < vx[index]) max_vel[0] = vx[index];
				if (max_vel[1] < vy[index]) max_vel[1] = vy[index];
				if (max_vel[2] < vz[index]) max_vel[2] = vz[index];
			}

	/* also apply emission maps */
	for (int i = 0; i < numflowobj; i++) {
		EmissionMap *em = &emaps[i];

		for (x = em->min[0]; x < em->max[0]; x++)
			for (y = em->min[1]; y < em->max[1]; y++)
				for (z = em->min[2]; z < em->max[2]; z++)
				{
					int index = smoke_get_index(x - em->min[0], em->res[0], y - em->min[1], em->res[1], z - em->min[2]);
					float max_den = em->influence[index];

					/* density bounds */
					if (max_den >= sds->adapt_threshold) {
						if (min[0] > x) min[0] = x;
						if (min[1] > y) min[1] = y;
						if (min[2] > z) min[2] = z;
						if (max[0] < x) max[0] = x;
						if (max[1] < y) max[1] = y;
						if (max[2] < z) max[2] = z;
					}
				}
	}

	/* calculate new bounds based on these values */
	mul_v3_fl(min_vel, 1.0f / sds->dx);
	mul_v3_fl(max_vel, 1.0f / sds->dx);
	clampBoundsInDomain(sds, min, max, min_vel, max_vel, sds->adapt_margin + 1, dt);

	for (int i = 0; i < 3; i++) {
		/* calculate new resolution */
		res[i] = max[i] - min[i];
		total_cells *= res[i];

		if (new_shift[i])
			shift_changed = 1;

		/* if no content set minimum dimensions */
		if (res[i] <= 0) {
			int j;
			for (j = 0; j < 3; j++) {
				min[j] = 0;
				max[j] = 1;
				res[j] = 1;
			}
			res_changed = 1;
			total_cells = 1;
			break;
		}
		if (min[i] != sds->res_min[i] || max[i] != sds->res_max[i])
			res_changed = 1;
	}

	if (res_changed || shift_changed) {
		struct FLUID *fluid_old = sds->fluid;

		/* allocate new fluid data */
		smoke_reallocate_fluid(sds, res, 0);
		if (sds->flags & MOD_SMOKE_HIGHRES) {
			smoke_reallocate_highres_fluid(sds, sds->dx, res);
		}

		/* copy values from old fluid to new */
		if (sds->total_cells > 1 && total_cells > 1) {
			/* low res smoke */
			float *o_dens, *o_react, *o_flame, *o_fuel, *o_heat, *o_vx, *o_vy, *o_vz, *o_r, *o_g, *o_b;
			float *n_dens, *n_react, *n_flame, *n_fuel, *n_heat, *n_vx, *n_vy, *n_vz, *n_r, *n_g, *n_b;
			float dummy, *dummy_s;
			int *dummy_p;
			/* high res smoke */
			int wt_res_old[3];
			float *o_wt_dens, *o_wt_react, *o_wt_flame, *o_wt_fuel, *o_wt_tcu, *o_wt_tcv, *o_wt_tcw, *o_wt_tcu2, *o_wt_tcv2, *o_wt_tcw2, *o_wt_r, *o_wt_g, *o_wt_b;
			float *n_wt_dens, *n_wt_react, *n_wt_flame, *n_wt_fuel, *n_wt_tcu, *n_wt_tcv, *n_wt_tcw, *n_wt_tcu2, *n_wt_tcv2, *n_wt_tcw2, *n_wt_r, *n_wt_g, *n_wt_b;

			smoke_export(fluid_old, &dummy, &dummy, &o_dens, &o_react, &o_flame, &o_fuel, &o_heat, &o_vx, &o_vy, &o_vz, &o_r, &o_g, &o_b, &dummy_p, &dummy_s);
			smoke_export(sds->fluid, &dummy, &dummy, &n_dens, &n_react, &n_flame, &n_fuel, &n_heat, &n_vx, &n_vy, &n_vz, &n_r, &n_g, &n_b, &dummy_p, &dummy_s);

			if (sds->flags & MOD_SMOKE_HIGHRES) {
				smoke_turbulence_export(fluid_old, &o_wt_dens, &o_wt_react, &o_wt_flame, &o_wt_fuel, &o_wt_r, &o_wt_g, &o_wt_b, &o_wt_tcu, &o_wt_tcv, &o_wt_tcw, &o_wt_tcu2, &o_wt_tcv2, &o_wt_tcw2);
				smoke_turbulence_get_res(fluid_old, wt_res_old);
				smoke_turbulence_export(sds->fluid, &n_wt_dens, &n_wt_react, &n_wt_flame, &n_wt_fuel, &n_wt_r, &n_wt_g, &n_wt_b, &n_wt_tcu, &n_wt_tcv, &n_wt_tcw, &n_wt_tcu2, &n_wt_tcv2, &n_wt_tcw2);
			}

			for (x = sds->res_min[0]; x < sds->res_max[0]; x++)
				for (y = sds->res_min[1]; y < sds->res_max[1]; y++)
					for (z = sds->res_min[2]; z < sds->res_max[2]; z++)
					{
						/* old grid index */
						int xo = x - sds->res_min[0];
						int yo = y - sds->res_min[1];
						int zo = z - sds->res_min[2];
						int index_old = smoke_get_index(xo, sds->res[0], yo, sds->res[1], zo);
						/* new grid index */
						int xn = x - min[0] - new_shift[0];
						int yn = y - min[1] - new_shift[1];
						int zn = z - min[2] - new_shift[2];
						int index_new = smoke_get_index(xn, res[0], yn, res[1], zn);

						/* skip if outside new domain */
						if (xn < 0 || xn >= res[0] ||
						    yn < 0 || yn >= res[1] ||
						    zn < 0 || zn >= res[2])
							continue;

						/* copy data */
						n_dens[index_new] = o_dens[index_old];
						/* heat */
						if (n_heat && o_heat) {
							n_heat[index_new] = o_heat[index_old];
						}
						/* fuel */
						if (n_fuel && o_fuel) {
							n_flame[index_new] = o_flame[index_old];
							n_fuel[index_new] = o_fuel[index_old];
							n_react[index_new] = o_react[index_old];
						}
						/* color */
						if (o_r && n_r) {
							n_r[index_new] = o_r[index_old];
							n_g[index_new] = o_g[index_old];
							n_b[index_new] = o_b[index_old];
						}
						n_vx[index_new] = o_vx[index_old];
						n_vy[index_new] = o_vy[index_old];
						n_vz[index_new] = o_vz[index_old];

						if (sds->flags & MOD_SMOKE_HIGHRES && fluid_old) {
							int i, j, k;
							/* old grid index */
							int xx_o = xo * block_size;
							int yy_o = yo * block_size;
							int zz_o = zo * block_size;
							/* new grid index */
							int xx_n = xn * block_size;
							int yy_n = yn * block_size;
							int zz_n = zn * block_size;

							n_wt_tcu[index_new] = o_wt_tcu[index_old];
							n_wt_tcv[index_new] = o_wt_tcv[index_old];
							n_wt_tcw[index_new] = o_wt_tcw[index_old];
							
							n_wt_tcu2[index_new] = o_wt_tcu2[index_old];
							n_wt_tcv2[index_new] = o_wt_tcv2[index_old];
							n_wt_tcw2[index_new] = o_wt_tcw2[index_old];

							for (i = 0; i < block_size; i++)
								for (j = 0; j < block_size; j++)
									for (k = 0; k < block_size; k++)
									{
										int big_index_old = smoke_get_index(xx_o + i, wt_res_old[0], yy_o + j, wt_res_old[1], zz_o + k);
										int big_index_new = smoke_get_index(xx_n + i, sds->res_wt[0], yy_n + j, sds->res_wt[1], zz_n + k);
										/* copy data */
										n_wt_dens[big_index_new] = o_wt_dens[big_index_old];
										if (n_wt_flame && o_wt_flame) {
											n_wt_flame[big_index_new] = o_wt_flame[big_index_old];
											n_wt_fuel[big_index_new] = o_wt_fuel[big_index_old];
											n_wt_react[big_index_new] = o_wt_react[big_index_old];
										}
										if (n_wt_r && o_wt_r) {
											n_wt_r[big_index_new] = o_wt_r[big_index_old];
											n_wt_g[big_index_new] = o_wt_g[big_index_old];
											n_wt_b[big_index_new] = o_wt_b[big_index_old];
										}
									}
						}
					}
		}
		smoke_free(fluid_old);

		/* set new domain dimensions */
		VECCOPY(sds->res_min, min);
		VECCOPY(sds->res_max, max);
		VECCOPY(sds->res, res);
		sds->total_cells = total_cells;
	}
}

BLI_INLINE void apply_outflow_fields(int index, float distance_value, float *density, float *heat, float *fuel, float *react, float *color_r, float *color_g, float *color_b, float *phiout)
{
	/* determine outflow cells - phiout used in smoke and liquids */
	if (phiout) {
		phiout[index] = distance_value;
	}

	/* set smoke outflow */
	if (density) {
		density[index] = 0.f;
	}
	if (heat) {
		heat[index] = 0.f;
	}
	if (fuel) {
		fuel[index] = 0.f;
		react[index] = 0.f;
	}
	if (color_r) {
		color_r[index] = 0.f;
		color_g[index] = 0.f;
		color_b[index] = 0.f;
	}
}

BLI_INLINE void apply_inflow_fields(SmokeFlowSettings *sfs, float emission_value, float distance_value, int index, float *density, float *heat, float *fuel, float *react, float *color_r, float *color_g, float *color_b, float *phi, float *emission_in)
{
	/* add liquid inflow */
	if (phi) {
		phi[index] = distance_value;
		return;
	}

	/* save inflow value for mantaflow standalone */
	if (emission_in) {
		emission_in[index] = emission_value;
	}

	/* add smoke inflow */
	int absolute_flow = (sfs->flags & MOD_SMOKE_FLOW_ABSOLUTE);
	float dens_old = density[index];
	// float fuel_old = (fuel) ? fuel[index] : 0.0f;  /* UNUSED */
	float dens_flow = (sfs->type == MOD_SMOKE_FLOW_TYPE_FIRE) ? 0.0f : emission_value * sfs->density;
	float fuel_flow = (fuel) ? emission_value * sfs->fuel_amount : 0.0f;
	/* add heat */
	if (heat && emission_value > 0.0f) {
		heat[index] = ADD_IF_LOWER(heat[index], sfs->temp);
	}
	/* absolute */
	if (absolute_flow) {
		if (sfs->type != MOD_SMOKE_FLOW_TYPE_FIRE) {
			if (dens_flow > density[index])
				density[index] = dens_flow;
		}
		if (sfs->type != MOD_SMOKE_FLOW_TYPE_SMOKE && fuel && fuel_flow) {
			if (fuel_flow > fuel[index])
				fuel[index] = fuel_flow;
		}
	}
	/* additive */
	else {
		if (sfs->type != MOD_SMOKE_FLOW_TYPE_FIRE) {
			density[index] += dens_flow;
			CLAMP(density[index], 0.0f, 1.0f);
		}
		if (sfs->type != MOD_SMOKE_FLOW_TYPE_SMOKE && fuel && sfs->fuel_amount) {
			fuel[index] += fuel_flow;
			CLAMP(fuel[index], 0.0f, 10.0f);
		}
	}

	/* set color */
	if (color_r && dens_flow) {
		float total_dens = density[index] / (dens_old + dens_flow);
		color_r[index] = (color_r[index] + sfs->color[0] * dens_flow) * total_dens;
		color_g[index] = (color_g[index] + sfs->color[1] * dens_flow) * total_dens;
		color_b[index] = (color_b[index] + sfs->color[2] * dens_flow) * total_dens;
	}

	/* set fire reaction coordinate */
	if (fuel && fuel[index] > FLT_EPSILON) {
		/* instead of using 1.0 for all new fuel add slight falloff
		 * to reduce flow blockiness */
		float value = 1.0f - pow2f(1.0f - emission_value);

		if (value > react[index]) {
			float f = fuel_flow / fuel[index];
			react[index] = value * f + (1.0f - f) * react[index];
			CLAMP(react[index], 0.0f, value);
		}
	}
}

static void update_flowsflags(SmokeDomainSettings *sds, Object **flowobjs, int numflowobj)
{
	int active_fields = sds->active_fields;
	unsigned int flowIndex;

	/* Monitor active fields based on flow settings */
	for (flowIndex = 0; flowIndex < numflowobj; flowIndex++)
	{
		Object *collob = flowobjs[flowIndex];
		SmokeModifierData *smd2 = (SmokeModifierData *)modifiers_findByType(collob, eModifierType_Smoke);
		
		if ((smd2->type & MOD_SMOKE_TYPE_FLOW) && smd2->flow) {
			SmokeFlowSettings *sfs = smd2->flow;
			if (!sfs) break;
			if (sfs->flags & MOD_SMOKE_FLOW_INITVELOCITY) {
				active_fields |= SM_ACTIVE_INVEL;
			}
			/* activate heat field if flow produces any heat */
			if (sfs->temp && sds->type == MOD_SMOKE_DOMAIN_TYPE_GAS) {
				active_fields |= SM_ACTIVE_HEAT;
			}
			/* activate fuel field if flow adds any fuel */
			if (sfs->fuel_amount && (sfs->type == MOD_SMOKE_FLOW_TYPE_FIRE || sfs->type == MOD_SMOKE_FLOW_TYPE_SMOKEFIRE)) {
				active_fields |= SM_ACTIVE_FIRE;
			}
			/* activate color field if flows add smoke with varying colors */
			if (sfs->density && (sfs->type == MOD_SMOKE_FLOW_TYPE_SMOKE || sfs->type == MOD_SMOKE_FLOW_TYPE_SMOKEFIRE)) {
				if (!(active_fields & SM_ACTIVE_COLOR_SET)) {
					copy_v3_v3(sds->active_color, sfs->color);
					active_fields |= SM_ACTIVE_COLOR_SET;
				}
				else if (!equals_v3v3(sds->active_color, sfs->color)) {
					copy_v3_v3(sds->active_color, sfs->color);
					active_fields |= SM_ACTIVE_COLORS;
				}
			}
		}
	}
	/* Monitor active fields based on domain settings */
	if (active_fields & SM_ACTIVE_FIRE) {
		/* heat is always needed for fire */
		active_fields |= SM_ACTIVE_HEAT;
		/* also activate colors if domain smoke color differs from active color */
		if (!(active_fields & SM_ACTIVE_COLOR_SET)) {
			copy_v3_v3(sds->active_color, sds->flame_smoke_color);
			active_fields |= SM_ACTIVE_COLOR_SET;
		}
		else if (!equals_v3v3(sds->active_color, sds->flame_smoke_color)) {
			copy_v3_v3(sds->active_color, sds->flame_smoke_color);
			active_fields |= SM_ACTIVE_COLORS;
		}
	}
	/* Finally, initialize new data fields if any */
	if (active_fields & SM_ACTIVE_INVEL) {
		fluid_ensure_invelocity(sds->fluid, sds->smd);
	}
	if (active_fields & SM_ACTIVE_HEAT) {
		smoke_ensure_heat(sds->fluid, sds->smd);
	}
	if (active_fields & SM_ACTIVE_FIRE) {
		smoke_ensure_fire(sds->fluid, sds->smd);
	}
	if (active_fields & SM_ACTIVE_COLORS) {
		/* initialize all smoke with "active_color" */
		smoke_ensure_colors(sds->fluid, sds->smd);
	}
	if (sds->type == MOD_SMOKE_DOMAIN_TYPE_LIQUID && (sds->particle_type & MOD_SMOKE_PARTICLE_SPRAY || sds->particle_type & MOD_SMOKE_PARTICLE_FOAM || sds->particle_type & MOD_SMOKE_PARTICLE_TRACER)) {
		fluid_ensure_sndparts(sds->fluid, sds->smd);
	}
	sds->active_fields = active_fields;
}

static void update_flowsfluids(Scene *scene, Object *ob, SmokeDomainSettings *sds, float time_per_frame, float frame_length, int frame)
{
	EmissionMap *emaps = NULL;
	int new_shift[3] = {0};
	Object **flowobjs = NULL;
	unsigned int numflowobj = 0, flowIndex = 0;

	flowobjs = get_collisionobjects(scene, ob, sds->fluid_group, &numflowobj, eModifierType_Smoke);

	/* Update all flow related flags and ensure that corresponding grids get initialized */
	update_flowsflags(sds, flowobjs, numflowobj);

	/* calculate domain shift for current frame if using adaptive domain */
	if (sds->flags & MOD_SMOKE_ADAPTIVE_DOMAIN) {
		int total_shift[3];
		float frame_shift_f[3];
		float ob_loc[3] = {0};

		mul_m4_v3(ob->obmat, ob_loc);

		VECSUB(frame_shift_f, ob_loc, sds->prev_loc);
		copy_v3_v3(sds->prev_loc, ob_loc);
		/* convert global space shift to local "cell" space */
		mul_mat3_m4_v3(sds->imat, frame_shift_f);
		frame_shift_f[0] = frame_shift_f[0] / sds->cell_size[0];
		frame_shift_f[1] = frame_shift_f[1] / sds->cell_size[1];
		frame_shift_f[2] = frame_shift_f[2] / sds->cell_size[2];
		/* add to total shift */
		VECADD(sds->shift_f, sds->shift_f, frame_shift_f);
		/* convert to integer */
		total_shift[0] = floor(sds->shift_f[0]);
		total_shift[1] = floor(sds->shift_f[1]);
		total_shift[2] = floor(sds->shift_f[2]);
		VECSUB(new_shift, total_shift, sds->shift);
		copy_v3_v3_int(sds->shift, total_shift);

		/* calculate new domain boundary points so that smoke doesnt slide on sub-cell movement */
		sds->p0[0] = sds->dp0[0] - sds->cell_size[0] * (sds->shift_f[0] - total_shift[0] - 0.5f);
		sds->p0[1] = sds->dp0[1] - sds->cell_size[1] * (sds->shift_f[1] - total_shift[1] - 0.5f);
		sds->p0[2] = sds->dp0[2] - sds->cell_size[2] * (sds->shift_f[2] - total_shift[2] - 0.5f);
		sds->p1[0] = sds->p0[0] + sds->cell_size[0] * sds->base_res[0];
		sds->p1[1] = sds->p0[1] + sds->cell_size[1] * sds->base_res[1];
		sds->p1[2] = sds->p0[2] + sds->cell_size[2] * sds->base_res[2];
	}

	/* init emission maps for each flow */
	emaps = MEM_callocN(sizeof(struct EmissionMap) * numflowobj, "smoke_flow_maps");
	
	/* Prepare flow emission maps */
	for (flowIndex = 0; flowIndex < numflowobj; flowIndex++)
	{
		Object *flowobj = flowobjs[flowIndex];
		SmokeModifierData *smd2 = (SmokeModifierData *)modifiers_findByType(flowobj, eModifierType_Smoke);

		/* Check for initialized smoke object */
		if ((smd2->type & MOD_SMOKE_TYPE_FLOW) && smd2->flow) {
			SmokeFlowSettings *sfs = smd2->flow;
			int subframes = sfs->subframes;
			EmissionMap *em = &emaps[flowIndex];

			/* Length of one frame. If using adaptive stepping, length is smaller than actual frame length */
			float adaptframe_length = time_per_frame / frame_length;

			/* Further splitting because of emission subframe: If no subframes present, sample_size is 1 */
			float sample_size = 1.0f / (float)(subframes+1);
			float sdt = adaptframe_length * sample_size;
			int hires_multiplier = 1;

			/* First frame cannot have any subframes because there is (obviously) no previous frame from where subframes could come from */
			bool is_first_frame = (scene->r.cfra == frame);
			if (is_first_frame) subframes = 0;

			int subframe;
			float prev_frame_pos;

			/* Emission loop. When not using subframes this will loop only once. */
			for (subframe = 0; subframe <= subframes; subframe++) {

				/* Temporary emission map used when subframes are enabled, i.e. at least one subframe */
				EmissionMap em_temp = {NULL};

				/* Set scene time */
				/* Handle emission subframe */
				if (subframe < subframes) {
					prev_frame_pos = sdt * (float)(subframe+1);
					scene->r.subframe = prev_frame_pos;
				}
				/* Last frame in this loop (subframe == suframes). Can be real end frame or in between frames (adaptive frame) */
				else {
					/* Handle adaptive subframe (ie has subframe fraction). Need to set according scene subframe parameter */
					if (time_per_frame < frame_length) {
						scene->r.subframe = adaptframe_length;
					}
					/* Handle absolute endframe (ie no subframe fraction). Need to set the scene subframe parameter to 0 and advance current scene frame */
					else {
						scene->r.cfra = frame;
						scene->r.subframe = 0.0f;
					}
				}

				/* Emission from particles */
				if (sfs->source == MOD_SMOKE_FLOW_SOURCE_PARTICLES) {
					/* emit_from_particles() updates timestep internally */
					if (subframes) {
						emit_from_particles(flowobj, sds, sfs, &em_temp, scene, sdt);
					}
					else {
						emit_from_particles(flowobj, sds, sfs, em, scene, sdt);
					}

					if (!(sfs->flags & MOD_SMOKE_FLOW_USE_PART_SIZE)) {
						hires_multiplier = 1;
					}
				}
				/* Emission from mesh */
				else if (sfs->source == MOD_SMOKE_FLOW_SOURCE_MESH) {
					/* Update flow object frame */
					BLI_mutex_lock(&object_update_lock);
					BKE_object_modifier_update_subframe(scene, flowobj, true, 5, BKE_scene_frame_get(scene), eModifierType_Smoke);
					BLI_mutex_unlock(&object_update_lock);

					/* Apply flow */
					if (subframes) {
						emit_from_derivedmesh(flowobj, sds, sfs, &em_temp, sdt);
					}
					else {
						emit_from_derivedmesh(flowobj, sds, sfs, em, sdt);
					}
				}
				else {
					printf("Error: unknown flow emission source\n");
				}

				/* If this we emitted with temp emission map in this loop (subframe emission), we combine the temp map with the original emission map */
				if (subframes) {
					/* Combine emission maps */
					em_combineMaps(em, &em_temp, hires_multiplier, !(sfs->flags & MOD_SMOKE_FLOW_ABSOLUTE), sample_size);
					em_freeData(&em_temp);
				}
			}
		}
	}

	/* Adjust domain size if needed */
	if (sds->flags & MOD_SMOKE_ADAPTIVE_DOMAIN) {
		adjustDomainResolution(sds, new_shift, emaps, numflowobj, time_per_frame);
	}

	float *phi_in = liquid_get_phiin(sds->fluid);
	float *phiout_in = liquid_get_phioutin(sds->fluid);
	float *density = smoke_get_density(sds->fluid);
	float *color_r = smoke_get_color_r(sds->fluid);
	float *color_g = smoke_get_color_g(sds->fluid);
	float *color_b = smoke_get_color_b(sds->fluid);
	float *fuel = smoke_get_fuel(sds->fluid);
	float *heat = smoke_get_heat(sds->fluid);
	float *react = smoke_get_react(sds->fluid);
	float *emission_in = fluid_get_emission_in(sds->fluid);
	float *bigdensity = smoke_turbulence_get_density(sds->fluid);
	float *bigfuel = smoke_turbulence_get_fuel(sds->fluid);
	float *bigreact = smoke_turbulence_get_react(sds->fluid);
	float *bigcolor_r = smoke_turbulence_get_color_r(sds->fluid);
	float *bigcolor_g = smoke_turbulence_get_color_g(sds->fluid);
	float *bigcolor_b = smoke_turbulence_get_color_b(sds->fluid);
	float *velx_initial = smoke_get_in_velocity_x(sds->fluid);
	float *vely_initial = smoke_get_in_velocity_y(sds->fluid);
	float *velz_initial = smoke_get_in_velocity_z(sds->fluid);

	/* Apply emission data */
	for (flowIndex = 0; flowIndex < numflowobj; flowIndex++)
	{
		Object *flowobj = flowobjs[flowIndex];
		SmokeModifierData *smd2 = (SmokeModifierData *)modifiers_findByType(flowobj, eModifierType_Smoke);

		// check for initialized smoke object
		if ((smd2->type & MOD_SMOKE_TYPE_FLOW) && smd2->flow) {
			SmokeFlowSettings *sfs = smd2->flow;
			EmissionMap *em = &emaps[flowIndex];
			int bigres[3];
			float *velocity_map = em->velocity;
			float *emission_map = em->influence;
			float *emission_map_high = em->influence_high;
			float* distance_map = em->distances;
			float* distance_map_high = em->distances_high;

			int ii, jj, kk, gx, gy, gz, ex, ey, ez, dx, dy, dz, block_size;
			size_t e_index, d_index, index_big;

			// loop through every emission map cell
			for (gx = em->min[0]; gx < em->max[0]; gx++)
				for (gy = em->min[1]; gy < em->max[1]; gy++)
					for (gz = em->min[2]; gz < em->max[2]; gz++)
					{
						/* get emission map index */
						ex = gx - em->min[0];
						ey = gy - em->min[1];
						ez = gz - em->min[2];
						e_index = smoke_get_index(ex, em->res[0], ey, em->res[1], ez);

						/* get domain index */
						dx = gx - sds->res_min[0];
						dy = gy - sds->res_min[1];
						dz = gz - sds->res_min[2];
						d_index = smoke_get_index(dx, sds->res[0], dy, sds->res[1], dz);
						/* make sure emission cell is inside the new domain boundary */
						if (dx < 0 || dy < 0 || dz < 0 || dx >= sds->res[0] || dy >= sds->res[1] || dz >= sds->res[2]) continue;

						if (sfs->behavior == MOD_SMOKE_FLOW_BEHAVIOR_OUTFLOW) { // outflow
							apply_outflow_fields(d_index, distance_map[e_index], density, heat, fuel, react, color_r, color_g, color_b, phiout_in);
						}
						else if (sfs->behavior == MOD_SMOKE_FLOW_BEHAVIOR_GEOMETRY && smd2->time > 2) {
							apply_inflow_fields(sfs, 0.0f, 9999.0f, d_index, density, heat, fuel, react, color_r, color_g, color_b, phi_in, emission_in);
						}
						else if (sfs->behavior == MOD_SMOKE_FLOW_BEHAVIOR_INFLOW || sfs->behavior == MOD_SMOKE_FLOW_BEHAVIOR_GEOMETRY) { // inflow
							/* only apply inflow if enabled */
							if (sfs->flags & MOD_SMOKE_FLOW_USE_INFLOW) {
								apply_inflow_fields(sfs, emission_map[e_index], distance_map[e_index], d_index, density, heat, fuel, react, color_r, color_g, color_b, phi_in, emission_in);
								/* initial velocity */
								if (sfs->flags & MOD_SMOKE_FLOW_INITVELOCITY) {
									velx_initial[d_index] = velocity_map[e_index * 3];
									vely_initial[d_index] = velocity_map[e_index * 3 + 1];
									velz_initial[d_index] = velocity_map[e_index * 3 + 2];
								}
							}
						}

						/* loop through high res blocks if high res enabled */
						if (bigdensity) {
							// neighbor cell emission densities (for high resolution smoke smooth interpolation)
							float c000, c001, c010, c011,  c100, c101, c110, c111;

							smoke_turbulence_get_res(sds->fluid, bigres);

							block_size = sds->amplify + 1;  // high res block size

							c000 = (ex > 0 && ey > 0 && ez > 0) ? emission_map[smoke_get_index(ex - 1, em->res[0], ey - 1, em->res[1], ez - 1)] : 0;
							c001 = (ex > 0 && ey > 0) ? emission_map[smoke_get_index(ex - 1, em->res[0], ey - 1, em->res[1], ez)] : 0;
							c010 = (ex > 0 && ez > 0) ? emission_map[smoke_get_index(ex - 1, em->res[0], ey, em->res[1], ez - 1)] : 0;
							c011 = (ex > 0) ? emission_map[smoke_get_index(ex - 1, em->res[0], ey, em->res[1], ez)] : 0;

							c100 = (ey > 0 && ez > 0) ? emission_map[smoke_get_index(ex, em->res[0], ey - 1, em->res[1], ez - 1)] : 0;
							c101 = (ey > 0) ? emission_map[smoke_get_index(ex, em->res[0], ey - 1, em->res[1], ez)] : 0;
							c110 = (ez > 0) ? emission_map[smoke_get_index(ex, em->res[0], ey, em->res[1], ez - 1)] : 0;
							c111 = emission_map[smoke_get_index(ex, em->res[0], ey, em->res[1], ez)]; // this cell

							for (ii = 0; ii < block_size; ii++)
								for (jj = 0; jj < block_size; jj++)
									for (kk = 0; kk < block_size; kk++)
									{

										float fx, fy, fz, interpolated_value;
										int shift_x = 0, shift_y = 0, shift_z = 0;


										/* Use full sample emission map if enabled and available */
										if ((sds->highres_sampling == SM_HRES_FULLSAMPLE) && emission_map_high) {
											interpolated_value = emission_map_high[smoke_get_index(ex * block_size + ii, em->res[0] * block_size, ey * block_size + jj, em->res[1] * block_size, ez * block_size + kk)]; // this cell
										}
										else if (sds->highres_sampling == SM_HRES_NEAREST) {
											/* without interpolation use same low resolution
											 * block value for all hi-res blocks */
											interpolated_value = c111;
										}
										/* Fall back to interpolated */
										else
										{
											/* get relative block position
											 * for interpolation smoothing */
											fx = (float)ii / block_size + 0.5f / block_size;
											fy = (float)jj / block_size + 0.5f / block_size;
											fz = (float)kk / block_size + 0.5f / block_size;

											/* calculate trilinear interpolation */
											interpolated_value = c000 * (1 - fx) * (1 - fy) * (1 - fz) +
																 c100 * fx * (1 - fy) * (1 - fz) +
																 c010 * (1 - fx) * fy * (1 - fz) +
																 c001 * (1 - fx) * (1 - fy) * fz +
																 c101 * fx * (1 - fy) * fz +
																 c011 * (1 - fx) * fy * fz +
																 c110 * fx * fy * (1 - fz) +
																 c111 * fx * fy * fz;


											/* add some contrast / sharpness
											 * depending on hi-res block size */
											interpolated_value = (interpolated_value - 0.4f) * (block_size / 2) + 0.4f;
											CLAMP(interpolated_value, 0.0f, 1.0f);

											/* shift smoke block index
											 * (because pixel center is actually
											 * in halfway of the low res block) */
											shift_x = (dx < 1) ? 0 : block_size / 2;
											shift_y = (dy < 1) ? 0 : block_size / 2;
											shift_z = (dz < 1) ? 0 : block_size / 2;
										}

										/* get shifted index for current high resolution block */
										index_big = smoke_get_index(block_size * dx + ii - shift_x, bigres[0], block_size * dy + jj - shift_y, bigres[1], block_size * dz + kk - shift_z);

										if (sfs->behavior == MOD_SMOKE_FLOW_BEHAVIOR_OUTFLOW) { // outflow
											if (interpolated_value) {
												apply_outflow_fields(index_big, distance_map_high[index_big], bigdensity, NULL, bigfuel, bigreact, bigcolor_r, bigcolor_g, bigcolor_b, NULL);
											}
										}
										else if (sfs->behavior == MOD_SMOKE_FLOW_BEHAVIOR_GEOMETRY && smd2->time > 2) {
											apply_inflow_fields(sfs, 0.0f, 0.5f, d_index, density, heat, fuel, react, color_r, color_g, color_b, NULL, NULL);
										}
										else if (sfs->behavior == MOD_SMOKE_FLOW_BEHAVIOR_INFLOW || sfs->behavior == MOD_SMOKE_FLOW_BEHAVIOR_GEOMETRY) { // inflow
											// TODO (sebbas) inflow map highres?
											apply_inflow_fields(sfs, interpolated_value, distance_map_high[index_big], index_big, bigdensity, NULL, bigfuel, bigreact, bigcolor_r, bigcolor_g, bigcolor_b, NULL, NULL);
										}
									} // hires loop
						}  // bigdensity
					} // low res loop

			// free emission maps
			em_freeData(em);

		} // end emission
	}

	if (flowobjs) MEM_freeN(flowobjs);
	if (emaps) MEM_freeN(emaps);
}

typedef struct UpdateEffectorsData {
	Scene *scene;
	SmokeDomainSettings *sds;
	ListBase *effectors;

	float *density;
	float *fuel;
	float *force_x;
	float *force_y;
	float *force_z;
	float *velocity_x;
	float *velocity_y;
	float *velocity_z;
	int *flags;
	float *phiObsIn;
} UpdateEffectorsData;

static void update_effectors_task_cb(
        void *__restrict userdata,
        const int x,
        const ParallelRangeTLS *__restrict UNUSED(tls))
{
	UpdateEffectorsData *data = userdata;
	SmokeDomainSettings *sds = data->sds;

	for (int y = 0; y < sds->res[1]; y++) {
		for (int z = 0; z < sds->res[2]; z++)
		{
			EffectedPoint epoint;
			float mag;
			float voxelCenter[3] = {0, 0, 0}, vel[3] = {0, 0, 0}, retvel[3] = {0, 0, 0};
			const unsigned int index = smoke_get_index(x, sds->res[0], y, sds->res[1], z);

			if ((data->fuel && MAX2(data->density[index], data->fuel[index]) < FLT_EPSILON) ||
				(data->density && data->density[index] < FLT_EPSILON) ||
				(data->phiObsIn  && data->phiObsIn[index] < 0.0f) ||
				// TODO (sebbas): isnt checking phiobs enough? maybe remove flags check
				 data->flags[index] & 2) // mantaflow convention: 2 == FlagObstacle
			{
				continue;
			}

			vel[0] = data->velocity_x[index];
			vel[1] = data->velocity_y[index];
			vel[2] = data->velocity_z[index];

			/* convert vel to global space */
			mag = len_v3(vel);
			mul_mat3_m4_v3(sds->obmat, vel);
			normalize_v3(vel);
			mul_v3_fl(vel, mag);

			voxelCenter[0] = sds->p0[0] + sds->cell_size[0] * ((float)(x + sds->res_min[0]) + 0.5f);
			voxelCenter[1] = sds->p0[1] + sds->cell_size[1] * ((float)(y + sds->res_min[1]) + 0.5f);
			voxelCenter[2] = sds->p0[2] + sds->cell_size[2] * ((float)(z + sds->res_min[2]) + 0.5f);
			mul_m4_v3(sds->obmat, voxelCenter);

			pd_point_from_loc(data->scene, voxelCenter, vel, index, &epoint);
			pdDoEffectors(data->effectors, NULL, sds->effector_weights, &epoint, retvel, NULL);

			/* convert retvel to local space */
			mag = len_v3(retvel);
			mul_mat3_m4_v3(sds->imat, retvel);
			normalize_v3(retvel);
			mul_v3_fl(retvel, mag);

			// TODO dg - do in force!
			data->force_x[index] = min_ff(max_ff(-1.0f, retvel[0] * 0.2f), 1.0f);
			data->force_y[index] = min_ff(max_ff(-1.0f, retvel[1] * 0.2f), 1.0f);
			data->force_z[index] = min_ff(max_ff(-1.0f, retvel[2] * 0.2f), 1.0f);
		}
	}
}

static void update_effectors(Scene *scene, Object *ob, SmokeDomainSettings *sds, float UNUSED(dt))
{
	ListBase *effectors;
	/* make sure smoke flow influence is 0.0f */
	sds->effector_weights->weight[PFIELD_SMOKEFLOW] = 0.0f;
	effectors = pdInitEffectors(scene, ob, NULL, sds->effector_weights, true);

	if (effectors) {
		// precalculate wind forces
		UpdateEffectorsData data;
		data.scene = scene;
		data.sds = sds;
		data.effectors = effectors;
		data.density = smoke_get_density(sds->fluid);
		data.fuel = smoke_get_fuel(sds->fluid);
		data.force_x = smoke_get_force_x(sds->fluid);
		data.force_y = smoke_get_force_y(sds->fluid);
		data.force_z = smoke_get_force_z(sds->fluid);
		data.velocity_x = smoke_get_velocity_x(sds->fluid);
		data.velocity_y = smoke_get_velocity_y(sds->fluid);
		data.velocity_z = smoke_get_velocity_z(sds->fluid);
		data.flags = smoke_get_obstacle(sds->fluid);
		data.phiObsIn = liquid_get_phiobsin(sds->fluid);

		ParallelRangeSettings settings;
		BLI_parallel_range_settings_defaults(&settings);
		settings.scheduling_mode = TASK_SCHEDULING_DYNAMIC;
		BLI_task_parallel_range(0, sds->res[0],
		                        &data,
		                        update_effectors_task_cb,
		                        &settings);
	}

	pdEndEffectors(&effectors);
}

static DerivedMesh *createLiquidMesh(SmokeDomainSettings *sds, DerivedMesh *orgdm, Object* ob)
{
	DerivedMesh *dm;
	MVert *mverts;
	MPoly *mpolys;
	MLoop *mloops;
	short *normals, *no_s;
	float no[3];
	float min[3];
	float max[3];
	float size[3];
	float cell_size_scaled[3];

	/* assign material + flags to new dm
	 * if there's no faces in original dm, keep materials and flags unchanged */
	MPoly *mpoly;
	MPoly mp_example = {0};
	mpoly = orgdm->getPolyArray(orgdm);
	if (mpoly) {
		mp_example = *mpoly;
	}

	const short mp_mat_nr = mp_example.mat_nr;
	const char mp_flag    = mp_example.flag;

	int i;
	int num_verts, num_normals, num_faces;

	if (!sds->fluid)
		return NULL;

	/* just display original object */
	if (sds->viewport_display_mode == SM_VIEWPORT_GEOMETRY)
		return NULL;

	num_verts   = liquid_get_num_verts(sds->fluid);
	num_normals = liquid_get_num_normals(sds->fluid);
	num_faces   = liquid_get_num_triangles(sds->fluid);

//	printf("num_verts: %d, num_normals: %d, num_faces: %d\n", num_verts, num_normals, num_faces);

	if (!num_verts || !num_normals || !num_faces)
		return NULL;

	dm     = CDDM_new(num_verts, 0, 0, num_faces * 3, num_faces);
	mverts = CDDM_get_verts(dm);
	mpolys = CDDM_get_polys(dm);
	mloops = CDDM_get_loops(dm);

	if (!dm)
		return NULL;

	// Get size (dimension) but considering scaling scaling
	copy_v3_v3(cell_size_scaled, sds->cell_size);
	mul_v3_v3(cell_size_scaled, ob->size);
	VECMADD(min, sds->p0, cell_size_scaled, sds->res_min);
	VECMADD(max, sds->p0, cell_size_scaled, sds->res_max);
	sub_v3_v3v3(size, max, min);

	// Biggest dimension will be used for upscaling
	float max_size = MAX3(size[0], size[1], size[2]);

	// Vertices
	for (i = 0; i < num_verts; i++, mverts++)
	{
		// read raw data. is normalized cube around domain origin
		mverts->co[0] = liquid_get_vertex_x_at(sds->fluid, i);
		mverts->co[1] = liquid_get_vertex_y_at(sds->fluid, i);
		mverts->co[2] = liquid_get_vertex_z_at(sds->fluid, i);

		mverts->co[0] *= max_size / fabsf(ob->size[0]);
		mverts->co[1] *= max_size / fabsf(ob->size[1]);
		mverts->co[2] *= max_size / fabsf(ob->size[2]);

//		printf("mverts->co[0]: %f, mverts->co[1]: %f, mverts->co[2]: %f\n", mverts->co[0], mverts->co[1], mverts->co[2]);
	}

	// Normals
	normals = MEM_callocN(sizeof(short) * num_normals * 3, "liquid_tmp_normals");

	for (i = 0, no_s = normals; i < num_normals; no_s += 3, i++)
	{
		no[0] = liquid_get_normal_x_at(sds->fluid, i);
		no[1] = liquid_get_normal_y_at(sds->fluid, i);
		no[2] = liquid_get_normal_z_at(sds->fluid, i);

		normal_float_to_short_v3(no_s, no);

//		printf("no_s[0]: %d, no_s[1]: %d, no_s[2]: %d\n", no_s[0], no_s[1], no_s[2]);
	}

	// Triangles
	for (i = 0; i < num_faces; i++, mpolys++, mloops += 3)
	{
		/* initialize from existing face */
		mpolys->mat_nr = mp_mat_nr; // TODO (sebbas)
		mpolys->flag =   mp_flag; // TODO (sebbas)

		mpolys->loopstart = i * 3;
		mpolys->totloop = 3;

		mloops[0].v = liquid_get_triangle_x_at(sds->fluid, i);
		mloops[1].v = liquid_get_triangle_y_at(sds->fluid, i);
		mloops[2].v = liquid_get_triangle_z_at(sds->fluid, i);

//		printf("mloops[0].v: %d, mloops[1].v: %d, mloops[2].v: %d\n", mloops[0].v, mloops[1].v, mloops[2].v);
	}

	CDDM_calc_edges(dm);
	CDDM_apply_vert_normals(dm, (short (*)[3])normals);

	MEM_freeN(normals);

	return dm;
}

static DerivedMesh *createDomainGeometry(SmokeDomainSettings *sds, Object *ob)
{
	DerivedMesh *result;
	MVert *mverts;
	MPoly *mpolys;
	MLoop *mloops;
	float min[3];
	float max[3];
	float *co;
	MPoly *mp;
	MLoop *ml;

	int num_verts = 8;
	int num_faces = 6;
	int i;
	float ob_loc[3] = {0};
	float ob_cache_loc[3] = {0};

	/* dont generate any mesh if there isnt any content */
	if (sds->total_cells <= 1) {
		num_verts = 0;
		num_faces = 0;
	}

	result = CDDM_new(num_verts, 0, 0, num_faces * 4, num_faces);
	mverts = CDDM_get_verts(result);
	mpolys = CDDM_get_polys(result);
	mloops = CDDM_get_loops(result);


	if (num_verts) {
		/* volume bounds */
		VECMADD(min, sds->p0, sds->cell_size, sds->res_min);
		VECMADD(max, sds->p0, sds->cell_size, sds->res_max);

		/* set vertices */
		/* top slab */
		co = mverts[0].co; co[0] = min[0]; co[1] = min[1]; co[2] = max[2];
		co = mverts[1].co; co[0] = max[0]; co[1] = min[1]; co[2] = max[2];
		co = mverts[2].co; co[0] = max[0]; co[1] = max[1]; co[2] = max[2];
		co = mverts[3].co; co[0] = min[0]; co[1] = max[1]; co[2] = max[2];
		/* bottom slab */
		co = mverts[4].co; co[0] = min[0]; co[1] = min[1]; co[2] = min[2];
		co = mverts[5].co; co[0] = max[0]; co[1] = min[1]; co[2] = min[2];
		co = mverts[6].co; co[0] = max[0]; co[1] = max[1]; co[2] = min[2];
		co = mverts[7].co; co[0] = min[0]; co[1] = max[1]; co[2] = min[2];

		/* create faces */
		/* top */
		mp = &mpolys[0]; ml = &mloops[0 * 4]; mp->loopstart = 0 * 4; mp->totloop = 4;
		ml[0].v = 0; ml[1].v = 1; ml[2].v = 2; ml[3].v = 3;
		/* right */
		mp = &mpolys[1]; ml = &mloops[1 * 4]; mp->loopstart = 1 * 4; mp->totloop = 4;
		ml[0].v = 2; ml[1].v = 1; ml[2].v = 5; ml[3].v = 6;
		/* bottom */
		mp = &mpolys[2]; ml = &mloops[2 * 4]; mp->loopstart = 2 * 4; mp->totloop = 4;
		ml[0].v = 7; ml[1].v = 6; ml[2].v = 5; ml[3].v = 4;
		/* left */
		mp = &mpolys[3]; ml = &mloops[3 * 4]; mp->loopstart = 3 * 4; mp->totloop = 4;
		ml[0].v = 0; ml[1].v = 3; ml[2].v = 7; ml[3].v = 4;
		/* front */
		mp = &mpolys[4]; ml = &mloops[4 * 4]; mp->loopstart = 4 * 4; mp->totloop = 4;
		ml[0].v = 3; ml[1].v = 2; ml[2].v = 6; ml[3].v = 7;
		/* back */
		mp = &mpolys[5]; ml = &mloops[5 * 4]; mp->loopstart = 5 * 4; mp->totloop = 4;
		ml[0].v = 1; ml[1].v = 0; ml[2].v = 4; ml[3].v = 5;

		/* calculate required shift to match domain's global position
		 *  it was originally simulated at (if object moves without smoke step) */
		invert_m4_m4(ob->imat, ob->obmat);
		mul_m4_v3(ob->obmat, ob_loc);
		mul_m4_v3(sds->obmat, ob_cache_loc);
		VECSUB(sds->obj_shift_f, ob_cache_loc, ob_loc);
		/* convert shift to local space and apply to vertices */
		mul_mat3_m4_v3(ob->imat, sds->obj_shift_f);
		/* apply */
		for (i = 0; i < num_verts; i++) {
			add_v3_v3(mverts[i].co, sds->obj_shift_f);
		}
	}


	CDDM_calc_edges(result);
	result->dirty |= DM_DIRTY_NORMALS;
	return result;
}

static void smokeModifier_process(SmokeModifierData *smd, Scene *scene, Object *ob, DerivedMesh *dm)
{
	if ((smd->type & MOD_SMOKE_TYPE_FLOW))
	{
		if (scene->r.cfra >= smd->time)
			smokeModifier_init(smd, ob, scene, dm);

		if (smd->flow->dm) smd->flow->dm->release(smd->flow->dm);
		smd->flow->dm = CDDM_copy(dm);

		if (scene->r.cfra > smd->time)
		{
			smd->time = scene->r.cfra;
		}
		else if (scene->r.cfra < smd->time)
		{
			smd->time = scene->r.cfra;
			smokeModifier_reset_ex(smd, false);
		}
	}
	else if (smd->type & MOD_SMOKE_TYPE_EFFEC)
	{
		if (scene->r.cfra >= smd->time)
			smokeModifier_init(smd, ob, scene, dm);

		if (smd->effec)
		{
			if (smd->effec->dm)
				smd->effec->dm->release(smd->effec->dm);

			smd->effec->dm = CDDM_copy(dm);
		}

		smd->time = scene->r.cfra;
		if (scene->r.cfra < smd->time)
		{
			smokeModifier_reset_ex(smd, false);
		}
	}
	else if (smd->type & MOD_SMOKE_TYPE_DOMAIN)
	{
		int startframe, endframe, framenr;
		framenr = scene->r.cfra;
		startframe = smd->domain->cache_frame_start;
		endframe = smd->domain->cache_frame_end;

		/* Baking can be any of geometry, data, mesh or particles */
		bool isBaking = (smd->domain->cache_flag & (FLUID_CACHE_BAKING_GEOMETRY|FLUID_CACHE_BAKING_LOW|FLUID_CACHE_BAKING_HIGH|
													FLUID_CACHE_BAKING_MESH_LOW|FLUID_CACHE_BAKING_MESH_HIGH|
													FLUID_CACHE_BAKING_PARTICLES_LOW|FLUID_CACHE_BAKING_PARTICLES_HIGH));

		if (isBaking) return;

		/* Reset fluid if no fluid present (obviously) or if timeline gets reset to startframe when no (!) baking is running */
		if (!smd->domain->fluid || (framenr == startframe && !isBaking))
			smokeModifier_reset_ex(smd, false);

		if (!smd->domain->fluid && (framenr != startframe) && (smd->domain->flags & MOD_SMOKE_FILE_LOAD) == 0)
			return;

		smd->domain->flags &= ~MOD_SMOKE_FILE_LOAD;
		CLAMP(framenr, startframe, endframe);

		/* If already viewing a pre/after frame, no need to reload */
		if ((smd->time == framenr) && (framenr != scene->r.cfra))
			return;

		if (smokeModifier_init(smd, ob, scene, dm) == 0)
			return;

		/* Try to read from cache */
		if (fluid_read_cache(smd->domain->fluid, smd, framenr)) {
			smd->time = framenr;
			return;
		}
		smd->time = scene->r.cfra;
	}
}

struct DerivedMesh *smokeModifier_do(SmokeModifierData *smd, Scene *scene, Object *ob, DerivedMesh *dm)
{
	/* lock so preview render does not read smoke data while it gets modified */
	if ((smd->type & MOD_SMOKE_TYPE_DOMAIN) && smd->domain)
		BLI_rw_mutex_lock(smd->domain->fluid_mutex, THREAD_LOCK_WRITE);

	smokeModifier_process(smd, scene, ob, dm);

	if ((smd->type & MOD_SMOKE_TYPE_DOMAIN) && smd->domain)
		BLI_rw_mutex_unlock(smd->domain->fluid_mutex);

	/* return generated geometry for adaptive domain */
	if (smd->type & MOD_SMOKE_TYPE_DOMAIN && smd->domain &&
	    smd->domain->flags & MOD_SMOKE_ADAPTIVE_DOMAIN &&
	    smd->domain->base_res[0])
	{
		return createDomainGeometry(smd->domain, ob);
	}
	else if (smd->type & MOD_SMOKE_TYPE_DOMAIN && smd->domain &&
		smd->domain->type == MOD_SMOKE_DOMAIN_TYPE_LIQUID)
	{
		DerivedMesh *result = createLiquidMesh(smd->domain, dm, ob);
		return (result) ? result : CDDM_copy(dm);
	}
	else {
		return CDDM_copy(dm);
	}
}

static float calc_voxel_transp(float *result, float *input, int res[3], int *pixel, float *tRay, float correct)
{
	const size_t index = smoke_get_index(pixel[0], res[0], pixel[1], res[1], pixel[2]);

	// T_ray *= T_vox
	*tRay *= expf(input[index] * correct);

	if (result[index] < 0.0f)
	{
		result[index] = *tRay;
	}

	return *tRay;
}

static void bresenham_linie_3D(int x1, int y1, int z1, int x2, int y2, int z2, float *tRay, bresenham_callback cb, float *result, float *input, int res[3], float correct)
{
	int dx, dy, dz, i, l, m, n, x_inc, y_inc, z_inc, err_1, err_2, dx2, dy2, dz2;
	int pixel[3];

	pixel[0] = x1;
	pixel[1] = y1;
	pixel[2] = z1;

	dx = x2 - x1;
	dy = y2 - y1;
	dz = z2 - z1;

	x_inc = (dx < 0) ? -1 : 1;
	l = abs(dx);
	y_inc = (dy < 0) ? -1 : 1;
	m = abs(dy);
	z_inc = (dz < 0) ? -1 : 1;
	n = abs(dz);
	dx2 = l << 1;
	dy2 = m << 1;
	dz2 = n << 1;

	if ((l >= m) && (l >= n)) {
		err_1 = dy2 - l;
		err_2 = dz2 - l;
		for (i = 0; i < l; i++) {
			if (cb(result, input, res, pixel, tRay, correct) <= FLT_EPSILON)
				break;
			if (err_1 > 0) {
				pixel[1] += y_inc;
				err_1 -= dx2;
			}
			if (err_2 > 0) {
				pixel[2] += z_inc;
				err_2 -= dx2;
			}
			err_1 += dy2;
			err_2 += dz2;
			pixel[0] += x_inc;
		}
	}
	else if ((m >= l) && (m >= n)) {
		err_1 = dx2 - m;
		err_2 = dz2 - m;
		for (i = 0; i < m; i++) {
			if (cb(result, input, res, pixel, tRay, correct) <= FLT_EPSILON)
				break;
			if (err_1 > 0) {
				pixel[0] += x_inc;
				err_1 -= dy2;
			}
			if (err_2 > 0) {
				pixel[2] += z_inc;
				err_2 -= dy2;
			}
			err_1 += dx2;
			err_2 += dz2;
			pixel[1] += y_inc;
		}
	}
	else {
		err_1 = dy2 - n;
		err_2 = dx2 - n;
		for (i = 0; i < n; i++) {
			if (cb(result, input, res, pixel, tRay, correct) <= FLT_EPSILON)
				break;
			if (err_1 > 0) {
				pixel[1] += y_inc;
				err_1 -= dz2;
			}
			if (err_2 > 0) {
				pixel[0] += x_inc;
				err_2 -= dz2;
			}
			err_1 += dy2;
			err_2 += dx2;
			pixel[2] += z_inc;
		}
	}
	cb(result, input, res, pixel, tRay, correct);
}

static void smoke_calc_transparency(SmokeDomainSettings *sds, Scene *scene)
{
	float bv[6] = {0};
	float light[3];
	int a, z, slabsize = sds->res[0] * sds->res[1], size = sds->res[0] * sds->res[1] * sds->res[2];
	float *density = smoke_get_density(sds->fluid);
	float *shadow = smoke_get_shadow(sds->fluid);
	float correct = -7.0f * sds->dx;

	if (!get_lamp(scene, light)) return;

	/* convert light pos to sim cell space */
	mul_m4_v3(sds->imat, light);
	light[0] = (light[0] - sds->p0[0]) / sds->cell_size[0] - 0.5f - (float)sds->res_min[0];
	light[1] = (light[1] - sds->p0[1]) / sds->cell_size[1] - 0.5f - (float)sds->res_min[1];
	light[2] = (light[2] - sds->p0[2]) / sds->cell_size[2] - 0.5f - (float)sds->res_min[2];

	for (a = 0; a < size; a++)
		shadow[a] = -1.0f;

	/* calculate domain bounds in sim cell space */
	// 0,2,4 = 0.0f
	bv[1] = (float)sds->res[0]; // x
	bv[3] = (float)sds->res[1]; // y
	bv[5] = (float)sds->res[2]; // z

	for (z = 0; z < sds->res[2]; z++)
	{
		size_t index = z * slabsize;
		int x, y;

		for (y = 0; y < sds->res[1]; y++)
			for (x = 0; x < sds->res[0]; x++, index++)
			{
				float voxelCenter[3];
				float pos[3];
				int cell[3];
				float tRay = 1.0;

				if (shadow[index] >= 0.0f)
					continue;
				voxelCenter[0] = (float)x;
				voxelCenter[1] = (float)y;
				voxelCenter[2] = (float)z;

				// get starting cell (light pos)
				if (BLI_bvhtree_bb_raycast(bv, light, voxelCenter, pos) > FLT_EPSILON)
				{
					// we're ouside -> use point on side of domain
					cell[0] = (int)floor(pos[0]);
					cell[1] = (int)floor(pos[1]);
					cell[2] = (int)floor(pos[2]);
				}
				else {
					// we're inside -> use light itself
					cell[0] = (int)floor(light[0]);
					cell[1] = (int)floor(light[1]);
					cell[2] = (int)floor(light[2]);
				}
				/* clamp within grid bounds */
				CLAMP(cell[0], 0, sds->res[0] - 1);
				CLAMP(cell[1], 0, sds->res[1] - 1);
				CLAMP(cell[2], 0, sds->res[2] - 1);

				bresenham_linie_3D(cell[0], cell[1], cell[2], x, y, z, &tRay, calc_voxel_transp, shadow, density, sds->res, correct);

				// convention -> from a RGBA float array, use G value for tRay
				shadow[index] = tRay;
			}
	}
}

int smoke_step(Scene *scene, Object *ob, SmokeModifierData *smd, int frame)
{
	SmokeDomainSettings *sds = smd->domain;
	DerivedMesh *domain_dm = ob->derivedDeform;
	float fps = scene->r.frs_sec / scene->r.frs_sec_base;
	float dt;
	float sdt; // dt after adapted timestep
	float time_per_frame;

	/* TODO (sebbas): Move dissolve smoke code to mantaflow */
	if (sds->flags & MOD_SMOKE_DISSOLVE) {
		smoke_dissolve(sds->fluid, sds->diss_speed, sds->flags & MOD_SMOKE_DISSOLVE_LOG);
		if (sds->fluid && sds->flags & MOD_SMOKE_HIGHRES) {
			smoke_dissolve_wavelet(sds->fluid, sds->diss_speed, sds->flags & MOD_SMOKE_DISSOLVE_LOG);
		}
	}

	/* update object state */
	invert_m4_m4(sds->imat, ob->obmat);
	copy_m4_m4(sds->obmat, ob->obmat);
	smoke_set_domain_from_derivedmesh(sds, ob, domain_dm, (sds->flags & MOD_SMOKE_ADAPTIVE_DOMAIN) != 0);

	/* adapt timestep for different framerates, dt = 0.1 is at 25fps */
	dt = DT_DEFAULT * (25.0f / fps);

	time_per_frame = 0;

	// loop as long as time_per_frame (sum of sudivdt) does not exceed dt (actual framelength)
	while (time_per_frame < dt)
	{
		fluid_update_variables_low(sds->fluid, smd);
		fluid_adapt_timestep(sds->fluid);
		sdt = fluid_get_timestep(sds->fluid);
		time_per_frame += sdt;

		// Calculate inflow geometry
		update_flowsfluids(scene, ob, sds, time_per_frame, dt, frame);

		// Calculate obstacle geometry
		update_obstacles(scene, ob, sds, sdt);

		if (sds->total_cells > 1) {
			update_effectors(scene, ob, sds, sdt); // DG TODO? problem --> uses forces instead of velocity, need to check how they need to be changed with variable dt
			fluid_bake_low(sds->fluid, smd, frame);
		}

		if (sds->type == MOD_SMOKE_DOMAIN_TYPE_GAS) {
			smoke_calc_transparency(sds, scene);
		}
	}
	/* Write call currently only writes shadow grid */
	return fluid_write_cache(sds->fluid, smd, frame);
}

/* get smoke velocity and density at given coordinates
 *  returns fluid density or -1.0f if outside domain*/
float smoke_get_velocity_at(struct Object *ob, float position[3], float velocity[3])
{
	SmokeModifierData *smd = (SmokeModifierData *)modifiers_findByType(ob, eModifierType_Smoke);
	zero_v3(velocity);

	if (smd && (smd->type & MOD_SMOKE_TYPE_DOMAIN) && smd->domain && smd->domain->fluid) {
		SmokeDomainSettings *sds = smd->domain;
		float time_mult = 25.f * DT_DEFAULT;
		float vel_mag;
		float *velX = smoke_get_velocity_x(sds->fluid);
		float *velY = smoke_get_velocity_y(sds->fluid);
		float *velZ = smoke_get_velocity_z(sds->fluid);
		float density = 0.0f, fuel = 0.0f;
		float pos[3];
		copy_v3_v3(pos, position);
		smoke_pos_to_cell(sds, pos);

		/* check if point is outside domain max bounds */
		if (pos[0] < sds->res_min[0] || pos[1] < sds->res_min[1] || pos[2] < sds->res_min[2]) return -1.0f;
		if (pos[0] > sds->res_max[0] || pos[1] > sds->res_max[1] || pos[2] > sds->res_max[2]) return -1.0f;

		/* map pos between 0.0 - 1.0 */
		pos[0] = (pos[0] - sds->res_min[0]) / ((float)sds->res[0]);
		pos[1] = (pos[1] - sds->res_min[1]) / ((float)sds->res[1]);
		pos[2] = (pos[2] - sds->res_min[2]) / ((float)sds->res[2]);


		/* check if point is outside active area */
		if (smd->domain->flags & MOD_SMOKE_ADAPTIVE_DOMAIN) {
			if (pos[0] < 0.0f || pos[1] < 0.0f || pos[2] < 0.0f) return 0.0f;
			if (pos[0] > 1.0f || pos[1] > 1.0f || pos[2] > 1.0f) return 0.0f;
		}

		/* get interpolated velocity */
		velocity[0] = BLI_voxel_sample_trilinear(velX, sds->res, pos) * sds->global_size[0] * time_mult;
		velocity[1] = BLI_voxel_sample_trilinear(velY, sds->res, pos) * sds->global_size[1] * time_mult;
		velocity[2] = BLI_voxel_sample_trilinear(velZ, sds->res, pos) * sds->global_size[2] * time_mult;

		/* convert velocity direction to global space */
		vel_mag = len_v3(velocity);
		mul_mat3_m4_v3(sds->obmat, velocity);
		normalize_v3(velocity);
		mul_v3_fl(velocity, vel_mag);

		/* use max value of fuel or smoke density */
		density = BLI_voxel_sample_trilinear(smoke_get_density(sds->fluid), sds->res, pos);
		if (smoke_has_fuel(sds->fluid)) {
			fuel = BLI_voxel_sample_trilinear(smoke_get_fuel(sds->fluid), sds->res, pos);
		}
		return MAX2(density, fuel);
	}
	return -1.0f;
}

int smoke_get_data_flags(SmokeDomainSettings *sds)
{
	int flags = 0;

	if (sds->fluid) {
		if (smoke_has_heat(sds->fluid))
			flags |= SM_ACTIVE_HEAT;
		if (smoke_has_fuel(sds->fluid))
			flags |= SM_ACTIVE_FIRE;
		if (smoke_has_colors(sds->fluid))
			flags |= SM_ACTIVE_COLORS;
	}

	return flags;
}

#endif /* WITH_MANTA */
