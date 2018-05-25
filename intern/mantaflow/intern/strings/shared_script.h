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

/** \file mantaflow/intern/strings/shared_script.h
 *  \ingroup mantaflow
 */

#include <string>

//////////////////////////////////////////////////////////////////////
// LIBRARIES
//////////////////////////////////////////////////////////////////////

const std::string manta_import = "\
from manta import *\n\
import os.path, shutil, math, sys, gc, multiprocessing, platform, time\n\
\n\
withMP = False\n\
isWindows = platform.system() != 'Darwin' and platform.system() != 'Linux'\n\
# TODO (sebbas): Use this to simulate Windows multiprocessing (has default mode spawn)\n\
#try:\n\
#    multiprocessing.set_start_method('spawn')\n\
#except:\n\
#    pass\n\
\n\
bpy = sys.modules.get('bpy')\n\
if bpy is not None:\n\
    sys.executable = bpy.app.binary_path_python\n\
del bpy\n";

//////////////////////////////////////////////////////////////////////
// DEBUG
//////////////////////////////////////////////////////////////////////

const std::string manta_debuglevel = "\n\
def set_manta_debuglevel(level):\n\
    setDebugLevel(level=level)\n # level 0 = mute all output from manta\n";

//////////////////////////////////////////////////////////////////////
// SOLVERS
//////////////////////////////////////////////////////////////////////

const std::string fluid_solver = "\n\
mantaMsg('Solver base')\n\
s$ID$ = Solver(name='solver_base$ID$', gridSize=gs_s$ID$, dim=dim_s$ID$)\n";

const std::string fluid_solver_noise = "\n\
mantaMsg('Solver noise')\n\
sn$ID$ = Solver(name='solver_noise$ID$', gridSize=gs_sn$ID$)\n";

const std::string fluid_solver_mesh = "\n\
mantaMsg('Solver mesh')\n\
sm$ID$ = Solver(name='solver_mesh$ID$', gridSize=gs_sm$ID$)\n";

const std::string fluid_solver_particles = "\n\
mantaMsg('Solver particles')\n\
sp$ID$ = Solver(name='solver_particles$ID$', gridSize=gs_sp$ID$)\n";

//////////////////////////////////////////////////////////////////////
// VARIABLES
//////////////////////////////////////////////////////////////////////

const std::string fluid_variables = "\n\
mantaMsg('Fluid variables low')\n\
dim_s$ID$     = $SOLVER_DIM$\n\
res_s$ID$     = $RES$\n\
gravity_s$ID$ = vec3($GRAVITY_X$, $GRAVITY_Y$, $GRAVITY_Z$)\n\
gs_s$ID$      = vec3($RESX$, $RESY$, $RESZ$)\n\
maxVel_s$ID$  = 0\n\
\n\
doOpen_s$ID$          = $DO_OPEN$\n\
boundConditions_s$ID$ = '$BOUNDCONDITIONS$'\n\
boundaryWidth_s$ID$   = 1\n\
\n\
using_smoke_s$ID$     = $USING_SMOKE$\n\
using_liquid_s$ID$    = $USING_LIQUID$\n\
using_highres_s$ID$   = $USING_HIGHRES$\n\
using_adaptTime_s$ID$ = $USING_ADAPTIVETIME$\n\
using_obstacle_s$ID$  = $USING_OBSTACLE$\n\
using_guiding_s$ID$   = $USING_GUIDING$\n\
using_invel_s$ID$     = $USING_INVEL$\n\
using_sndparts_s$ID$  = $USING_SNDPARTS$\n\
\n\
# fluid guiding params\n\
alpha_s$ID$ = $GUIDING_ALPHA$\n\
beta_s$ID$  = $GUIDING_BETA$\n\
tau_s$ID$   = 1.0\n\
sigma_s$ID$ = 0.99/tau_s$ID$\n\
theta_s$ID$ = 1.0\n\
\n\
# fluid time params\n\
dt_default_s$ID$ = 0.1 # dt is 0.1 at 25fps\n\
dt_factor_s$ID$  = $DT_FACTOR$\n\
fps_s$ID$        = $FPS$\n\
dt0_s$ID$        = dt_default_s$ID$ * (25.0 / fps_s$ID$) * dt_factor_s$ID$\n\
cfl_cond_s$ID$   = $CFL$\n\
\n\
# fluid diffusion / viscosity\n\
domainSize_s$ID$ = $FLUID_DOMAIN_SIZE$ # longest domain side in cm\n\
if domainSize_s$ID$ == 0: domainSize_s$ID$ = 100 # TODO (sebbas): just for versioning, remove with proper 2.8 versioning\n\
viscosity_s$ID$ = $FLUID_VISCOSITY$ / (domainSize_s$ID$*domainSize_s$ID$) # kinematic viscosity in m^2/s\n";

