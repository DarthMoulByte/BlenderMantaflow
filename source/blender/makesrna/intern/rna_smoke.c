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
 * Contributor(s): Daniel Genrich
 *                 Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/makesrna/intern/rna_smoke.c
 *  \ingroup RNA
 */


#include <stdlib.h>
#include <limits.h>

#include "BLI_sys_types.h"
#include "BLI_threads.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "BKE_modifier.h"
#include "BKE_smoke.h"
#include "BKE_pointcache.h"
#include "BKE_object.h"

#include "DNA_modifier_types.h"
#include "DNA_object_force_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_smoke_types.h"

#include "WM_types.h"
#include "WM_api.h"

#ifdef RNA_RUNTIME

#include "BKE_colorband.h"
#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_particle.h"

#ifdef WITH_MANTA
#	include "manta_fluid_API.h"
#endif

static void rna_Smoke_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	DAG_id_tag_update(ptr->id.data, OB_RECALC_DATA);

	// Needed for liquid domain objects
	Object *ob = ptr->id.data;
	DAG_id_tag_update(ptr->id.data, OB_RECALC_DATA);
	WM_main_add_notifier(NC_OBJECT | ND_DRAW, ob);
}

static void rna_Smoke_dependency_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	rna_Smoke_update(bmain, scene, ptr);
	DAG_relations_tag_update(bmain);
}

static void rna_Smoke_resetCache(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;
	if (settings->smd && settings->smd->domain)
		settings->point_cache[0]->flag |= PTCACHE_OUTDATED;
	DAG_id_tag_update(ptr->id.data, OB_RECALC_DATA);
}

static void rna_Smoke_viewport_set(struct PointerRNA *ptr, int value)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;
	Object *ob = (Object *)ptr->id.data;
	ModifierData *md;
	float framenr;
	bool can_simulate;
	PTCacheID pid;

	/* Reload cache if viewport type changed */
	if (value != settings->viewport_display_mode) {
		md = ((ModifierData*) settings->smd);
		framenr = md->scene->r.cfra;
		can_simulate = (framenr == (int)settings->smd->time + 1) && (framenr == md->scene->r.cfra);

		BKE_ptcache_id_from_smoke(&pid, ob, settings->smd);
		BKE_ptcache_read(&pid, framenr, can_simulate);

		settings->viewport_display_mode = value;
	}
}

static void rna_Smoke_parts_create(PointerRNA *ptr, char *pset_name, char* parts_name, char* psys_name, int psys_type)
{
	Object *ob = (Object *)ptr->id.data;
	ParticleSystemModifierData *psmd;
	ParticleSystem *psys;
	ParticleSettings *part;

	/* add particle system */
	part = BKE_particlesettings_add(NULL, pset_name);
	psys = MEM_callocN(sizeof(ParticleSystem), "particle_system");

	part->type = psys_type;
	part->totpart = 0;
	psys->part = part;
	psys->pointcache = BKE_ptcache_add(&psys->ptcaches);
	BLI_strncpy(psys->name, parts_name, sizeof(psys->name));
	BLI_addtail(&ob->particlesystem, psys);

	/* add modifier */
	psmd = (ParticleSystemModifierData *)modifier_new(eModifierType_ParticleSystem);
	BLI_strncpy(psmd->modifier.name, psys_name, sizeof(psmd->modifier.name));
	psmd->psys = psys;
	BLI_addtail(&ob->modifiers, psmd);
	modifier_unique_name(&ob->modifiers, (ModifierData *)psmd);
}

static void rna_Smoke_parts_delete(PointerRNA *ptr, int ptype)
{
	Object *ob = (Object *)ptr->id.data;
	ParticleSystemModifierData *psmd;
	ParticleSystem *psys, *next_psys;

	for (psys = ob->particlesystem.first; psys; psys = next_psys) {
		next_psys = psys->next;
		if (psys->part->type == ptype) {
			/* clear modifier */
			psmd = psys_get_modifier(ob, psys);
			BLI_remlink(&ob->modifiers, psmd);
			modifier_free((ModifierData *)psmd);

			/* clear particle system */
			BLI_remlink(&ob->particlesystem, psys);
			psys_free(ob, psys);
		}
	}
}

static bool rna_Smoke_parts_exists(PointerRNA *ptr, int ptype)
{
	Object *ob = (Object *)ptr->id.data;
	ParticleSystem *psys;

	for (psys = ob->particlesystem.first; psys; psys = psys->next) {
		if (psys->part->type == ptype) return true;
	}
	return false;
}

static void rna_Smoke_draw_type_update(Main *UNUSED(bmain), Scene *UNUSED(scene), struct PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;

	/* Wireframe mode more convenient when particles present */
	if (settings->particle_type == 0) {
		ob->dt = OB_SOLID;
	} else {
		ob->dt = OB_WIRE;
	}
}

static void rna_Smoke_flip_parts_set(struct PointerRNA *ptr, int value)
{
	Object *ob = (Object *)ptr->id.data;
	SmokeModifierData *smd;
	smd = (SmokeModifierData *)modifiers_findByType(ob, eModifierType_Smoke);
	bool exists = rna_Smoke_parts_exists(ptr, PART_MANTA_FLIP);

	if (value) {
		if (ob->type == OB_MESH && !exists)
			rna_Smoke_parts_create(ptr, "FlipParticleSettings", "FLIP Particles", "FLIP Particle System", PART_MANTA_FLIP);
		smd->domain->particle_type |= MOD_SMOKE_PARTICLE_FLIP;
	}
	else {
		rna_Smoke_parts_delete(ptr, PART_MANTA_FLIP);
		rna_Smoke_resetCache(NULL, NULL, ptr);

		smd->domain->particle_type &= ~MOD_SMOKE_PARTICLE_FLIP;
	}
	rna_Smoke_draw_type_update(NULL, NULL, ptr);
}

static void rna_Smoke_spray_parts_set(struct PointerRNA *ptr, int value)
{
	// TODO (Georg Kohl) finish combined export
	Object *ob = (Object *)ptr->id.data;
	SmokeModifierData *smd;
	smd = (SmokeModifierData *)modifiers_findByType(ob, eModifierType_Smoke);
	bool exists = rna_Smoke_parts_exists(ptr, PART_MANTA_SPRAY);

	if (value) {
		if (ob->type == OB_MESH && !exists)
			rna_Smoke_parts_create(ptr, "SprayParticleSettings", "Spray Particles", "Spray Particle System", PART_MANTA_SPRAY);
		smd->domain->particle_type |= MOD_SMOKE_PARTICLE_SPRAY;
	}
	else {
		rna_Smoke_parts_delete(ptr, PART_MANTA_SPRAY);
		rna_Smoke_resetCache(NULL, NULL, ptr);

		smd->domain->particle_type &= ~MOD_SMOKE_PARTICLE_SPRAY;
	}
	rna_Smoke_draw_type_update(NULL, NULL, ptr);
}

static void rna_Smoke_bubble_parts_set(struct PointerRNA *ptr, int value)
{
	Object *ob = (Object *)ptr->id.data;
	SmokeModifierData *smd;
	smd = (SmokeModifierData *)modifiers_findByType(ob, eModifierType_Smoke);
	bool exists = rna_Smoke_parts_exists(ptr, PART_MANTA_BUBBLE);

	if (value) {
		if (ob->type == OB_MESH && !exists)
		rna_Smoke_parts_create(ptr, "BubbleParticleSettings", "Bubble Particles", "Bubble Particle System", PART_MANTA_BUBBLE);
		smd->domain->particle_type |= MOD_SMOKE_PARTICLE_BUBBLE;
	}
	else {
		rna_Smoke_parts_delete(ptr, PART_MANTA_BUBBLE);
		rna_Smoke_resetCache(NULL, NULL, ptr);

		smd->domain->particle_type &= ~MOD_SMOKE_PARTICLE_BUBBLE;
	}
	rna_Smoke_draw_type_update(NULL, NULL, ptr);
}

static void rna_Smoke_foam_parts_set(struct PointerRNA *ptr, int value)
{
	Object *ob = (Object *)ptr->id.data;
	SmokeModifierData *smd;
	smd = (SmokeModifierData *)modifiers_findByType(ob, eModifierType_Smoke);
	bool exists = rna_Smoke_parts_exists(ptr, PART_MANTA_FOAM);

	if (value) {
		if (ob->type == OB_MESH && !exists)
		rna_Smoke_parts_create(ptr, "FoamParticleSettings", "Foam Particles", "Foam Particle System", PART_MANTA_FOAM);
		smd->domain->particle_type |= MOD_SMOKE_PARTICLE_FOAM;
	}
	else {
		rna_Smoke_parts_delete(ptr, PART_MANTA_FOAM);
		rna_Smoke_resetCache(NULL, NULL, ptr);

		smd->domain->particle_type &= ~MOD_SMOKE_PARTICLE_FOAM;
	}
	rna_Smoke_draw_type_update(NULL, NULL, ptr);
}