const std::string fluid_variables_noise = "\n\
mantaMsg('Fluid variables noise')\n\
upres_sn$ID$  = $NOISE_SCALE$\n\
gs_sn$ID$     = vec3($NOISE_RESX$, $NOISE_RESY$, $NOISE_RESZ$)\n";

const std::string fluid_variables_mesh = "\n\
mantaMsg('Fluid variables mesh')\n\
upres_sm$ID$  = $MESH_SCALE$\n\
gs_sm$ID$     = vec3($MESH_RESX$, $MESH_RESY$, $MESH_RESZ$)\n";

const std::string fluid_variables_particles = "\n\
mantaMsg('Fluid variables particles')\n\
upres_sp$ID$  = $PARTICLE_SCALE$\n\
gs_sp$ID$     = vec3($PARTICLE_RESX$, $PARTICLE_RESY$, $PARTICLE_RESZ$)\n\
tauMin_wc_sp$ID$ = $SNDPARTICLE_TAU_MIN_WC$\n\
tauMax_wc_sp$ID$ = $SNDPARTICLE_TAU_MAX_WC$\n\
tauMin_ta_sp$ID$ = $SNDPARTICLE_TAU_MIN_TA$\n\
tauMax_ta_sp$ID$ = $SNDPARTICLE_TAU_MAX_TA$\n\
tauMin_k_sp$ID$ = $SNDPARTICLE_TAU_MIN_K$\n\
tauMax_k_sp$ID$ = $SNDPARTICLE_TAU_MAX_K$\n\
k_wc_sp$ID$ = $SNDPARTICLE_K_WC$\n\
k_ta_sp$ID$ = $SNDPARTICLE_K_TA$\n\
k_b_sp$ID$ = $SNDPARTICLE_K_B$\n\
k_d_sp$ID$ = $SNDPARTICLE_K_D$\n\
lMin_sp$ID$ = $SNDPARTICLE_L_MIN$\n\
lMax_sp$ID$ = $SNDPARTICLE_L_MAX$\n\
c_s_sp$ID$ = 0.4   # classification constant for snd parts\n\
c_b_sp$ID$ = 0.77  # classification constant for snd parts\n\
scaleFromManta_sp$ID$ = $FLUID_DOMAIN_SIZE$ / float(res_s$ID$) # resize factor for snd parts\n\
gravity_rescaled_sp$ID$ = gravity_s$ID$ / scaleFromManta_sp$ID$";

const std::string fluid_with_obstacle = "\n\
using_obstacle_s$ID$ = True\n";

const std::string fluid_with_guiding = "\n\
using_guiding_s$ID$ = True\n";

const std::string fluid_with_invel = "\n\
using_invel_s$ID$ = True\n";

const std::string fluid_with_sndparts = "\n\
using_sndparts_s$ID$ = True\n";

//////////////////////////////////////////////////////////////////////
// ADAPTIVE TIME STEPPING
//////////////////////////////////////////////////////////////////////

const std::string fluid_adaptive_time_stepping = "\n\
mantaMsg('Fluid adaptive time stepping low')\n\
s$ID$.frameLength = dt0_s$ID$ \n\
s$ID$.timestepMin = s$ID$.frameLength / 10.\n\
s$ID$.timestepMax = s$ID$.frameLength\n\
s$ID$.cfl         = cfl_cond_s$ID$\n\
s$ID$.timestep    = s$ID$.frameLength\n";

const std::string fluid_adaptive_time_stepping_noise = "\n\
mantaMsg('Fluid adaptive time stepping high')\n\
sn$ID$.frameLength = s$ID$.frameLength\n\
sn$ID$.timestepMin = s$ID$.timestepMin\n\
sn$ID$.timestepMax = s$ID$.timestepMax\n\
sn$ID$.cfl         = s$ID$.cfl\n";