static void rna_Smoke_tracer_parts_set(struct PointerRNA *ptr, int value)
{
	Object *ob = (Object *)ptr->id.data;
	SmokeModifierData *smd;
	smd = (SmokeModifierData *)modifiers_findByType(ob, eModifierType_Smoke);
	bool exists = rna_Smoke_parts_exists(ptr, PART_MANTA_TRACER);

	if (value) {
		if (ob->type == OB_MESH && !exists)
		rna_Smoke_parts_create(ptr, "TracerParticleSettings", "Tracer Particles", "Tracer Particle System", PART_MANTA_TRACER);
		smd->domain->particle_type |= MOD_SMOKE_PARTICLE_TRACER;
	}
	else {
		rna_Smoke_parts_delete(ptr, PART_MANTA_TRACER);
		rna_Smoke_resetCache(NULL, NULL, ptr);

		smd->domain->particle_type &= ~MOD_SMOKE_PARTICLE_TRACER;
	}
	rna_Smoke_draw_type_update(NULL, NULL, ptr);
}

static void rna_Smoke_use_surface_format_set(struct PointerRNA *ptr, int value)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;

	if (value == 1) {
		settings->flags |= MOD_SMOKE_USE_SURFACE_CACHE;
	}
	else {
		settings->flags &= ~MOD_SMOKE_USE_SURFACE_CACHE;
	}
}

static void rna_Smoke_use_volume_format_set(struct PointerRNA *ptr, int value)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;

	if (value == 1) {
		settings->flags |= MOD_SMOKE_USE_VOLUME_CACHE;
	}
	else {
		settings->flags &= ~MOD_SMOKE_USE_VOLUME_CACHE;
	}
}

static void rna_Smoke_cachetype_surface_set(struct PointerRNA *ptr, int value)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;
	Object *ob = (Object *)ptr->id.data;

	if (value != settings->cache_surface_format) {
		/* Clear old caches. */
		PTCacheID id;
		BKE_ptcache_id_from_smoke(&id, ob, settings->smd);
		BKE_ptcache_id_clear(&id, PTCACHE_CLEAR_ALL, 0);

		settings->cache_surface_format = value;
	}
}

static void rna_Smoke_cachetype_volume_set(struct PointerRNA *ptr, int value)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;
	Object *ob = (Object *)ptr->id.data;

	if (value != settings->cache_volume_format) {
		/* Clear old caches. */
		PTCacheID id;
		BKE_ptcache_id_from_smoke(&id, ob, settings->smd);
		BKE_ptcache_id_clear(&id, PTCACHE_CLEAR_ALL, 0);

		settings->cache_volume_format = value;
	}
}

static EnumPropertyItem *rna_Smoke_cachetype_surface_itemf(
        bContext *UNUSED(C), PointerRNA *ptr, PropertyRNA *UNUSED(prop), bool *r_free)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;

	EnumPropertyItem *item = NULL;
	EnumPropertyItem tmp = {0, "", 0, "", ""};
	int totitem = 0;

	if (settings->type == MOD_SMOKE_DOMAIN_TYPE_GAS)
	{
		tmp.value = PTCACHE_FILE_OBJECT;
		tmp.identifier = "OBJECT";
		tmp.name = "Object files";
		tmp.description = "Binary object file format";
		RNA_enum_item_add(&item, &totitem, &tmp);
	}
	else if (settings->type == MOD_SMOKE_DOMAIN_TYPE_LIQUID)
	{
		tmp.value = PTCACHE_FILE_OBJECT;
		tmp.identifier = "OBJECT";
		tmp.name = "Object files";
		tmp.description = "Binary object file format";
		RNA_enum_item_add(&item, &totitem, &tmp);
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

static EnumPropertyItem *rna_Smoke_cachetype_volume_itemf(
        bContext *UNUSED(C), PointerRNA *ptr, PropertyRNA *UNUSED(prop), bool *r_free)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;

	EnumPropertyItem *item = NULL;
	EnumPropertyItem tmp = {0, "", 0, "", ""};
	int totitem = 0;

	if (settings->type == MOD_SMOKE_DOMAIN_TYPE_GAS)
	{
		tmp.value = PTCACHE_FILE_PTCACHE;
		tmp.identifier = "POINTCACHE";
		tmp.name = "Point Cache";
		tmp.description = "Blender specific point cache file format";
		RNA_enum_item_add(&item, &totitem, &tmp);

#ifdef WITH_OPENVDB
		tmp.value = PTCACHE_FILE_OPENVDB;
		tmp.identifier = "OPENVDB";
		tmp.name = "OpenVDB";
		tmp.description = "OpenVDB file format";
		RNA_enum_item_add(&item, &totitem, &tmp);
#endif
	}
	else if (settings->type == MOD_SMOKE_DOMAIN_TYPE_LIQUID)
	{
		tmp.value = PTCACHE_FILE_PTCACHE;
		tmp.identifier = "POINTCACHE";
		tmp.name = "Point Cache";
		tmp.description = "Blender specific point cache file format";
		RNA_enum_item_add(&item, &totitem, &tmp);

//		tmp.value = PTCACHE_FILE_UNI;
//		tmp.identifier = "UNI";
//		tmp.name = "Uni files";
//		tmp.description = "Uni file format";
//		RNA_enum_item_add(&item, &totitem, &tmp);
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;
	
	return item;
}

static void rna_Smoke_collisionextents_set(struct PointerRNA *ptr, int value, bool clear)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;
	if (clear) {
		settings->border_collisions &= value;
	}
	else {
		settings->border_collisions |= value;
	}
}

static void rna_Smoke_domaintype_set(struct PointerRNA *ptr, int value)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;
	Object *ob = (Object *)ptr->id.data;
	
	if (value != settings->type) {
		/* Set common values for liquid/smoke domain: cache type, border collision and viewport drawtype. */
		if (value == MOD_SMOKE_DOMAIN_TYPE_GAS)
		{
			rna_Smoke_use_surface_format_set(ptr, 0);
			rna_Smoke_use_volume_format_set(ptr, 1);
			rna_Smoke_cachetype_surface_set(ptr, PTCACHE_FILE_OBJECT);
			rna_Smoke_cachetype_volume_set(ptr, PTCACHE_FILE_PTCACHE);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_FRONT, 1);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_BACK, 1);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_RIGHT, 1);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_LEFT, 1);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_TOP, 1);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_BOTTOM, 1);
			BKE_object_draw_type_set(ob, OB_WIRE);
		}
		else if (value == MOD_SMOKE_DOMAIN_TYPE_LIQUID)
		{
			rna_Smoke_use_surface_format_set(ptr, 1);
			rna_Smoke_use_volume_format_set(ptr, 0);
			rna_Smoke_cachetype_surface_set(ptr, PTCACHE_FILE_OBJECT);
			rna_Smoke_cachetype_volume_set(ptr, PTCACHE_FILE_PTCACHE);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_FRONT, 0);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_BACK, 0);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_RIGHT, 0);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_LEFT, 0);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_TOP, 0);
			rna_Smoke_collisionextents_set(ptr, MOD_SMOKE_BORDER_BOTTOM, 0);
			BKE_object_draw_type_set(ob, OB_SOLID);
		}

		/* Set actual domain type */
		settings->type = value;
	}
}

static void rna_Smoke_reset(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;

	smokeModifier_reset(settings->smd);
	rna_Smoke_resetCache(bmain, scene, ptr);

	rna_Smoke_update(bmain, scene, ptr);
}

static void rna_Smoke_reset_dependency(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;

	smokeModifier_reset(settings->smd);

	if (settings->smd && settings->smd->domain)
		settings->smd->domain->point_cache[0]->flag |= PTCACHE_OUTDATED;

	rna_Smoke_dependency_update(bmain, scene, ptr);
}

static char *rna_SmokeDomainSettings_path(PointerRNA *ptr)
{
	SmokeDomainSettings *settings = (SmokeDomainSettings *)ptr->data;
	ModifierData *md = (ModifierData *)settings->smd;
	char name_esc[sizeof(md->name) * 2];

	BLI_strescape(name_esc, md->name, sizeof(name_esc));
	return BLI_sprintfN("modifiers[\"%s\"].domain_settings", name_esc);
}

static char *rna_SmokeFlowSettings_path(PointerRNA *ptr)
{
	SmokeFlowSettings *settings = (SmokeFlowSettings *)ptr->data;
	ModifierData *md = (ModifierData *)settings->smd;
	char name_esc[sizeof(md->name) * 2];

	BLI_strescape(name_esc, md->name, sizeof(name_esc));
	return BLI_sprintfN("modifiers[\"%s\"].flow_settings", name_esc);
}

static char *rna_SmokeCollSettings_path(PointerRNA *ptr)
{
	SmokeCollSettings *settings = (SmokeCollSettings *)ptr->data;
	ModifierData *md = (ModifierData *)settings->smd;
	char name_esc[sizeof(md->name) * 2];

	BLI_strescape(name_esc, md->name, sizeof(name_esc));
	return BLI_sprintfN("modifiers[\"%s\"].effec_settings", name_esc);
}

static int rna_SmokeModifier_grid_get_length(PointerRNA *ptr, int length[RNA_MAX_ARRAY_DIMENSION])
{
#ifdef WITH_MANTA
	SmokeDomainSettings *sds = (SmokeDomainSettings *)ptr->data;
	float *density = NULL;
	int size = 0;

	if (sds->flags & MOD_SMOKE_HIGHRES && sds->fluid) {
		/* high resolution smoke */
		int res[3];

		smoke_turbulence_get_res(sds->fluid, res);
		size = res[0] * res[1] * res[2];

		density = smoke_turbulence_get_density(sds->fluid);
	}
	else if (sds->fluid) {
		/* regular resolution */
		size = sds->res[0] * sds->res[1] * sds->res[2];
		density = smoke_get_density(sds->fluid);
	}

	length[0] = (density) ? size : 0;
#else
	(void)ptr;
	length[0] = 0;
#endif
	return length[0];
}

static int rna_SmokeModifier_color_grid_get_length(PointerRNA *ptr, int length[RNA_MAX_ARRAY_DIMENSION])
{
	rna_SmokeModifier_grid_get_length(ptr, length);

	length[0] *= 4;
	return length[0];
}

static int rna_SmokeModifier_velocity_grid_get_length(PointerRNA *ptr, int length[RNA_MAX_ARRAY_DIMENSION])
{
#ifdef WITH_MANTA
	SmokeDomainSettings *sds = (SmokeDomainSettings *)ptr->data;
	float *vx = NULL;
	float *vy = NULL;
	float *vz = NULL;
	int size = 0;

	/* Velocity data is always low-resolution. */
	if (sds->fluid) {
		size = 3 * sds->res[0] * sds->res[1] * sds->res[2];
		vx = smoke_get_velocity_x(sds->fluid);
		vy = smoke_get_velocity_y(sds->fluid);
		vz = smoke_get_velocity_z(sds->fluid);
	}

	length[0] = (vx && vy && vz) ? size : 0;
#else
	(void)ptr;
	length[0] = 0;
#endif
	return length[0];
}

static int rna_SmokeModifier_heat_grid_get_length(
        PointerRNA *ptr,
        int length[RNA_MAX_ARRAY_DIMENSION])
{
#ifdef WITH_MANTA
	SmokeDomainSettings *sds = (SmokeDomainSettings *)ptr->data;
	float *heat = NULL;
	int size = 0;

	/* Heat data is always low-resolution. */
	if (sds->fluid) {
		size = sds->res[0] * sds->res[1] * sds->res[2];
		heat = smoke_get_heat(sds->fluid);
	}

	length[0] = (heat) ? size : 0;
#else
	(void)ptr;
	length[0] = 0;
#endif
	return length[0];
}

static void rna_SmokeModifier_density_grid_get(PointerRNA *ptr, float *values)
{
#ifdef WITH_MANTA
	SmokeDomainSettings *sds = (SmokeDomainSettings *)ptr->data;
	int length[RNA_MAX_ARRAY_DIMENSION];
	int size = rna_SmokeModifier_grid_get_length(ptr, length);
	float *density;

	BLI_rw_mutex_lock(sds->fluid_mutex, THREAD_LOCK_READ);
	
	if (sds->flags & MOD_SMOKE_HIGHRES && sds->fluid)
		density = smoke_turbulence_get_density(sds->fluid);
	else
		density = smoke_get_density(sds->fluid);

	memcpy(values, density, size * sizeof(float));

	BLI_rw_mutex_unlock(sds->fluid_mutex);
#else
	UNUSED_VARS(ptr, values);
#endif
}

static void rna_SmokeModifier_velocity_grid_get(PointerRNA *ptr, float *values)
{
#ifdef WITH_MANTA
	SmokeDomainSettings *sds = (SmokeDomainSettings *)ptr->data;
	int length[RNA_MAX_ARRAY_DIMENSION];
	int size = rna_SmokeModifier_velocity_grid_get_length(ptr, length);
	float *vx, *vy, *vz;
	int i;

	BLI_rw_mutex_lock(sds->fluid_mutex, THREAD_LOCK_READ);

	vx = smoke_get_velocity_x(sds->fluid);
	vy = smoke_get_velocity_y(sds->fluid);
	vz = smoke_get_velocity_z(sds->fluid);

	for (i = 0; i < size; i += 3) {
		*(values++) = *(vx++);
		*(values++) = *(vy++);
		*(values++) = *(vz++);
	}

	BLI_rw_mutex_unlock(sds->fluid_mutex);
#else
	UNUSED_VARS(ptr, values);
#endif
}

static void rna_SmokeModifier_color_grid_get(PointerRNA *ptr, float *values)
{
#ifdef WITH_MANTA
	SmokeDomainSettings *sds = (SmokeDomainSettings *)ptr->data;

	BLI_rw_mutex_lock(sds->fluid_mutex, THREAD_LOCK_READ);

	if (sds->flags & MOD_SMOKE_HIGHRES) {
		if (smoke_turbulence_has_colors(sds->fluid))
			smoke_turbulence_get_rgba(sds->fluid, values, 0);
		else
			smoke_turbulence_get_rgba_from_density(sds->fluid, sds->active_color, values, 0);
	}
	else {
		if (smoke_has_colors(sds->fluid))
			smoke_get_rgba(sds->fluid, values, 0);
		else
			smoke_get_rgba_from_density(sds->fluid, sds->active_color, values, 0);
	}

	BLI_rw_mutex_unlock(sds->fluid_mutex);
#else
	UNUSED_VARS(ptr, values);
#endif
}

static void rna_SmokeModifier_flame_grid_get(PointerRNA *ptr, float *values)
{
#ifdef WITH_MANTA
	SmokeDomainSettings *sds = (SmokeDomainSettings *)ptr->data;
	int length[RNA_MAX_ARRAY_DIMENSION];
	int size = rna_SmokeModifier_grid_get_length(ptr, length);
	float *flame;

	BLI_rw_mutex_lock(sds->fluid_mutex, THREAD_LOCK_READ);

	if (sds->flags & MOD_SMOKE_HIGHRES && sds->fluid)
		flame = smoke_turbulence_get_flame(sds->fluid);
	else
		flame = smoke_get_flame(sds->fluid);
	
	if (flame)
		memcpy(values, flame, size * sizeof(float));
	else
		memset(values, 0, size * sizeof(float));

	BLI_rw_mutex_unlock(sds->fluid_mutex);
#else
	UNUSED_VARS(ptr, values);
#endif
}

static void rna_SmokeModifier_heat_grid_get(PointerRNA *ptr, float *values)
{
#ifdef WITH_MANTA
	SmokeDomainSettings *sds = (SmokeDomainSettings *)ptr->data;
	int length[RNA_MAX_ARRAY_DIMENSION];
	int size = rna_SmokeModifier_heat_grid_get_length(ptr, length);
	float *heat;

	BLI_rw_mutex_lock(sds->fluid_mutex, THREAD_LOCK_READ);

	heat = smoke_get_heat(sds->fluid);

	if (heat != NULL) {
		/* scale heat values from -2.0-2.0 to -1.0-1.0. */
		for (int i = 0; i < size; i++) {
			values[i] = heat[i] * 0.5f;
		}
	}
	else {
		memset(values, 0, size * sizeof(float));
	}

	BLI_rw_mutex_unlock(sds->fluid_mutex);
#else
	UNUSED_VARS(ptr, values);
#endif
}

static void rna_SmokeModifier_temperature_grid_get(PointerRNA *ptr, float *values)
{
#ifdef WITH_MANTA
	SmokeDomainSettings *sds = (SmokeDomainSettings *)ptr->data;
	int length[RNA_MAX_ARRAY_DIMENSION];
	int size = rna_SmokeModifier_grid_get_length(ptr, length);
	float *flame;

	BLI_rw_mutex_lock(sds->fluid_mutex, THREAD_LOCK_READ);

	if (sds->flags & MOD_SMOKE_HIGHRES && sds->fluid) {
		flame = smoke_turbulence_get_flame(sds->fluid);
	}
	else {
		flame = smoke_get_flame(sds->fluid);
	}

	if (flame) {
		/* Output is such that 0..1 maps to 0..1000K */
		float offset = sds->flame_ignition;
		float scale = sds->flame_max_temp - sds->flame_ignition;

		for (int i = 0; i < size; i++) {
			values[i] = (flame[i] > 0.01f) ? offset + flame[i] * scale : 0.0f;
		}
	}
	else {
		memset(values, 0, size * sizeof(float));
	}

	BLI_rw_mutex_unlock(sds->fluid_mutex);
#else
	UNUSED_VARS(ptr, values);
#endif
}

static void rna_SmokeFlow_density_vgroup_get(PointerRNA *ptr, char *value)
{
	SmokeFlowSettings *flow = (SmokeFlowSettings *)ptr->data;
	rna_object_vgroup_name_index_get(ptr, value, flow->vgroup_density);
}

static int rna_SmokeFlow_density_vgroup_length(PointerRNA *ptr)
{
	SmokeFlowSettings *flow = (SmokeFlowSettings *)ptr->data;
	return rna_object_vgroup_name_index_length(ptr, flow->vgroup_density);
}

static void rna_SmokeFlow_density_vgroup_set(PointerRNA *ptr, const char *value)
{
	SmokeFlowSettings *flow = (SmokeFlowSettings *)ptr->data;
	rna_object_vgroup_name_index_set(ptr, value, &flow->vgroup_density);
}

static void rna_SmokeFlow_uvlayer_set(PointerRNA *ptr, const char *value)
{
	SmokeFlowSettings *flow = (SmokeFlowSettings *)ptr->data;
	rna_object_uvlayer_name_set(ptr, value, flow->uvlayer_name, sizeof(flow->uvlayer_name));
}

static void rna_Smoke_use_color_ramp_set(PointerRNA *ptr, int value)
{
	SmokeDomainSettings *sds = (SmokeDomainSettings *)ptr->data;

	sds->use_coba = value;

	if (value && sds->coba == NULL) {
		sds->coba = BKE_colorband_add(false);
	}
}