const std::string fluid_adapt_time_step = "\n\
def fluid_adapt_time_step_$ID$():\n\
    mantaMsg('Fluid adapt time step')\n\
    \n\
    # time params are animatable\n\
    s$ID$.frameLength = dt0_s$ID$ \n\
    s$ID$.cfl = cfl_cond_s$ID$\n\
    \n\
    maxVel_s$ID$ = vel_s$ID$.getMax() if vel_s$ID$ else 0\n\
    if using_adaptTime_s$ID$:\n\
        mantaMsg('Adapt timestep, maxvel: ' + str(maxVel_s$ID$))\n\
        s$ID$.adaptTimestep(maxVel_s$ID$)\n";

const std::string fluid_adapt_time_step_noise = "\n\
def fluid_adapt_time_step_noise_$ID$():\n\
    sn$ID$.timestep    = s$ID$.timestep\n\
    sn$ID$.frameLength = dt0_s$ID$ \n\
    sn$ID$.cfl         = cfl_cond_s$ID$\n";

//////////////////////////////////////////////////////////////////////
// GRIDS
//////////////////////////////////////////////////////////////////////

const std::string fluid_alloc = "\n\
mantaMsg('Fluid alloc low')\n\
flags_s$ID$       = s$ID$.create(FlagGrid)\n\
numFlow_s$ID$     = s$ID$.create(IntGrid)\n\
flowType_s$ID$    = s$ID$.create(IntGrid)\n\
vel_s$ID$         = s$ID$.create(MACGrid)\n\
x_vel_s$ID$       = s$ID$.create(RealGrid)\n\
y_vel_s$ID$       = s$ID$.create(RealGrid)\n\
z_vel_s$ID$       = s$ID$.create(RealGrid)\n\
pressure_s$ID$    = s$ID$.create(RealGrid)\n\
phiObs_s$ID$      = s$ID$.create(LevelsetGrid)\n\
phiOut_s$ID$      = s$ID$.create(LevelsetGrid)\n\
phiOutIn_s$ID$    = s$ID$.create(LevelsetGrid) # TODO (sebbas): Move phiOutIn to separete init - similarly to phiObsIn\n\
forces_s$ID$      = s$ID$.create(Vec3Grid)\n\
x_force_s$ID$     = s$ID$.create(RealGrid)\n\
y_force_s$ID$     = s$ID$.create(RealGrid)\n\
z_force_s$ID$     = s$ID$.create(RealGrid)\n\
\n\
# Keep track of important objects in dict to load them later on\n\
fluid_data_dict_s$ID$ = dict(vel=vel_s$ID$, phiObs=phiObs_s$ID$, phiOut=phiOut_s$ID$)\n";

const std::string fluid_alloc_obstacle_low = "\n\
mantaMsg('Allocating obstacle low')\n\
numObs_s$ID$     = s$ID$.create(IntGrid)\n\
phiObsIn_s$ID$   = s$ID$.create(LevelsetGrid)\n\
obvel_s$ID$      = s$ID$.create(MACGrid)\n\
obvelC_s$ID$     = s$ID$.create(Vec3Grid)\n\
x_obvel_s$ID$    = s$ID$.create(RealGrid)\n\
y_obvel_s$ID$    = s$ID$.create(RealGrid)\n\
z_obvel_s$ID$    = s$ID$.create(RealGrid)\n\
\n\
tmpDict_s$ID$ = dict(phiObsIn=phiObsIn_s$ID$)\n\
fluid_data_dict_s$ID$.update(tmpDict_s$ID$)\n";

const std::string fluid_alloc_guiding_low = "\n\
mantaMsg('Allocating guiding low')\n\
numGuides_s$ID$   = s$ID$.create(IntGrid)\n\
phiGuideIn_s$ID$  = s$ID$.create(LevelsetGrid)\n\
guidevel_s$ID$    = s$ID$.create(MACGrid)\n\
guidevelC_s$ID$   = s$ID$.create(Vec3Grid)\n\
x_guidevel_s$ID$  = s$ID$.create(RealGrid)\n\
y_guidevel_s$ID$  = s$ID$.create(RealGrid)\n\
z_guidevel_s$ID$  = s$ID$.create(RealGrid)\n\
weightGuide_s$ID$ = s$ID$.create(RealGrid)\n";

const std::string fluid_alloc_invel_low = "\n\
mantaMsg('Allocating initial velocity low')\n\
invel_s$ID$   = s$ID$.create(VecGrid)\n\
x_invel_s$ID$ = s$ID$.create(RealGrid)\n\
y_invel_s$ID$ = s$ID$.create(RealGrid)\n\
z_invel_s$ID$ = s$ID$.create(RealGrid)\n";

const std::string fluid_alloc_sndparts = "\n\
mantaMsg('Allocating snd parts low')\n\
ppSnd_sp$ID$     = sp$ID$.create(BasicParticleSystem)\n\
pVelSnd_pp$ID$   = ppSnd_sp$ID$.create(PdataVec3)\n\
pForceSnd_pp$ID$ = ppSnd_sp$ID$.create(PdataVec3)\n\
pLifeSnd_pp$ID$  = ppSnd_sp$ID$.create(PdataReal)\n\
vel_sp$ID$       = sp$ID$.create(MACGrid)\n\
flags_sp$ID$     = sp$ID$.create(FlagGrid)\n\
phi_sp$ID$       = sp$ID$.create(LevelsetGrid)\n\
phiIn_sp$ID$     = sp$ID$.create(LevelsetGrid)\n\
phiObs_sp$ID$    = sp$ID$.create(LevelsetGrid)\n\
phiObsIn_sp$ID$  = sp$ID$.create(LevelsetGrid)\n\
\n\
normal_sp$ID$        = sp$ID$.create(VecGrid)\n\
neighborRatio_sp$ID$ = sp$ID$.create(RealGrid)\n\
trappedAir_sp$ID$    = sp$ID$.create(RealGrid)\n\
waveCrest_sp$ID$     = sp$ID$.create(RealGrid)\n\
kineticEnergy_sp$ID$ = sp$ID$.create(RealGrid)\n\
\n\
# Keep track of important objects in dict to load them later on\n\
fluid_particles_dict_s$ID$ = dict(ppSnd=ppSnd_sp$ID$, pVelSnd=pVelSnd_pp$ID$, pLifeSnd=pLifeSnd_pp$ID$, trappedAir=trappedAir_sp$ID$, waveCrest=waveCrest_sp$ID$, kineticEnergy=kineticEnergy_sp$ID$)\n";

//////////////////////////////////////////////////////////////////////
// DESTRUCTION
//////////////////////////////////////////////////////////////////////

const std::string fluid_delete_all = "\n\
mantaMsg('Deleting fluid')\n\
# Delete childs from pp system object first\n\
for var in list(globals()):\n\
    if var.endswith('_pp$ID$'):\n\
        del globals()[var]\n\
# Now delete childs from solver objects\n\
for var in list(globals()):\n\
    if var.endswith('_s$ID$') or var.endswith('_sn$ID$') or var.endswith('_sm$ID$') or var.endswith('_sp$ID$'):\n\
        del globals()[var]\n\
\n\
# Extra cleanup for multigrid and fluid guiding\n\
mantaMsg('Release multigrid')\n\
if 's$ID$' in globals(): releaseMG(s$ID$)\n\
if 'sn$ID$' in globals(): releaseMG(sn$ID$)\n\
mantaMsg('Release fluid guiding')\n\
releaseBlurPrecomp()\n\
\n\
# Release unreferenced memory (if there is some left, can in fact happen)\n\
gc.collect()\n\
\n\
# Now it is safe to delete solver objects (always need to be deleted last)\n\
mantaMsg('Delete solver low')\n\
if 's$ID$' in globals(): del s$ID$\n\
mantaMsg('Delete noise solver')\n\
if 'sn$ID$' in globals(): del sn$ID$\n\
mantaMsg('Delete mesh solver')\n\
if 'sm$ID$' in globals(): del sm$ID$\n\
mantaMsg('Delete noise particles')\n\
if 'sp$ID$' in globals(): del sp$ID$\n\
\n\
# Release unreferenced memory (if there is some left)\n\
gc.collect()\n";

//////////////////////////////////////////////////////////////////////
// BAKE
//////////////////////////////////////////////////////////////////////

const std::string fluid_bake_helper = "\n\
def fluid_cache_get_framenr_formatted_$ID$(framenr):\n\
    return str(framenr).zfill(4) # framenr with leading zeroes\n\
\n\
def fluid_cache_multiprocessing_start_$ID$(function, framenr, format_data=None, format_noise=None, format_mesh=None, format_particles=None, path_data=None, path_noise=None, path_mesh=None, path_particles=None):\n\
    if __name__ == '__main__':\n\
        args = (framenr,)\n\
        if format_data:\n\
            args += (format_data,)\n\
        if format_noise:\n\
            args += (format_noise,)\n\
        if format_mesh:\n\
            args += (format_mesh,)\n\
        if format_particles:\n\
            args += (format_particles,)\n\
        if path_data:\n\
            args += (path_data,)\n\
        if path_noise:\n\
            args += (path_noise,)\n\
        if path_mesh:\n\
            args += (path_mesh,)\n\
        if path_particles:\n\
            args += (path_particles,)\n\
        p$ID$ = multiprocessing.Process(target=function, args=args)\n\
        p$ID$.start()\n\
        p$ID$.join()\n";