static void rna_Smoke_flowsource_set(struct PointerRNA *ptr, int value)
{
	SmokeFlowSettings *settings = (SmokeFlowSettings *)ptr->data;

	if (value != settings->source) {
		settings->source = value;
	}
}

static EnumPropertyItem *rna_Smoke_flowsource_itemf(bContext *UNUSED(C), PointerRNA *ptr, PropertyRNA *UNUSED(prop), bool *r_free)
{
	SmokeFlowSettings *settings = (SmokeFlowSettings *)ptr->data;

	EnumPropertyItem *item = NULL;
	EnumPropertyItem tmp = {0, "", 0, "", ""};
	int totitem = 0;

	tmp.value = MOD_SMOKE_FLOW_SOURCE_MESH;
	tmp.identifier = "MESH";
	tmp.icon = ICON_META_CUBE;
	tmp.name = "Mesh";
	tmp.description = "Emit fluid from mesh surface or volume";
	RNA_enum_item_add(&item, &totitem, &tmp);

	if (settings->type != MOD_SMOKE_FLOW_TYPE_LIQUID)
	{
		tmp.value = MOD_SMOKE_FLOW_SOURCE_PARTICLES;
		tmp.identifier = "PARTICLES";
		tmp.icon = ICON_PARTICLES;
		tmp.name = "Particle System";
		tmp.description = "Emit smoke from particles";
		RNA_enum_item_add(&item, &totitem, &tmp);
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

static void rna_Smoke_flowtype_set(struct PointerRNA *ptr, int value)
{
	SmokeFlowSettings *settings = (SmokeFlowSettings *)ptr->data;

	if (value != settings->type) {
		settings->type = value;

		/* Force flow source to mesh */
		if (value == MOD_SMOKE_FLOW_TYPE_LIQUID) {
			rna_Smoke_flowsource_set(ptr, MOD_SMOKE_FLOW_SOURCE_MESH);
			settings->surface_distance = 0.5f;
		} else {
			settings->surface_distance = 1.5f;
		}
	}
}

#else

static void rna_def_smoke_domain_settings(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	static EnumPropertyItem smoke_domain_types[] = {
		{MOD_SMOKE_DOMAIN_TYPE_GAS, "GAS", 0, "Gas", "Create domain for gases"},
		{MOD_SMOKE_DOMAIN_TYPE_LIQUID, "LIQUID", 0, "Liquid", "Create domain for liquids"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem prop_noise_type_items[] = {
		{MOD_SMOKE_NOISEWAVE, "NOISEWAVE", 0, "Wavelet", ""},
#ifdef WITH_FFTW3
#ifndef WITH_MANTA
		{MOD_SMOKE_NOISEFFT, "NOISEFFT", 0, "FFT", ""},
#endif
#endif
		/*  {MOD_SMOKE_NOISECURL, "NOISECURL", 0, "Curl", ""}, */
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem prop_compression_items[] = {
		{ VDB_COMPRESSION_ZIP, "ZIP", 0, "Zip", "Effective but slow compression" },
#ifdef WITH_OPENVDB_BLOSC
		{ VDB_COMPRESSION_BLOSC, "BLOSC", 0, "Blosc", "Multithreaded compression, similar in size and quality as 'Zip'" },
#endif
		{ VDB_COMPRESSION_NONE, "NONE", 0, "None", "Do not use any compression" },
		{ 0, NULL, 0, NULL, NULL }
	};

	static const EnumPropertyItem smoke_cache_comp_items[] = {
		{SM_CACHE_LIGHT, "CACHELIGHT", 0, "Light", "Fast but not so effective compression"},
		{SM_CACHE_HEAVY, "CACHEHEAVY", 0, "Heavy", "Effective but slow compression"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem smoke_highres_sampling_items[] = {
		{SM_HRES_FULLSAMPLE, "FULLSAMPLE", 0, "Full Sample", ""},
		{SM_HRES_LINEAR, "LINEAR", 0, "Linear", ""},
		{SM_HRES_NEAREST, "NEAREST", 0, "Nearest", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem smoke_data_depth_items[] = {
		{16, "16", 0, "Float (Half)", "Half float (16 bit data)"},
		{0,  "32", 0, "Float (Full)", "Full float (32 bit data)"},  /* default */
		{0, NULL, 0, NULL, NULL},
	};
	
	static EnumPropertyItem smoke_quality_items[] = {
		{SM_VIEWPORT_GEOMETRY, "GEOMETRY", 0, "Geometry", "Display geometry"},
		{SM_VIEWPORT_PREVIEW, "PREVIEW", 0, "Preview", "Display preview quality results"},
		{SM_VIEWPORT_FINAL, "FINAL", 0, "Final", "Display final quality results"},
		{0, NULL, 0, NULL, NULL}
	};

	/*  Cache type - generated dynamically based on domain type */
	static EnumPropertyItem cache_file_type_items[] = {
		{0, "NONE", 0, "", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem smoke_view_items[] = {
	    {MOD_SMOKE_SLICE_VIEW_ALIGNED, "VIEW_ALIGNED", 0, "View", "Slice volume parallel to the view plane"},
	    {MOD_SMOKE_SLICE_AXIS_ALIGNED, "AXIS_ALIGNED", 0, "Axis", "Slice volume parallel to the major axis"},
	    {0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem axis_slice_method_items[] = {
	    {AXIS_SLICE_FULL, "FULL", 0, "Full", "Slice the whole domain object"},
	    {AXIS_SLICE_SINGLE, "SINGLE", 0, "Single", "Perform a single slice of the domain object"},
	    {0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem axis_slice_position_items[] = {
	    {SLICE_AXIS_AUTO, "AUTO", 0, "Auto", "Adjust slice direction according to the view direction"},
	    {SLICE_AXIS_X, "X", 0, "X", "Slice along the X axis"},
	    {SLICE_AXIS_Y, "Y", 0, "Y", "Slice along the Y axis"},
	    {SLICE_AXIS_Z, "Z", 0, "Z", "Slice along the Z axis"},
	    {0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem vector_draw_items[] = {
	    {VECTOR_DRAW_NEEDLE, "NEEDLE", 0, "Needle", "Draw vectors as needles"},
	    {VECTOR_DRAW_STREAMLINE, "STREAMLINE", 0, "Streamlines", "Draw vectors as streamlines"},
	    {0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem sndparticle_boundary_items[] = {
		{ SNDPARTICLE_BOUNDARY_DELETE, "DELETE", 0, "Delete", "Delete secondary particles that are inside obstacles or left the domain" },
		{ SNDPARTICLE_BOUNDARY_PUSHOUT, "PUSHOUT", 0, "Push Out", "Push secondary particles that left the domain back into the domain" },
		{ 0, NULL, 0, NULL, NULL }
	};

	static const EnumPropertyItem sndparticle_potential_resolution_items[] = {
		{ SNDPARTICLE_POTENTIAL_RESOLUTION_LOW, "LOW", 0, "Low", "Use the same resolution as the base fluid grid (faster simulation, but less accurate)" },
		{ SNDPARTICLE_POTENTIAL_RESOLUTION_HIGH, "HIGH", 0, "High", "Use twice the resolution as the base fluid grid (slower simulation, but very accurate)" },
		{ 0, NULL, 0, NULL, NULL }
	};

	static const EnumPropertyItem sndparticle_potential_quality_items[] = {
		{ SNDPARTICLE_POTENTIAL_QUALITY_LOW, "LOW", 0, "Low", "Compute potential grids with low accuracy" },
		{ SNDPARTICLE_POTENTIAL_QUALITY_HIGH, "HIGH", 0, "High", "Compute potential grids with high accuracy" },
		{ 0, NULL, 0, NULL, NULL }
	};

	static const EnumPropertyItem sndparticle_combined_export_items[] = {
		{ SNDPARTICLE_COMBINED_EXPORT_OFF, "OFF", 0, "Off", "Create a seperate particle system for every secondary particle type" },
		{ SNDPARTICLE_COMBINED_EXPORT_SPRAY_FOAM, "SPRAY & FOAM", 0, "Spray & Foam", "Spray and foam particles are saved in the same particle system" },
		{ SNDPARTICLE_COMBINED_EXPORT_SPRAY_BUBBLE, "SPRAY & BUBBLES", 0, "Spray & Bubbles", "Spray and bubble particles are saved in the same particle system" },
		{ SNDPARTICLE_COMBINED_EXPORT_FOAM_BUBBLE, "FOAM & BUBBLES", 0, "Foam & Bubbles", "Foam and bubbles particles are saved in the same particle system" },
		{ SNDPARTICLE_COMBINED_EXPORT_SPRAY_FOAM_BUBBLE, "SPRAY & FOAM & BUBBLES", 0, "Spray & Foam & Bubbles", "Create one particle system that contains all three secondary particle types" },
		{ 0, NULL, 0, NULL, NULL }
	};

	srna = RNA_def_struct(brna, "SmokeDomainSettings", NULL);
	RNA_def_struct_ui_text(srna, "Domain Settings", "Smoke domain settings");
	RNA_def_struct_sdna(srna, "SmokeDomainSettings");
	RNA_def_struct_path_func(srna, "rna_SmokeDomainSettings_path");
	
	prop = RNA_def_property(srna, "smoke_domain_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type");
	RNA_def_property_enum_items(prop, smoke_domain_types);
	RNA_def_property_enum_funcs(prop, NULL, "rna_Smoke_domaintype_set", NULL);
	RNA_def_property_ui_text(prop, "Domain Type", "Change domain type of the simulation");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "resolution_max", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "maxres");
	RNA_def_property_range(prop, 6, 10000);
	RNA_def_property_ui_range(prop, 24, 10000, 2, -1);
	RNA_def_property_ui_text(prop, "Max Res", "Resolution used for the fluid domain");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "amplify", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "amplify");
	RNA_def_property_range(prop, 1, 10);
	RNA_def_property_ui_range(prop, 1, 10, 1, -1);
	RNA_def_property_ui_text(prop, "Amplification", "Enhance the resolution of fluid domain by this factor");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_high_resolution", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_HIGHRES);
	RNA_def_property_ui_text(prop, "High res", "Enable high resolution (using amplification)");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "noise_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "noise");
	RNA_def_property_enum_items(prop, prop_noise_type_items);
	RNA_def_property_ui_text(prop, "Noise Method", "Noise method which is used for creating the high resolution");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "alpha", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "alpha");
	RNA_def_property_range(prop, -5.0, 5.0);
	RNA_def_property_ui_range(prop, -5.0, 5.0, 0.02, 5);
	RNA_def_property_ui_text(prop, "Density",
	                         "How much density affects smoke motion (higher value results in faster rising smoke)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "beta", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "beta");
	RNA_def_property_range(prop, -5.0, 5.0);
	RNA_def_property_ui_range(prop, -5.0, 5.0, 0.02, 5);
	RNA_def_property_ui_text(prop, "Heat",
	                         "How much heat affects smoke motion (higher value results in faster rising smoke)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "collision_group", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "coll_group");
	RNA_def_property_struct_type(prop, "Group");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Collision Group", "Limit collisions to this group");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset_dependency");

	prop = RNA_def_property(srna, "fluid_group", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "fluid_group");
	RNA_def_property_struct_type(prop, "Group");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Fluid Group", "Limit fluid objects to this group");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset_dependency");

	prop = RNA_def_property(srna, "effector_group", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "eff_group");
	RNA_def_property_struct_type(prop, "Group");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Effector Group", "Limit effectors to this group");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset_dependency");

	prop = RNA_def_property(srna, "strength", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "strength");
	RNA_def_property_range(prop, 0.0, 10.0);
	RNA_def_property_ui_range(prop, 0.0, 10.0, 1, 2);
	RNA_def_property_ui_text(prop, "Strength", "Strength of noise");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "dissolve_speed", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "diss_speed");
	RNA_def_property_range(prop, 1.0, 10000.0);
	RNA_def_property_ui_range(prop, 1.0, 10000.0, 1, -1);
	RNA_def_property_ui_text(prop, "Dissolve Speed", "Dissolve Speed");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "use_dissolve_smoke", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_DISSOLVE);
	RNA_def_property_ui_text(prop, "Dissolve Smoke", "Enable smoke to disappear over time");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "use_dissolve_smoke_log", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_DISSOLVE_LOG);
	RNA_def_property_ui_text(prop, "Logarithmic dissolve", "Using 1/x ");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "point_cache", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "point_cache[0]");
	RNA_def_property_ui_text(prop, "Point Cache", "");

	prop = RNA_def_property(srna, "point_cache_compress_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "cache_comp");
	RNA_def_property_enum_items(prop, smoke_cache_comp_items);
	RNA_def_property_ui_text(prop, "Cache Compression", "Compression method to be used");

	prop = RNA_def_property(srna, "openvdb_cache_compress_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "openvdb_comp");
	RNA_def_property_enum_items(prop, prop_compression_items);
	RNA_def_property_ui_text(prop, "Compression", "Compression method to be used");

	prop = RNA_def_property(srna, "data_depth", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "data_depth");
	RNA_def_property_enum_items(prop, smoke_data_depth_items);
	RNA_def_property_ui_text(prop, "Data Depth",
	                         "Bit depth for writing all scalar (including vector) "
	                         "lower values reduce file size");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "use_collision_border_front", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "border_collisions", MOD_SMOKE_BORDER_FRONT);
	RNA_def_property_ui_text(prop, "Front", "Enable collisons with front domain border");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_collision_border_back", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "border_collisions", MOD_SMOKE_BORDER_BACK);
	RNA_def_property_ui_text(prop, "Back", "Enable collisons with back domain border");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_collision_border_right", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "border_collisions", MOD_SMOKE_BORDER_RIGHT);
	RNA_def_property_ui_text(prop, "Right", "Enable collisons with right domain border");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_collision_border_left", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "border_collisions", MOD_SMOKE_BORDER_LEFT);
	RNA_def_property_ui_text(prop, "Left", "Enable collisons with left domain border");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_collision_border_top", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "border_collisions", MOD_SMOKE_BORDER_TOP);
	RNA_def_property_ui_text(prop, "Top", "Enable collisons with top domain border");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_collision_border_bottom", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "border_collisions", MOD_SMOKE_BORDER_BOTTOM);
	RNA_def_property_ui_text(prop, "Bottom", "Enable collisons with bottom domain border");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "viewport_display_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "viewport_display_mode");
	RNA_def_property_enum_items(prop, smoke_quality_items);
	RNA_def_property_enum_funcs(prop, NULL, "rna_Smoke_viewport_set", NULL);
	RNA_def_property_ui_text(prop, "Viewport Display Mode", "How to display the mesh in the viewport");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_Smoke_update");

	prop = RNA_def_property(srna, "render_display_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "render_display_mode");
	RNA_def_property_enum_items(prop, smoke_quality_items);
	RNA_def_property_ui_text(prop, "Render Display Mode", "How to display the mesh for rendering");
	RNA_def_property_update(prop, 0, "rna_Smoke_update");

	prop = RNA_def_property(srna, "effector_weights", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "EffectorWeights");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Effector Weights", "");

	prop = RNA_def_property(srna, "highres_sampling", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, smoke_highres_sampling_items);
	RNA_def_property_ui_text(prop, "Emitter", "Method for sampling the high resolution flow");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "time_scale", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "time_scale");
	RNA_def_property_range(prop, 0.0001, 10.0);
	RNA_def_property_ui_text(prop, "Time Scale", "Adjust simulation speed");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");
	
	prop = RNA_def_property(srna, "use_adaptive_stepping", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_ADAPTIVE_TIME);
	RNA_def_property_ui_text(prop, "Adaptive stepping", "Enable adaptive time-stepping");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "cfl_condition", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "cfl_condition");
	RNA_def_property_range(prop, 0.0, 10.0);
	RNA_def_property_ui_text(prop, "CFL", "Maximal velocity per cell (higher value results in larger timesteps)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "vorticity", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "vorticity");
	RNA_def_property_range(prop, 0.0, 4.0);
	RNA_def_property_ui_text(prop, "Vorticity", "Amount of turbulence/rotation in fluid");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "density_grid", PROP_FLOAT, PROP_NONE);
	RNA_def_property_array(prop, 32);
	RNA_def_property_flag(prop, PROP_DYNAMIC);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_dynamic_array_funcs(prop, "rna_SmokeModifier_grid_get_length");
	RNA_def_property_float_funcs(prop, "rna_SmokeModifier_density_grid_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Density Grid", "Smoke density grid");

	prop = RNA_def_property(srna, "velocity_grid", PROP_FLOAT, PROP_NONE);
	RNA_def_property_array(prop, 32);
	RNA_def_property_flag(prop, PROP_DYNAMIC);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_dynamic_array_funcs(prop, "rna_SmokeModifier_velocity_grid_get_length");
	RNA_def_property_float_funcs(prop, "rna_SmokeModifier_velocity_grid_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Velocity Grid", "Smoke velocity grid");

	prop = RNA_def_property(srna, "flame_grid", PROP_FLOAT, PROP_NONE);
	RNA_def_property_array(prop, 32);
	RNA_def_property_flag(prop, PROP_DYNAMIC);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_dynamic_array_funcs(prop, "rna_SmokeModifier_grid_get_length");
	RNA_def_property_float_funcs(prop, "rna_SmokeModifier_flame_grid_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Flame Grid", "Smoke flame grid");

	prop = RNA_def_property(srna, "color_grid", PROP_FLOAT, PROP_NONE);
	RNA_def_property_array(prop, 32);
	RNA_def_property_flag(prop, PROP_DYNAMIC);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_dynamic_array_funcs(prop, "rna_SmokeModifier_color_grid_get_length");
	RNA_def_property_float_funcs(prop, "rna_SmokeModifier_color_grid_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Color Grid", "Smoke color grid");

	prop = RNA_def_property(srna, "heat_grid", PROP_FLOAT, PROP_NONE);
	RNA_def_property_array(prop, 32);
	RNA_def_property_flag(prop, PROP_DYNAMIC);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_dynamic_array_funcs(prop, "rna_SmokeModifier_heat_grid_get_length");
	RNA_def_property_float_funcs(prop, "rna_SmokeModifier_heat_grid_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Heat Grid", "Smoke heat grid");

	prop = RNA_def_property(srna, "temperature_grid", PROP_FLOAT, PROP_NONE);
	RNA_def_property_array(prop, 32);
	RNA_def_property_flag(prop, PROP_DYNAMIC);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_dynamic_array_funcs(prop, "rna_SmokeModifier_grid_get_length");
	RNA_def_property_float_funcs(prop, "rna_SmokeModifier_temperature_grid_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Temperature Grid", "Smoke temperature grid, range 0..1 represents 0..1000K");

	prop = RNA_def_property(srna, "cell_size", PROP_FLOAT, PROP_XYZ); /* can change each frame when using adaptive domain */
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "cell_size", "Cell Size");

	prop = RNA_def_property(srna, "start_point", PROP_FLOAT, PROP_XYZ); /* can change each frame when using adaptive domain */
	RNA_def_property_float_sdna(prop, NULL, "p0");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "p0", "Start point");

	prop = RNA_def_property(srna, "domain_resolution", PROP_INT, PROP_XYZ); /* can change each frame when using adaptive domain */
	RNA_def_property_int_sdna(prop, NULL, "res");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "res", "Smoke Grid Resolution");

	prop = RNA_def_property(srna, "burning_rate", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.01, 4.0);
	RNA_def_property_ui_text(prop, "Speed", "Speed of the burning reaction (use larger values for smaller flame)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "flame_smoke", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 8.0);
	RNA_def_property_ui_text(prop, "Smoke", "Amount of smoke created by burning fuel");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "flame_vorticity", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 2.0);
	RNA_def_property_ui_text(prop, "Vorticity", "Additional vorticity for the flames");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "flame_ignition", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.5, 5.0);
	RNA_def_property_ui_text(prop, "Ignition", "Minimum temperature of flames");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "flame_max_temp", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 1.0, 10.0);
	RNA_def_property_ui_text(prop, "Maximum", "Maximum temperature of flames");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "flame_smoke_color", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Smoke Color", "Color of smoke emitted from burning fuel");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "use_adaptive_domain", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_ADAPTIVE_DOMAIN);
	RNA_def_property_ui_text(prop, "Adaptive Domain", "Adapt simulation resolution and size to fluid");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "additional_res", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "adapt_res");
	RNA_def_property_range(prop, 0, 512);
	RNA_def_property_ui_text(prop, "Additional", "Maximum number of additional cells");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "adapt_margin", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "adapt_margin");
	RNA_def_property_range(prop, 2, 24);
	RNA_def_property_ui_text(prop, "Margin", "Margin added around fluid to minimize boundary interference");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "adapt_threshold", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.01, 0.5);
	RNA_def_property_ui_text(prop, "Threshold",
	                         "Maximum amount of fluid cell can contain before it is considered empty");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "use_surface_cache", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_USE_SURFACE_CACHE);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Smoke_use_surface_format_set");
	RNA_def_property_ui_text(prop, "Surface cache", "Enable surface cache");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_volume_cache", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_USE_VOLUME_CACHE);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Smoke_use_volume_format_set");
	RNA_def_property_ui_text(prop, "Volumetric cache", "Enable volume cache");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "cache_surface_format", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "cache_surface_format");
	RNA_def_property_enum_items(prop, cache_file_type_items);
	RNA_def_property_enum_funcs(prop, NULL, "rna_Smoke_cachetype_surface_set", "rna_Smoke_cachetype_surface_itemf");
	RNA_def_property_ui_text(prop, "File Format", "Select the file format to be used for caching surface data");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "cache_volume_format", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "cache_volume_format");
	RNA_def_property_enum_items(prop, cache_file_type_items);
	RNA_def_property_enum_funcs(prop, NULL, "rna_Smoke_cachetype_volume_set", "rna_Smoke_cachetype_volume_itemf");
	RNA_def_property_ui_text(prop, "File Format", "Select the file format to be used for caching volumetric data");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "cache_frame_start", PROP_INT, PROP_TIME);
	RNA_def_property_int_sdna(prop, NULL, "cache_frame_start");
	RNA_def_property_range(prop, -MAXFRAME, MAXFRAME);
	RNA_def_property_ui_range(prop, 1, MAXFRAME, 1, 1);
	RNA_def_property_ui_text(prop, "Start", "Frame on which the simulation starts");

	prop = RNA_def_property(srna, "cache_frame_end", PROP_INT, PROP_TIME);
	RNA_def_property_int_sdna(prop, NULL, "cache_frame_end");
	RNA_def_property_range(prop, 1, MAXFRAME);
	RNA_def_property_ui_text(prop, "End", "Frame on which the simulation stops");

	prop = RNA_def_property(srna, "cache_directory", PROP_STRING, PROP_FILEPATH);
	RNA_def_property_string_sdna(prop, NULL, "cache_directory");
	RNA_def_property_ui_text(prop, "Cache directory", "Directory that contains fluid cache files");

	prop = RNA_def_property(srna, "cache_baking_geometry", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKING_GEOMETRY);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baked_geometry", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKED_GEOMETRY);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baking_low", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKING_LOW);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baked_low", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKED_LOW);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baking_mesh_low", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKING_MESH_LOW);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baked_mesh_low", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKED_MESH_LOW);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baking_high", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKING_HIGH);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baked_high", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKED_HIGH);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baking_mesh_high", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKING_MESH_HIGH);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baked_mesh_high", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKED_MESH_HIGH);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baking_particles_low", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKING_PARTICLES_LOW);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baked_particles_low", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKED_PARTICLES_LOW);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baking_particles_high", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKING_PARTICLES_HIGH);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "cache_baked_particles_high", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cache_flag", FLUID_CACHE_BAKED_PARTICLES_HIGH);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	/* mantaflow variables */
	prop = RNA_def_property(srna, "manta_filepath", PROP_STRING, PROP_FILEPATH);
	RNA_def_property_string_sdna(prop, NULL, "manta_filepath");
	RNA_def_property_ui_text(prop, "Output Path", "Directory/name to save Mantaflow scene script");

	prop = RNA_def_property(srna, "noise_pos_scale", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "noise_pos_scale");
	RNA_def_property_range(prop, 0.0001, 10.0);
	RNA_def_property_ui_text(prop, "Scale", "Scale of noise (higher value results in larger vortices)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");
	
	prop = RNA_def_property(srna, "noise_time_anim", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "noise_time_anim");
	RNA_def_property_range(prop, 0.0001, 10.0);
	RNA_def_property_ui_text(prop, "Time", "Animation time of noise");
	
	prop = RNA_def_property(srna, "particle_randomness", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 10.0);
	RNA_def_property_ui_text(prop, "Randomness", "Randomness factor for particle sampling");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");
	
	prop = RNA_def_property(srna, "particle_number", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 1, 5);
	RNA_def_property_ui_text(prop, "Number", "Particle number factor (higher value results in more particles)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "particle_minimum", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Minimum", "Minimum number of particles per cell (ensures that each cell has at least this amount of particles)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "particle_maximum", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Maximum", "Maximum number of particles per cell (affects only non-surface regions, defined by particle radius factor)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "particle_radius", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 10.0);
	RNA_def_property_ui_text(prop, "Radius", "Particle radius factor (higher value results in larger particles)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "particle_band_width", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_text(prop, "Width", "Particle (narrow) band width (higher value results in thicker band and more particles)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "gravity", PROP_FLOAT, PROP_ACCELERATION);
	RNA_def_property_float_sdna(prop, NULL, "gravity");
	RNA_def_property_array(prop, 3);
	RNA_def_property_range(prop, -1000.1, 1000.1);
	RNA_def_property_ui_text(prop, "Gravity", "Gravity in X, Y and Z direction");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "use_flip_particles", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "particle_type", MOD_SMOKE_PARTICLE_FLIP);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Smoke_flip_parts_set");
	RNA_def_property_ui_text(prop, "FLIP", "Create FLIP particle system");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_spray_particles", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "particle_type", MOD_SMOKE_PARTICLE_SPRAY);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Smoke_spray_parts_set");
	RNA_def_property_ui_text(prop, "Spray", "Create spray particle system");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_bubble_particles", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "particle_type", MOD_SMOKE_PARTICLE_BUBBLE);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Smoke_bubble_parts_set");
	RNA_def_property_ui_text(prop, "Bubbles", "Create bubble particle system");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_foam_particles", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "particle_type", MOD_SMOKE_PARTICLE_FOAM);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Smoke_foam_parts_set");
	RNA_def_property_ui_text(prop, "Foam", "Create foam particle system");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_tracer_particles", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "particle_type", MOD_SMOKE_PARTICLE_TRACER);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Smoke_tracer_parts_set");
	RNA_def_property_ui_text(prop, "Tracer", "Create tracer particle system");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "particle_droplet_threshold", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_text(prop, "Threshold", "Velocity threshold for drop particle generation (higher value results in fewer drops)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "particle_droplet_amount", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_text(prop, "Generate", "Drop particle sampling per cell (integral part: number of sampling steps per cell plus one; fractional part: probability of sampling a particle in a cell)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "particle_droplet_life", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_text(prop, "Life", "Life span of drop particles");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "particle_droplet_max", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Maximum", "Maximum number of drop particles per cell");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "particle_bubble_rise", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_ui_text(prop, "Rise", "How much of inverse gravity to apply on bubbles (higher value results in faster rising bubbles)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "particle_bubble_life", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_text(prop, "Life", "Life span of bubble particles");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "particle_bubble_max", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Maximum", "Maximum number of bubble particles per cell");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "particle_floater_amount", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_text(prop, "Generate", "Float particle sampling per cell (integral part: number of sampling steps per cell plus one; fractional part: probability of sampling a particle in a cell)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "particle_floater_life", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_text(prop, "Life", "Life span of float particles");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "particle_floater_max", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Maximum", "Maximum number of float particles per cell");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "particle_tracer_amount", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_text(prop, "Generate", "Tracer particle sampling per cell (integral part: number of sampling steps per cell plus one; fractional part: probability of sampling a particle in a cell)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "particle_tracer_life", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_text(prop, "Life", "Life span of tracer particles");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "particle_tracer_max", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Maximum", "Maximum number of tracer particles per cell");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "surface_tension", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 100.0);
	RNA_def_property_ui_text(prop, "Tension", "Surface tension of liquid (higher value results in greater hydrophobic behaviour)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "viscosity_base", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "viscosity_base");
	RNA_def_property_range(prop, 0, 10);
	RNA_def_property_ui_text(prop, "Viscosity Base", "Viscosity setting: value that is multiplied by 10 to the power of (exponent*-1)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "viscosity_exponent", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "viscosity_exponent");
	RNA_def_property_range(prop, 0, 10);
	RNA_def_property_ui_text(prop, "Viscosity Exponent",
							 "Negative exponent for the viscosity value (to simplify entering small values "
							 "e.g. 5*10^-6)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "domain_size", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.001, 10000.0);
	RNA_def_property_ui_text(prop, "Meters", "Domain size in meters (longest domain side)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "sndparticle_tau_min_wc", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_range(prop, 0.0, 1000.0, 100.0, 3);
	RNA_def_property_ui_text(prop, "tauMin_wc", "Lower clamping threshold for marking fluid cells as wave crests (lower values result in more marked cells)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_tau_max_wc", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_range(prop, 0.0, 1000.0, 100.0, 3);
	RNA_def_property_ui_text(prop, "tauMax_wc", "Upper clamping threshold for marking fluid cells as wave crests (higher values result in less marked cells)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_tau_min_ta", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_range(prop, 0.0, 10000.0, 100.0, 3);
	RNA_def_property_ui_text(prop, "tauMin_ta", "Lower clamping threshold for marking fluid cells where air is trapped (lower values result in more marked cells)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_tau_max_ta", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_range(prop, 0.0, 1000.0, 100.0, 3);
	RNA_def_property_ui_text(prop, "tauMax_ta", "Upper clamping threshold for marking fluid cells where air is trapped (higher values result in less marked cells)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_tau_min_k", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_range(prop, 0.0, 1000.0, 100.0, 3);
	RNA_def_property_ui_text(prop, "tauMin_k", "Lower clamping threshold that indicates the fluid speed where cells start to emit particles (lower values result in generally more particles)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_tau_max_k", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_range(prop, 0.0, 1000.0, 100.0, 3);
	RNA_def_property_ui_text(prop, "tauMax_k", "Upper clamping threshold that indicates the fluid speed where cells no longer emit more particles (higher values result in generally less particles)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_k_wc", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_range(prop, 0, 10000, 1.0, -1);
	RNA_def_property_ui_text(prop, "Wave Crest Sampling", "Maximum number of particles generated per wave crest cell per frame");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_k_ta", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_range(prop, 0, 10000, 1.0, -1);
	RNA_def_property_ui_text(prop, "Trapped Air Sampling", "Maximum number of particles generated per trapped air cell per frame");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_k_b", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 100.0);
	RNA_def_property_ui_range(prop, 0.0, 100.0, 10.0, 2);
	RNA_def_property_ui_text(prop, "Buoyancy", "Amount of buoyancy force that rises bubbles (high values result in bubble movement mainly upwards)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_k_d", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 100.0);
	RNA_def_property_ui_range(prop, 0.0, 100.0, 10.0, 2);
	RNA_def_property_ui_text(prop, "Drag", "Amount of drag force that moves bubbles along with the fluid (high values result in bubble movement mainly along with the fluid)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_l_min", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 10000.0);
	RNA_def_property_ui_range(prop, 0.0, 10000.0, 100.0, 1);
	RNA_def_property_ui_text(prop, "Lifetime(min)", "Lowest possible particle lifetime");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_l_max", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 10000.0);
	RNA_def_property_ui_range(prop, 0.0, 10000.0, 100.0, 1);
	RNA_def_property_ui_text(prop, "Lifetime(max)", "Highest possible particle lifetime");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_resetCache");

	prop = RNA_def_property(srna, "sndparticle_boundary", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "sndparticle_boundary");
	RNA_def_property_enum_items(prop, sndparticle_boundary_items);
	RNA_def_property_ui_text(prop, "Particles in Boundary", "How particles that left the domain are treated");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "sndparticle_potential_resolution", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "sndparticle_potential_resolution");
	RNA_def_property_enum_items(prop, sndparticle_potential_resolution_items);
	RNA_def_property_ui_text(prop, "Potential Resolution", "Resolution of the potential grids");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "sndparticle_potential_quality", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "sndparticle_potential_quality");
	RNA_def_property_enum_items(prop, sndparticle_potential_quality_items);
	RNA_def_property_ui_text(prop, "Potential Quality", "How accurately are the potential grids computed");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "sndparticle_combined_export", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "sndparticle_combined_export");
	RNA_def_property_enum_items(prop, sndparticle_combined_export_items);
	RNA_def_property_ui_text(prop, "Combined Export", "Determines which particle systems are created from secondary particles");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "guiding_alpha", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "guiding_alpha");
	RNA_def_property_range(prop, 1.0, 100.0);
	RNA_def_property_ui_text(prop, "Weight", "Guiding weight (higher value results in greater lag)");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "guiding_beta", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "guiding_beta");
	RNA_def_property_range(prop, 1, 50);
	RNA_def_property_ui_text(prop, "Size", "Guiding size (higher value results in larger vortices)");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	/* display settings */

	prop = RNA_def_property(srna, "slice_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "slice_method");
	RNA_def_property_enum_items(prop, smoke_view_items);
	RNA_def_property_ui_text(prop, "View Method", "How to slice the volume for viewport rendering");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "axis_slice_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "axis_slice_method");
	RNA_def_property_enum_items(prop, axis_slice_method_items);
	RNA_def_property_ui_text(prop, "Method", "");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "slice_axis", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "slice_axis");
	RNA_def_property_enum_items(prop, axis_slice_position_items);
	RNA_def_property_ui_text(prop, "Axis", "");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "slice_per_voxel", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "slice_per_voxel");
	RNA_def_property_range(prop, 0.0, 100.0);
	RNA_def_property_ui_range(prop, 0.0, 5.0, 0.1, 1);
	RNA_def_property_ui_text(prop, "Slice Per Voxel",
	                         "How many slices per voxel should be generated");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "slice_depth", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "slice_depth");
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_ui_range(prop, 0.0, 1.0, 0.1, 3);
	RNA_def_property_ui_text(prop, "Position", "Position of the slice");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "display_thickness", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "display_thickness");
	RNA_def_property_range(prop, 0.001, 1000.0);
	RNA_def_property_ui_range(prop, 0.01, 100.0, 0.1, 3);
	RNA_def_property_ui_text(prop, "Thickness", "Thickness of smoke drawing in the viewport");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

	prop = RNA_def_property(srna, "draw_velocity", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "draw_velocity", 0);
	RNA_def_property_ui_text(prop, "Draw Velocity", "Toggle visualization of the velocity field as needles");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "vector_draw_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "vector_draw_type");
	RNA_def_property_enum_items(prop, vector_draw_items);
	RNA_def_property_ui_text(prop, "Draw Type", "");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "vector_scale", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "vector_scale");
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_range(prop, 0.0, 100.0, 0.1, 3);
	RNA_def_property_ui_text(prop, "Scale", "Multiplier for scaling the vectors");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	/* --------- Color mapping. --------- */

	prop = RNA_def_property(srna, "use_color_ramp", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "use_coba", 0);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Smoke_use_color_ramp_set");
	RNA_def_property_ui_text(prop, "Use Color Ramp",
	                         "Render a simulation field while mapping its voxels values to the colors of a ramp");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	static const EnumPropertyItem coba_field_items[] = {
	    {FLUID_FIELD_COLOR_R, "COLOR_R", 0, "Red", "Red component of the color field"},
	    {FLUID_FIELD_COLOR_G, "COLOR_G", 0, "Green", "Green component of the color field"},
	    {FLUID_FIELD_COLOR_B, "COLOR_B", 0, "Blue", "Blue component of the color field"},
	    {FLUID_FIELD_DENSITY, "DENSITY", 0, "Density", "Quantity of soot in the fluid"},
	    {FLUID_FIELD_FLAME, "FLAME", 0, "Flame", "Flame field"},
	    {FLUID_FIELD_FUEL, "FUEL", 0, "Fuel", "Fuel field"},
	    {FLUID_FIELD_HEAT, "HEAT", 0, "Heat", "Temperature of the fluid"},
	    {FLUID_FIELD_VELOCITY_X, "VELOCITY_X", 0, "X Velocity", "X component of the velocity field"},
	    {FLUID_FIELD_VELOCITY_Y, "VELOCITY_Y", 0, "Y Velocity", "Y component of the velocity field"},
	    {FLUID_FIELD_VELOCITY_Z, "VELOCITY_Z", 0, "Z Velocity", "Z component of the velocity field"},
	    {0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem coba_field_items_liquid[] = {
		{ FLUID_FIELD_PRESSURE, "PRESSURE", 0, "Pressure", "Pressure grid inside the liquid" },
		{ FLUID_FIELD_KINETIC_ENERGY, "KINETIC_ENERGY", 0, "Kinetic Energy Potential", "Kinetic energy potential grid for secondary particle generation" },
		{ FLUID_FIELD_TRAPPED_AIR, "TRAPPED_AIR", 0, "Trapped Air Potential", "Trapped air potential grid for secondary particle generation" },
		{ FLUID_FIELD_WAVE_CREST, "WAVE_CREST", 0, "Wave Crest Potential", "Wave crest potential grid for secondary particle generation" },
		{ 0, NULL, 0, NULL, NULL }
	};

	prop = RNA_def_property(srna, "coba_field", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "coba_field");
	RNA_def_property_enum_items(prop, coba_field_items);
	RNA_def_property_ui_text(prop, "Field", "Simulation field to color map");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "coba_field_liquid", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "coba_field_liquid");
	RNA_def_property_enum_items(prop, coba_field_items_liquid);
	RNA_def_property_ui_text(prop, "Field", "Simulation field to color map");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "color_ramp", PROP_POINTER, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "coba");
	RNA_def_property_struct_type(prop, "ColorRamp");
	RNA_def_property_ui_text(prop, "Color Ramp", "");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);
}