const std::string fluid_bake_data = "\n\
def bake_fluid_process_data_$ID$(framenr, format_data, format_particles, path_data):\n\
    mantaMsg('Bake fluid data')\n\
    \n\
    s$ID$.frame = framenr\n\
    \n\
    start_time = time.time()\n\
    if using_smoke_s$ID$:\n\
        smoke_adaptive_step_$ID$(framenr)\n\
    if using_liquid_s$ID$:\n\
        liquid_adaptive_step_$ID$(framenr)\n\
    mantaMsg('--- Step: %s seconds ---' % (time.time() - start_time))\n\
\n\
def bake_fluid_data_$ID$(path_data, framenr, format_data, format_particles):\n\
    if not withMP or isWindows:\n\
        bake_fluid_process_data_$ID$(framenr, format_data, format_particles, path_data)\n\
    else:\n\
        fluid_cache_multiprocessing_start_$ID$(function=bake_fluid_process_data_$ID$, framenr=framenr, format_data=format_data, format_particles=format_particles, path_data=path_data)\n";

const std::string fluid_bake_noise = "\n\
def bake_noise_process_$ID$(framenr, format_data, format_noise, path_data, path_noise):\n\
    mantaMsg('Bake fluid noise')\n\
    \n\
    sn$ID$.frame = framenr\n\
    \n\
    fluid_load_data_$ID$(path_data, framenr, format_data)\n\
    smoke_load_data_$ID$(path_data, framenr, format_data)\n\
    smoke_adaptive_step_noise_$ID$(framenr)\n\
    smoke_save_noise_$ID$(path_noise, framenr, format_noise)\n\
\n\
def bake_noise_$ID$(path_data, path_noise, framenr, format_data, format_noise):\n\
    if not withMP or isWindows:\n\
        bake_noise_process_$ID$(framenr, format_data, format_noise, path_data, path_noise)\n\
    else:\n\
        fluid_cache_multiprocessing_start_$ID$(function=bake_noise_process_$ID$, framenr=framenr, format_data=format_data, format_noise=format_noise, path_data=path_data, path_noise=path_noise)\n";

const std::string fluid_bake_mesh = "\n\
def bake_mesh_process_$ID$(framenr, format_data, format_mesh, format_particles, path_data, path_mesh):\n\
    mantaMsg('Bake fluid mesh')\n\
    \n\
    sm$ID$.frame = framenr\n\
    \n\
    #if using_smoke_s$ID$:\n\
        # TODO (sebbas): Future update could include smoke mesh (vortex sheets)\n\
    if using_liquid_s$ID$:\n\
        liquid_load_data_$ID$(path_data, framenr, format_data)\n\
        liquid_load_flip_$ID$(path_data, framenr, format_particles)\n\
        liquid_step_mesh_$ID$()\n\
        liquid_save_mesh_$ID$(path_mesh, framenr, format_mesh)\n\
\n\
def bake_mesh_$ID$(path_data, path_mesh, framenr, format_data, format_mesh, format_particles):\n\
    if not withMP or isWindows:\n\
        bake_mesh_process_$ID$(framenr, format_data, format_mesh, format_particles, path_data, path_mesh)\n\
    else:\n\
        fluid_cache_multiprocessing_start_$ID$(function=bake_mesh_process_$ID$, framenr=framenr, format_data=format_data, format_mesh=format_mesh, format_particles=format_particles, path_data=path_data, path_mesh=path_mesh)\n";

const std::string fluid_bake_particles = "\n\
def bake_particles_process_$ID$(framenr, format_data, format_particles, path_data, path_particles):\n\
    mantaMsg('Bake secondary particles')\n\
    \n\
    sp$ID$.frame = framenr\n\
    \n\
    fluid_load_data_$ID$(path_data, framenr, format_data)\n\
    #if using_smoke_s$ID$:\n\
        # TODO (sebbas): Future update could include smoke particles (e.g. fire sparks)\n\
    if using_liquid_s$ID$:\n\
        liquid_load_data_$ID$(path_data, framenr, format_data)\n\
        if framenr>1:\n\
            fluid_load_particles_$ID$(path_particles, framenr-1, format_particles)\n\
        \n\
        liquid_step_particles_$ID$()\n\
        fluid_save_particles_$ID$(path_particles, framenr, format_particles)\n\
\n\
def bake_particles_$ID$(path_data, path_particles, framenr, format_data, format_particles):\n\
    if not withMP or isWindows:\n\
        bake_particles_process_$ID$(framenr, format_data, format_particles, path_data, path_particles)\n\
    else:\n\
        fluid_cache_multiprocessing_start_$ID$(function=bake_particles_process_$ID$, framenr=framenr, format_data=format_data, format_particles=format_particles, path_data=path_data, path_particles=path_particles)\n";

//////////////////////////////////////////////////////////////////////
// IMPORT
//////////////////////////////////////////////////////////////////////

const std::string fluid_file_import = "\n\
def fluid_file_import_s$ID$(dict, path, framenr, file_format):\n\
    try:\n\
        framenr = fluid_cache_get_framenr_formatted_$ID$(framenr)\n\
        for name, object in dict.items():\n\
            file = os.path.join(path, name + '_' + framenr + file_format)\n\
            if os.path.isfile(file): object.load(file)\n\
    except Exception as e:\n\
        mantaMsg(str(e))\n\
        pass # Just skip file load errors for now\n";

const std::string fluid_load_particles = "\n\
def fluid_load_particles_$ID$(path, framenr, file_format):\n\
    mantaMsg('Fluid load particles')\n\
    fluid_file_import_s$ID$(dict=fluid_particles_dict_s$ID$, path=path, framenr=framenr, file_format=file_format)\n";

const std::string fluid_load_data = "\n\
def fluid_load_data_$ID$(path, framenr, file_format):\n\
    mantaMsg('Fluid load data')\n\
    fluid_file_import_s$ID$(dict=fluid_data_dict_s$ID$, path=path, framenr=framenr, file_format=file_format)\n";


//////////////////////////////////////////////////////////////////////
// EXPORT
//////////////////////////////////////////////////////////////////////

const std::string fluid_file_export = "\n\
def fluid_file_export_s$ID$(dict, path, framenr, file_format, mode_override=False):\n\
    try:\n\
        framenr = fluid_cache_get_framenr_formatted_$ID$(framenr)\n\
        for name, object in dict.items():\n\
            file = os.path.join(path, name + '_' + framenr + file_format)\n\
            if not os.path.isfile(file) or mode_override: object.save(file)\n\
    except Exception as e:\n\
        mantaMsg(str(e))\n\
        pass # Just skip file save errors for now\n";

const std::string fluid_save_particles = "\n\
def fluid_save_particles_$ID$(path, framenr, file_format):\n\
    mantaMsg('Liquid save particles')\n\
    fluid_file_export_s$ID$(dict=fluid_particles_dict_s$ID$, path=path, framenr=framenr, file_format=file_format)\n";

const std::string fluid_save_data = "\n\
def fluid_save_data_$ID$(path, framenr, file_format):\n\
    mantaMsg('Fluid save data low')\n\
    fluid_file_export_s$ID$(dict=fluid_data_dict_s$ID$, path=path, framenr=framenr, file_format=file_format)\n";

//////////////////////////////////////////////////////////////////////
// STANDALONE MODE
//////////////////////////////////////////////////////////////////////

const std::string fluid_standalone_load = "\n\
if using_obstacle_s$ID$:\n\
    load_fluid_obstacle_data_low_$ID$(path_prefix_$ID$)\n\
if using_guiding_s$ID$:\n\
    load_fluid_guiding_data_low_$ID$(path_prefix_$ID$)\n\
if using_invel_s$ID$:\n\
    load_fluid_invel_data_low_$ID$(path_prefix_$ID$)\n\
if using_sndparts_s$ID$:\n\
    load_fluid_sndparts_data_low_$ID$(path_prefix_$ID$)\n";

const std::string fluid_standalone = "\n\
if (GUI):\n\
    gui=Gui()\n\
    gui.show()\n\
    gui.pause()\n\
\n\
start_frame = $CURRENT_FRAME$\n\
end_frame = 1000\n\
\n\
# All low and high res steps\n\
while start_frame <= end_frame:\n\
    manta_step_$ID$(start_frame)\n\
    start_frame += 1\n";