static void rna_def_smoke_flow_settings(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem smoke_flow_types[] = {
		{MOD_SMOKE_FLOW_TYPE_SMOKE, "SMOKE", 0, "Smoke", "Add smoke"},
		{MOD_SMOKE_FLOW_TYPE_SMOKEFIRE, "BOTH", 0, "Fire + Smoke", "Add fire and smoke"},
		{MOD_SMOKE_FLOW_TYPE_FIRE, "FIRE", 0, "Fire", "Add fire"},
		{MOD_SMOKE_FLOW_TYPE_LIQUID, "LIQUID", 0, "Liquid", "Add liquid"},
		{0, NULL, 0, NULL, NULL}
	};
	
	static EnumPropertyItem smoke_flow_behaviors[] = {
		{MOD_SMOKE_FLOW_BEHAVIOR_INFLOW, "INFLOW", 0, "Inflow", "Add fluid to simulation"},
		{MOD_SMOKE_FLOW_BEHAVIOR_OUTFLOW, "OUTFLOW", 0, "Outflow", "Delete fluid from simulation"},
		{MOD_SMOKE_FLOW_BEHAVIOR_GEOMETRY, "GEOMETRY", 0, "Geometry", "Only use given geometry for fluid"},
		{0, NULL, 0, NULL, NULL}
	};

	/*  Flow source - generated dynamically based on flow type */
	static EnumPropertyItem smoke_flow_sources[] = {
		{0, "NONE", 0, "", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem smoke_flow_texture_types[] = {
		{MOD_SMOKE_FLOW_TEXTURE_MAP_AUTO, "AUTO", 0, "Generated", "Generated coordinates centered to flow object"},
		{MOD_SMOKE_FLOW_TEXTURE_MAP_UV, "UV", 0, "UV", "Use UV layer for texture coordinates"},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "SmokeFlowSettings", NULL);
	RNA_def_struct_ui_text(srna, "Flow Settings", "Smoke flow settings");
	RNA_def_struct_sdna(srna, "SmokeFlowSettings");
	RNA_def_struct_path_func(srna, "rna_SmokeFlowSettings_path");

	prop = RNA_def_property(srna, "density", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "density");
	RNA_def_property_range(prop, 0.0, 1);
	RNA_def_property_ui_text(prop, "Density", "");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "smoke_color", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "color");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Smoke Color", "Color of smoke");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "fuel_amount", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 10);
	RNA_def_property_ui_text(prop, "Flame Rate", "");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "temperature", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "temp");
	RNA_def_property_range(prop, -10, 10);
	RNA_def_property_ui_text(prop, "Temp. Diff.", "Temperature difference to ambient temperature");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");
	
	prop = RNA_def_property(srna, "particle_system", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "psys");
	RNA_def_property_struct_type(prop, "ParticleSystem");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Particle Systems", "Particle systems emitted from the object");
	RNA_def_property_update(prop, 0, "rna_Smoke_reset_dependency");

	prop = RNA_def_property(srna, "smoke_flow_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type");
	RNA_def_property_enum_items(prop, smoke_flow_types);
	RNA_def_property_enum_funcs(prop, NULL, "rna_Smoke_flowtype_set", NULL);
	RNA_def_property_ui_text(prop, "Flow Type", "Change type of fluid in the simulation");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");
	
	prop = RNA_def_property(srna, "smoke_flow_behavior", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "behavior");
	RNA_def_property_enum_items(prop, smoke_flow_behaviors);
	RNA_def_property_ui_text(prop, "Flow Behavior", "Change flow behavior in the simulation");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "smoke_flow_source", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "source");
	RNA_def_property_enum_items(prop, smoke_flow_sources);
	RNA_def_property_enum_funcs(prop, NULL, "rna_Smoke_flowsource_set", "rna_Smoke_flowsource_itemf");
	RNA_def_property_ui_text(prop, "Source", "Change how fluid is emitted");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_absolute", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_FLOW_ABSOLUTE);
	RNA_def_property_ui_text(prop, "Absolute Density", "Only allow given density value in emitter area");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_initial_velocity", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_FLOW_INITVELOCITY);
	RNA_def_property_ui_text(prop, "Initial Velocity", "Fluid has some initial velocity when it is emitted");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "velocity_factor", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "vel_multi");
	RNA_def_property_range(prop, -100.0, 100.0);
	RNA_def_property_ui_text(prop, "Source", "Multiplier of source velocity passed to fluid");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "velocity_normal", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "vel_normal");
	RNA_def_property_range(prop, -100.0, 100.0);
	RNA_def_property_ui_text(prop, "Normal", "Amount of normal directional velocity");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "velocity_random", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "vel_random");
	RNA_def_property_range(prop, 0.0, 10.0);
	RNA_def_property_ui_text(prop, "Random", "Amount of random velocity");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "volume_density", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_ui_text(prop, "Volume", "Factor for smoke emitted from inside the mesh volume");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "surface_distance", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 10.0);
	RNA_def_property_ui_text(prop, "Surface", "Maximum distance from mesh surface to emit fluid");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "particle_size", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.1, 20.0);
	RNA_def_property_ui_text(prop, "Size", "Particle size in simulation cells");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_particle_size", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_FLOW_USE_PART_SIZE);
	RNA_def_property_ui_text(prop, "Set Size", "Set particle size in simulation cells or use nearest cell");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_inflow", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_FLOW_USE_INFLOW);
	RNA_def_property_ui_text(prop, "Enabled", "Control when to apply inflow");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");
	
	prop = RNA_def_property(srna, "subframes", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 50);
	RNA_def_property_ui_text(prop, "Subframes", "Number of additional samples to take between frames to improve quality of fast moving flows");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "density_vertex_group", PROP_STRING, PROP_NONE);
	RNA_def_property_string_funcs(prop, "rna_SmokeFlow_density_vgroup_get",
	                              "rna_SmokeFlow_density_vgroup_length",
	                              "rna_SmokeFlow_density_vgroup_set");
	RNA_def_property_ui_text(prop, "Vertex Group",
	                         "Name of vertex group which determines surface emission rate");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "use_texture", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", MOD_SMOKE_FLOW_TEXTUREEMIT);
	RNA_def_property_ui_text(prop, "Use Texture", "Use a texture to control emission strength");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "texture_map_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "texture_type");
	RNA_def_property_enum_items(prop, smoke_flow_texture_types);
	RNA_def_property_ui_text(prop, "Mapping", "Texture mapping type");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "uv_layer", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "uvlayer_name");
	RNA_def_property_ui_text(prop, "UV Map", "UV map name");
	RNA_def_property_string_funcs(prop, NULL, NULL, "rna_SmokeFlow_uvlayer_set");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "noise_texture", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Texture", "Texture that controls emission strength");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "texture_size", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.01, 10.0);
	RNA_def_property_ui_text(prop, "Size", "Size of texture mapping");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "texture_offset", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 200.0);
	RNA_def_property_ui_text(prop, "Offset", "Z-offset of texture mapping");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");
}

static void rna_def_smoke_effec_settings(BlenderRNA *brna)
{
	static EnumPropertyItem smoke_effec_type_items[] = {
		{SM_EFFECTOR_COLLISION, "COLLISION", 0, "Collision", "Create collision object"},
		{SM_EFFECTOR_GUIDE, "GUIDE", 0, "Guide", "Create guiding object"},
		{0, NULL, 0, NULL, NULL}
	};

	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "SmokeCollSettings", NULL);
	RNA_def_struct_ui_text(srna, "Collision Settings", "Smoke collision settings");
	RNA_def_struct_sdna(srna, "SmokeCollSettings");
	RNA_def_struct_path_func(srna, "rna_SmokeCollSettings_path");

	prop = RNA_def_property(srna, "effec_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type");
	RNA_def_property_enum_items(prop, smoke_effec_type_items);
	RNA_def_property_ui_text(prop, "Effector type", "Effector type");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");

	prop = RNA_def_property(srna, "surface_distance", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 10.0);
	RNA_def_property_ui_text(prop, "Distance", "Distance around mesh surface to consider as effector");
	RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, "rna_Smoke_reset");
}

void RNA_def_smoke(BlenderRNA *brna)
{
	rna_def_smoke_domain_settings(brna);
	rna_def_smoke_flow_settings(brna);
	rna_def_smoke_effec_settings(brna);
}

#endif
