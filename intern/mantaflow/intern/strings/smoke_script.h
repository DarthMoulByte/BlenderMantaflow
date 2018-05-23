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

/** \file mantaflow/intern/strings/smoke.h
 *  \ingroup mantaflow
 */

#include <string>

//////////////////////////////////////////////////////////////////////
// BOUNDS
//////////////////////////////////////////////////////////////////////

const std::string smoke_bounds = "\n\
# prepare domain low\n\
mantaMsg('Smoke domain low')\n\
flags_s$ID$.initDomain(boundaryWidth=boundaryWidth_s$ID$)\n\
flags_s$ID$.fillGrid()\n\
if doOpen_s$ID$:\n\
    setOpenBound(flags=flags_s$ID$, bWidth=boundaryWidth_s$ID$, openBound=boundConditions_s$ID$, type=FlagOutflow|FlagEmpty)\n";

const std::string smoke_bounds_noise = "\n\
# prepare domain high\n\
mantaMsg('Smoke domain high')\n\
flags_sn$ID$.initDomain(boundaryWidth=boundaryWidth_s$ID$)\n\
flags_sn$ID$.fillGrid()\n\
if doOpen_s$ID$:\n\
    setOpenBound(flags=flags_sn$ID$, bWidth=boundaryWidth_s$ID$, openBound=boundConditions_s$ID$, type=FlagOutflow|FlagEmpty)\n";

//////////////////////////////////////////////////////////////////////
// VARIABLES
//////////////////////////////////////////////////////////////////////

const std::string smoke_variables = "\n\
mantaMsg('Smoke variables low')\n\
preconditioner_s$ID$  = PcMGStatic\n\
using_colors_s$ID$    = $USING_COLORS$\n\
using_heat_s$ID$      = $USING_HEAT$\n\
using_fire_s$ID$      = $USING_FIRE$\n\
vorticity_s$ID$       = $VORTICITY$\n\
buoyancy_dens_s$ID$   = $BUOYANCY_ALPHA$\n\
buoyancy_heat_s$ID$   = $BUOYANCY_BETA$\n\
absoluteFlow_s$ID$    = True\n";

const std::string smoke_variables_noise = "\n\
mantaMsg('Smoke variables high')\n\
wltStrength_s$ID$ = $WLT_STR$\n\
octaves_s$ID$     = 0\n\
uvs_s$ID$         = 2\n\
uv_s$ID$          = [] # list for UV grids\n\
\n\
if upres_sn$ID$ == 1:\n\
    octaves_s$ID$ = int(math.log(upres_sn$ID$+1)/ math.log(2.0) + 0.5)\n\
elif upres_sn$ID$ > 1:\n\
    octaves_s$ID$ = int(math.log(upres_sn$ID$)/ math.log(2.0) + 0.5)\n\
\n\
# wavelet noise params\n\
wltnoise_sn$ID$.posScale = vec3(int(1.0*gs_s$ID$.x)) / $NOISE_POSSCALE$\n\
wltnoise_sn$ID$.timeAnim = $NOISE_TIMEANIM$\n";

const std::string smoke_with_heat = "\n\
using_heat_s$ID$ = True\n";

const std::string smoke_with_colors = "\n\
using_colors_s$ID$ = True\n";

const std::string smoke_with_fire = "\n\
using_fire_s$ID$ = True\n";

//////////////////////////////////////////////////////////////////////
// GRIDS
//////////////////////////////////////////////////////////////////////

const std::string smoke_alloc = "\n\
mantaMsg('Smoke alloc low')\n\
density_s$ID$    = s$ID$.create(RealGrid)\n\
emissionIn_s$ID$ = s$ID$.create(RealGrid)\n\
shadow_s$ID$     = s$ID$.create(RealGrid)\n\
heat_s$ID$    = 0 # allocated dynamically\n\
flame_s$ID$   = 0\n\
fuel_s$ID$    = 0\n\
react_s$ID$   = 0\n\
color_r_s$ID$ = 0\n\
color_g_s$ID$ = 0\n\
color_b_s$ID$ = 0\n\
\n\
# Keep track of important objects in dict to load them later on\n\
smoke_data_dict_s$ID$ = dict(density=density_s$ID$, shadow=shadow_s$ID$)\n";

const std::string smoke_alloc_noise = "\n\
mantaMsg('Smoke alloc noise')\n\
vel_sn$ID$       = sn$ID$.create(MACGrid)\n\
density_sn$ID$   = sn$ID$.create(RealGrid)\n\
phiOut_sn$ID$    = sn$ID$.create(LevelsetGrid)\n\
phiObs_sn$ID$    = sn$ID$.create(LevelsetGrid)\n\
flags_sn$ID$     = sn$ID$.create(FlagGrid)\n\
energy_s$ID$     = s$ID$.create(RealGrid)\n\
tempFlag_s$ID$   = s$ID$.create(FlagGrid)\n\
texture_u_s$ID$  = s$ID$.create(RealGrid)\n\
texture_v_s$ID$  = s$ID$.create(RealGrid)\n\
texture_w_s$ID$  = s$ID$.create(RealGrid)\n\
texture_u2_s$ID$ = s$ID$.create(RealGrid)\n\
texture_v2_s$ID$ = s$ID$.create(RealGrid)\n\
texture_w2_s$ID$ = s$ID$.create(RealGrid)\n\
\n\
# Keep track of important objects in dict to load them later on\n\
smoke_noise_dict_s$ID$ = dict(density_noise=density_sn$ID$)\n\
tmpDict_s$ID$ = dict(texture_u=texture_u_s$ID$, texture_v=texture_v_s$ID$, texture_w=texture_w_s$ID$)\n\
smoke_noise_dict_s$ID$.update(tmpDict_s$ID$)\n\
tmpDict_s$ID$ = dict(texture_u2=texture_u2_s$ID$, texture_v2=texture_v2_s$ID$, texture_w2=texture_w2_s$ID$)\n\
smoke_noise_dict_s$ID$.update(tmpDict_s$ID$)\n";

//////////////////////////////////////////////////////////////////////
// ADDITIONAL GRIDS
//////////////////////////////////////////////////////////////////////

const std::string smoke_alloc_colors_low = "\n\
mantaMsg('Allocating colors low')\n\
color_r_s$ID$   = s$ID$.create(RealGrid)\n\
color_g_s$ID$   = s$ID$.create(RealGrid)\n\
color_b_s$ID$   = s$ID$.create(RealGrid)\n\
\n\
# Add objects to dict to load them later on\n\
tmpDict_s$ID$ = dict(color_r=color_r_s$ID$, color_g=color_g_s$ID$, color_b=color_b_s$ID$)\n\
smoke_data_dict_s$ID$.update(tmpDict_s$ID$)\n";

const std::string smoke_alloc_colors_high = "\
mantaMsg('Allocating colors high')\n\
color_r_sn$ID$ = sn$ID$.create(RealGrid)\n\
color_g_sn$ID$ = sn$ID$.create(RealGrid)\n\
color_b_sn$ID$ = sn$ID$.create(RealGrid)\n\
\n\
# Add objects to dict to load them later on\n\
tmpDict_s$ID$ = dict(color_r_noise=color_r_sn$ID$, color_g_noise=color_g_sn$ID$, color_b_noise=color_b_sn$ID$)\n\
smoke_noise_dict_s$ID$.update(tmpDict_s$ID$)\n";

const std::string smoke_init_colors_low = "\n\
mantaMsg('Initializing colors low')\n\
color_r_s$ID$.copyFrom(density_s$ID$) \n\
color_r_s$ID$.multConst($COLOR_R$) \n\
color_g_s$ID$.copyFrom(density_s$ID$) \n\
color_g_s$ID$.multConst($COLOR_G$) \n\
color_b_s$ID$.copyFrom(density_s$ID$) \n\
color_b_s$ID$.multConst($COLOR_B$)\n";

const std::string smoke_init_colors_high = "\n\
mantaMsg('Initializing colors high')\n\
color_r_sn$ID$.copyFrom(density_sn$ID$) \n\
color_r_sn$ID$.multConst($COLOR_R$) \n\
color_g_sn$ID$.copyFrom(density_sn$ID$) \n\
color_g_sn$ID$.multConst($COLOR_G$) \n\
color_b_sn$ID$.copyFrom(density_sn$ID$) \n\
color_b_sn$ID$.multConst($COLOR_B$)\n";

const std::string smoke_alloc_heat_low = "\n\
mantaMsg('Allocating heat low')\n\
heat_s$ID$   = s$ID$.create(RealGrid)\n\
\n\
# Add objects to dict to load them later on\n\
tmpDict_s$ID$ = dict(heat=heat_s$ID$)\n\
smoke_data_dict_s$ID$.update(tmpDict_s$ID$)\n";

const std::string smoke_alloc_fire_low = "\n\
mantaMsg('Allocating fire low')\n\
flame_s$ID$  = s$ID$.create(RealGrid)\n\
fuel_s$ID$   = s$ID$.create(RealGrid)\n\
react_s$ID$  = s$ID$.create(RealGrid)\n\
\n\
# Add objects to dict to load them later on\n\
tmpDict_s$ID$ = dict(flame=flame_s$ID$, fuel=fuel_s$ID$, react=react_s$ID$,)\n\
smoke_data_dict_s$ID$.update(tmpDict_s$ID$)\n";

const std::string smoke_alloc_fire_high = "\n\
mantaMsg('Allocating fire high')\n\
flame_sn$ID$ = sn$ID$.create(RealGrid)\n\
fuel_sn$ID$  = sn$ID$.create(RealGrid)\n\
react_sn$ID$ = sn$ID$.create(RealGrid)\n\
\n\
# Add objects to dict to load them later on\n\
tmpDict_s$ID$ = dict(react_noise=react_sn$ID$, fuel_noise=fuel_sn$ID$, flame_noise=flame_sn$ID$)\n\
smoke_noise_dict_s$ID$.update(tmpDict_s$ID$)\n";

//////////////////////////////////////////////////////////////////////
// PRE / POST STEP
//////////////////////////////////////////////////////////////////////

const std::string smoke_pre_step = "\n\
def smoke_pre_step_low_$ID$():\n\
    mantaMsg('Smoke pre step low')\n\
    # translate obvels (world space) to grid space\n\
    if using_obstacle_s$ID$:\n\
        x_obvel_s$ID$.multConst(Real(gs_s$ID$.x))\n\
        y_obvel_s$ID$.multConst(Real(gs_s$ID$.y))\n\
        z_obvel_s$ID$.multConst(Real(gs_s$ID$.z))\n\
        copyRealToVec3(sourceX=x_obvel_s$ID$, sourceY=y_obvel_s$ID$, sourceZ=z_obvel_s$ID$, target=obvelC_s$ID$)\n\
    \n\
    # translate guiding vels (world space) to grid space\n\
    if using_guiding_s$ID$:\n\
        x_guidevel_s$ID$.multConst(Real(gs_s$ID$.x))\n\
        y_guidevel_s$ID$.multConst(Real(gs_s$ID$.y))\n\
        z_guidevel_s$ID$.multConst(Real(gs_s$ID$.z))\n\
        copyRealToVec3(sourceX=x_guidevel_s$ID$, sourceY=y_guidevel_s$ID$, sourceZ=z_guidevel_s$ID$, target=guidevelC_s$ID$)\n\
    \n\
    # translate invels (world space) to grid space\n\
    if using_invel_s$ID$:\n\
        x_invel_s$ID$.multConst(Real(gs_s$ID$.x))\n\
        y_invel_s$ID$.multConst(Real(gs_s$ID$.y))\n\
        z_invel_s$ID$.multConst(Real(gs_s$ID$.z))\n\
        copyRealToVec3(sourceX=x_invel_s$ID$, sourceY=y_invel_s$ID$, sourceZ=z_invel_s$ID$, target=invel_s$ID$)\n\
    \n\
    #x_vel_s$ID$.multConst(Real(gs_s$ID$.x))\n\
    #y_vel_s$ID$.multConst(Real(gs_s$ID$.y))\n\
    #z_vel_s$ID$.multConst(Real(gs_s$ID$.z))\n\
    #copyRealToVec3(sourceX=x_vel_s$ID$, sourceY=y_vel_s$ID$, sourceZ=z_vel_s$ID$, target=vel_s$ID$)\n\
    copyRealToVec3(sourceX=x_force_s$ID$, sourceY=y_force_s$ID$, sourceZ=z_force_s$ID$, target=forces_s$ID$)\n\
    \n\
    # If obstacle has velocity, i.e. is moving switch to dynamic preconditioner\n\
    if using_obstacle_s$ID$ and obvelC_s$ID$.getMax() > 0:\n\
        mantaMsg('Using dynamic preconditioner')\n\
        preconditioner_s$ID$ = PcMGDynamic\n\
    else:\n\
        mantaMsg('Using static preconditioner')\n\
        preconditioner_s$ID$ = PcMGStatic\n";

const std::string smoke_pre_step_noise = "\n\
def smoke_pre_step_noise_$ID$():\n\
    mantaMsg('Smoke pre step high')\n\
    # Create interpolated version of original phi grids for later use in (optional) high-res step\n\
    if using_obstacle_s$ID$:\n\
        interpolateGrid(target=phiOut_sn$ID$, source=phiOut_s$ID$)\n\
        interpolateGrid(target=phiObs_sn$ID$, source=phiObs_s$ID$)\n\
    \n\
    global uv_s$ID$\n\
    if len(uv_s$ID$) != 0: # list of uvs already initialized?\n\
        copyRealToVec3(sourceX=texture_u_s$ID$, sourceY=texture_v_s$ID$, sourceZ=texture_w_s$ID$, target=uv_s$ID$[0])\n\
        copyRealToVec3(sourceX=texture_u2_s$ID$, sourceY=texture_v2_s$ID$, sourceZ=texture_w2_s$ID$, target=uv_s$ID$[1])\n\
    else:\n\
        mantaMsg('Initializing UV Grids')\n\
        for i in range(uvs_s$ID$):\n\
            uvGrid_s$ID$ = s$ID$.create(VecGrid)\n\
            uv_s$ID$.append(uvGrid_s$ID$)\n\
            resetUvGrid(uv_s$ID$[i])\n";

const std::string smoke_post_step = "\n\
def smoke_post_step_low_$ID$():\n\
    mantaMsg('Smoke post step low')\n\
    forces_s$ID$.clear()\n\
    if using_guiding_s$ID$:\n\
        weightGuide_s$ID$.clear()\n\
    if using_invel_s$ID$:\n\
        invel_s$ID$.clear()\n\
    \n\
    phiObs_s$ID$.setConst(9999)\n\
    phiOutIn_s$ID$.setConst(9999)\n\
    \n\
    #copyVec3ToReal(source=vel_s$ID$, targetX=x_vel_s$ID$, targetY=y_vel_s$ID$, targetZ=z_vel_s$ID$)\n\
    #x_vel_s$ID$.multConst( 1.0/Real(gs_s$ID$.x) )\n\
    #y_vel_s$ID$.multConst( 1.0/Real(gs_s$ID$.y) )\n\
    #z_vel_s$ID$.multConst( 1.0/Real(gs_s$ID$.z) )\n";

const std::string smoke_post_step_noise = "\n\
def smoke_post_step_noise_$ID$():\n\
    mantaMsg('Smoke post step high')\n\
    copyVec3ToReal(source=uv_s$ID$[0], targetX=texture_u_s$ID$, targetY=texture_v_s$ID$, targetZ=texture_w_s$ID$)\n\
    copyVec3ToReal(source=uv_s$ID$[1], targetX=texture_u2_s$ID$, targetY=texture_v2_s$ID$, targetZ=texture_w2_s$ID$)\n";

//////////////////////////////////////////////////////////////////////
// STEP FUNCTIONS
//////////////////////////////////////////////////////////////////////

const std::string smoke_adaptive_step = "\n\
def smoke_adaptive_step_$ID$(framenr):\n\
    mantaMsg('Manta step low, frame ' + str(framenr))\n\
    \n\
    # time params are animatable\n\
    s$ID$.frameLength = dt0_s$ID$ \n\
    s$ID$.cfl = cfl_cond_s$ID$\n\
    \n\
    mantaMsg('s.frame is ' + str(s$ID$.frame))\n\
    mantaMsg('s.timestep is ' + str(s$ID$.timestep))\n\
    mantaMsg('s.cfl is ' + str(s$ID$.cfl))\n\
    mantaMsg('s.frameLength is ' + str(s$ID$.frameLength))\n\
    \n\
    smoke_pre_step_low_$ID$()\n\
    \n\
    if using_obstacle_s$ID$: # TODO (sebbas): allow outflow objects when no obstacle set\n\
        phiObs_s$ID$.join(phiObsIn_s$ID$)\n\
    \n\
    phiOut_s$ID$.join(phiOutIn_s$ID$)\n\
    \n\
    setObstacleFlags(flags=flags_s$ID$, phiObs=phiObs_s$ID$, phiOut=phiOut_s$ID$)\n\
    flags_s$ID$.fillGrid()\n\
    \n\
    mantaMsg('Low step / s$ID$.frame: ' + str(s$ID$.frame))\n\
    if using_fire_s$ID$:\n\
        process_burn_low_$ID$()\n\
    step_low_$ID$()\n\
    if using_fire_s$ID$:\n\
        update_flame_low_$ID$()\n\
    \n\
    s$ID$.step()\n\
    \n\
    smoke_post_step_low_$ID$()\n";

const std::string smoke_adaptive_step_noise = "\n\
def smoke_adaptive_step_noise_$ID$(framenr):\n\
    mantaMsg('Manta step high, frame ' + str(framenr))\n\
    \n\
    sn$ID$.frame = framenr\n\
    sn$ID$.timeTotal = sn$ID$.frame * dt0_s$ID$\n\
    last_frame_s$ID$ = sn$ID$.frame\n\
    \n\
    smoke_pre_step_noise_$ID$()\n\
    \n\
    while sn$ID$.frame == last_frame_s$ID$:\n\
        \n\
        mantaMsg('sn.frame is ' + str(sn$ID$.frame))\n\
        setObstacleFlags(flags=flags_sn$ID$, phiObs=phiObs_sn$ID$, phiOut=phiOut_sn$ID$)\n\
        flags_sn$ID$.fillGrid()\n\
        \n\
        fluid_adapt_time_step_noise_$ID$()\n\
        mantaMsg('High step / sn$ID$.frame: ' + str(sn$ID$.frame))\n\
        if using_fire_s$ID$:\n\
            process_burn_high_$ID$()\n\
        step_noise_$ID$()\n\
        if using_fire_s$ID$:\n\
            update_flame_high_$ID$()\n\
        \n\
        sn$ID$.step()\n\
    \n\
    smoke_post_step_noise_$ID$()\n";

const std::string smoke_step = "\n\
def step_low_$ID$():\n\
    mantaMsg('Smoke step low')\n\
    mantaMsg('Advecting density')\n\
    advectSemiLagrange(flags=flags_s$ID$, vel=vel_s$ID$, grid=density_s$ID$, order=$ADVECT_ORDER$)\n\
    \n\
    if using_heat_s$ID$:\n\
        mantaMsg('Advecting heat')\n\
        advectSemiLagrange(flags=flags_s$ID$, vel=vel_s$ID$, grid=heat_s$ID$, order=$ADVECT_ORDER$)\n\
    \n\
    if using_fire_s$ID$:\n\
        mantaMsg('Advecting fire')\n\
        advectSemiLagrange(flags=flags_s$ID$, vel=vel_s$ID$, grid=fuel_s$ID$, order=$ADVECT_ORDER$)\n\
        advectSemiLagrange(flags=flags_s$ID$, vel=vel_s$ID$, grid=react_s$ID$, order=$ADVECT_ORDER$)\n\
    \n\
    if using_colors_s$ID$:\n\
        mantaMsg('Advecting colors')\n\
        advectSemiLagrange(flags=flags_s$ID$, vel=vel_s$ID$, grid=color_r_s$ID$, order=$ADVECT_ORDER$)\n\
        advectSemiLagrange(flags=flags_s$ID$, vel=vel_s$ID$, grid=color_g_s$ID$, order=$ADVECT_ORDER$)\n\
        advectSemiLagrange(flags=flags_s$ID$, vel=vel_s$ID$, grid=color_b_s$ID$, order=$ADVECT_ORDER$)\n\
    \n\
    mantaMsg('Advecting velocity')\n\
    advectSemiLagrange(flags=flags_s$ID$, vel=vel_s$ID$, grid=vel_s$ID$, order=$ADVECT_ORDER$, openBounds=doOpen_s$ID$, boundaryWidth=boundaryWidth_s$ID$)\n\
    \n\
    if doOpen_s$ID$:\n\
        resetOutflow(flags=flags_s$ID$, real=density_s$ID$)\n\
    \n\
    mantaMsg('Vorticity')\n\
    vorticityConfinement(vel=vel_s$ID$, flags=flags_s$ID$, strength=vorticity_s$ID$)\n\
    \n\
    if using_heat_s$ID$:\n\
        mantaMsg('Adding heat buoyancy')\n\
        addBuoyancy(flags=flags_s$ID$, density=density_s$ID$, vel=vel_s$ID$, gravity=gravity_s$ID$, coefficient=buoyancy_dens_s$ID$)\n\
        addBuoyancy(flags=flags_s$ID$, density=heat_s$ID$, vel=vel_s$ID$, gravity=gravity_s$ID$, coefficient=buoyancy_heat_s$ID$)\n\
    else:\n\
        mantaMsg('Adding buoyancy')\n\
        addBuoyancy(density=density_s$ID$, vel=vel_s$ID$, gravity=gravity_s$ID$, flags=flags_s$ID$)\n\
    \n\
    mantaMsg('Adding forces')\n\
    addForceField(flags=flags_s$ID$, vel=vel_s$ID$, force=forces_s$ID$)\n\
    \n\
    if using_obstacle_s$ID$:\n\
        mantaMsg('Extrapolating object velocity')\n\
        # ensure velocities inside of obs object, slightly add obvels outside of obs object\n\
        extrapolateVec3Simple(vel=obvelC_s$ID$, phi=phiObsIn_s$ID$, distance=int(res_s$ID$/2), inside=True)\n\
        extrapolateVec3Simple(vel=obvelC_s$ID$, phi=phiObsIn_s$ID$, distance=1, inside=False)\n\
        resampleVec3ToMac(source=obvelC_s$ID$, target=obvel_s$ID$)\n\
    \n\
    if using_guiding_s$ID$:\n\
        mantaMsg('Extrapolating guiding velocity')\n\
        # ensure velocities inside of guiding object, slightly add guiding vels outside of object too\n\
        extrapolateVec3Simple(vel=guidevelC_s$ID$, phi=phiGuideIn_s$ID$, distance=int(res_s$ID$/2), inside=True)\n\
        extrapolateVec3Simple(vel=guidevelC_s$ID$, phi=phiGuideIn_s$ID$, distance=4, inside=False)\n\
        resampleVec3ToMac(source=guidevelC_s$ID$, target=guidevel_s$ID$)\n\
    \n\
    # add initial velocity\n\
    if using_invel_s$ID$:\n\
        setInitialVelocity(flags=flags_s$ID$, vel=vel_s$ID$, invel=invel_s$ID$)\n\
    \n\
    mantaMsg('Walls')\n\
    setWallBcs(flags=flags_s$ID$, vel=vel_s$ID$, obvel=obvel_s$ID$ if using_obstacle_s$ID$ else 0)\n\
    \n\
    if using_guiding_s$ID$:\n\
        mantaMsg('Guiding and pressure')\n\
        weightGuide_s$ID$.addConst(alpha_s$ID$)\n\
        PD_fluid_guiding(vel=vel_s$ID$, velT=guidevel_s$ID$, flags=flags_s$ID$, weight=weightGuide_s$ID$, blurRadius=beta_s$ID$, pressure=pressure_s$ID$, tau=tau_s$ID$, sigma=sigma_s$ID$, theta=theta_s$ID$, preconditioner=preconditioner_s$ID$, zeroPressureFixing=not doOpen_s$ID$)\n\
    else:\n\
        mantaMsg('Pressure')\n\
        solvePressure(flags=flags_s$ID$, vel=vel_s$ID$, pressure=pressure_s$ID$, preconditioner=preconditioner_s$ID$, zeroPressureFixing=not doOpen_s$ID$) # closed domains require pressure fixing\n\
\n\
def process_burn_low_$ID$():\n\
    mantaMsg('Process burn')\n\
    processBurn(fuel=fuel_s$ID$, density=density_s$ID$, react=react_s$ID$, red=color_r_s$ID$ if using_colors_s$ID$ else 0, green=color_g_s$ID$ if using_colors_s$ID$ else 0, blue=color_b_s$ID$ if using_colors_s$ID$ else 0, heat=heat_s$ID$ if using_heat_s$ID$ else 0, burningRate=$BURNING_RATE$, flameSmoke=$FLAME_SMOKE$, ignitionTemp=$IGNITION_TEMP$, maxTemp=$MAX_TEMP$, flameSmokeColor=vec3($FLAME_SMOKE_COLOR_X$,$FLAME_SMOKE_COLOR_Y$,$FLAME_SMOKE_COLOR_Z$))\n\
\n\
def update_flame_low_$ID$():\n\
    mantaMsg('Update flame low')\n\
    updateFlame(react=react_s$ID$, flame=flame_s$ID$)\n";

const std::string smoke_step_noise = "\n\
def step_noise_$ID$():\n\
    mantaMsg('Smoke step noise')\n\
    \n\
    mantaMsg('Interpolating grids')\n\
    interpolateGrid(source=density_s$ID$, target=density_sn$ID$)\n\
    interpolateMACGrid(source=vel_s$ID$, target=vel_sn$ID$)\n\
    \n\
    for i in range(uvs_s$ID$):\n\
        mantaMsg('Advecting UV')\n\
        advectSemiLagrange(flags=flags_s$ID$, vel=vel_s$ID$, grid=uv_s$ID$[i], order=$ADVECT_ORDER$)\n\
        mantaMsg('Updating UVWeight')\n\
        updateUvWeight(resetTime=10.0 , index=i, numUvs=uvs_s$ID$, uv=uv_s$ID$[i])\n\
    \n\
    mantaMsg('Energy')\n\
    computeEnergy(flags=flags_s$ID$, vel=vel_s$ID$, energy=energy_s$ID$)\n\
    \n\
    tempFlag_s$ID$.copyFrom(flags_s$ID$)\n\
    extrapolateSimpleFlags(flags=flags_s$ID$, val=tempFlag_s$ID$, distance=2, flagFrom=FlagObstacle, flagTo=FlagFluid)\n\
    extrapolateSimpleFlags(flags=tempFlag_s$ID$, val=energy_s$ID$, distance=6, flagFrom=FlagFluid, flagTo=FlagObstacle)\n\
    computeWaveletCoeffs(energy_s$ID$)\n\
    \n\
    sStr_s$ID$ = 1.0 * wltStrength_s$ID$\n\
    sPos_s$ID$ = 2.0\n\
    \n\
    mantaMsg('Applying noise vec')\n\
    for o in range(octaves_s$ID$):\n\
        for i in range(uvs_s$ID$):\n\
            uvWeight_s$ID$ = getUvWeight(uv_s$ID$[i])\n\
            applyNoiseVec3(flags=flags_sn$ID$, target=vel_sn$ID$, noise=wltnoise_sn$ID$, scale=sStr_s$ID$ * uvWeight_s$ID$, scaleSpatial=sPos_s$ID$ , weight=energy_s$ID$, uv=uv_s$ID$[i])\n\
        sStr_s$ID$ *= 0.06 # magic kolmogorov factor \n\
        sPos_s$ID$ *= 2.0 \n\
    \n\
    for substep in range(int(upres_sn$ID$)):\n\
        if using_colors_s$ID$: \n\
            mantaMsg('Advecting colors high')\n\
            advectSemiLagrange(flags=flags_sn$ID$, vel=vel_sn$ID$, grid=color_r_sn$ID$, order=$ADVECT_ORDER$, openBounds=doOpen_s$ID$)\n\
            advectSemiLagrange(flags=flags_sn$ID$, vel=vel_sn$ID$, grid=color_g_sn$ID$, order=$ADVECT_ORDER$, openBounds=doOpen_s$ID$)\n\
            advectSemiLagrange(flags=flags_sn$ID$, vel=vel_sn$ID$, grid=color_b_sn$ID$, order=$ADVECT_ORDER$, openBounds=doOpen_s$ID$)\n\
        \n\
        if using_fire_s$ID$: \n\
            mantaMsg('Advecting fire high')\n\
            advectSemiLagrange(flags=flags_sn$ID$, vel=vel_sn$ID$, grid=fuel_sn$ID$, order=$ADVECT_ORDER$, openBounds=doOpen_s$ID$)\n\
            advectSemiLagrange(flags=flags_sn$ID$, vel=vel_sn$ID$, grid=react_sn$ID$, order=$ADVECT_ORDER$, openBounds=doOpen_s$ID$)\n\
        \n\
        mantaMsg('Advecting density high')\n\
        advectSemiLagrange(flags=flags_sn$ID$, vel=vel_sn$ID$, grid=density_sn$ID$, order=$ADVECT_ORDER$, openBounds=doOpen_s$ID$)\n\
\n\
def process_burn_high_$ID$():\n\
    mantaMsg('Process burn high')\n\
    processBurn(fuel=fuel_sn$ID$, density=density_sn$ID$, react=react_sn$ID$, red=color_r_sn$ID$ if using_colors_s$ID$ else 0, green=color_g_sn$ID$ if using_colors_s$ID$ else 0, blue=color_b_sn$ID$ if using_colors_s$ID$ else 0, burningRate=$BURNING_RATE$, flameSmoke=$FLAME_SMOKE$, ignitionTemp=$IGNITION_TEMP$, maxTemp=$MAX_TEMP$, dt=dt0_s$ID$, flameSmokeColor=vec3($FLAME_SMOKE_COLOR_X$,$FLAME_SMOKE_COLOR_Y$,$FLAME_SMOKE_COLOR_Z$))\n\
\n\
def update_flame_high_$ID$():\n\
    mantaMsg('Update flame high')\n\
    updateFlame(react=react_sn$ID$, flame=flame_sn$ID$)\n";

//////////////////////////////////////////////////////////////////////
// IMPORT
//////////////////////////////////////////////////////////////////////

const std::string smoke_load_data = "\n\
def smoke_load_data_$ID$(path, framenr, file_format):\n\
    mantaMsg('Smoke load data')\n\
    fluid_file_import_s$ID$(dict=smoke_data_dict_s$ID$, path=path, framenr=framenr, file_format=file_format)\n";

const std::string smoke_load_noise = "\n\
def smoke_load_noise_$ID$(path, framenr, file_format):\n\
    mantaMsg('Smoke load noise')\n\
    fluid_file_import_s$ID$(dict=smoke_noise_dict_s$ID$, path=path, framenr=framenr, file_format=file_format)\n";

//////////////////////////////////////////////////////////////////////
// EXPORT
//////////////////////////////////////////////////////////////////////

const std::string smoke_save_data = "\n\
def smoke_save_data_$ID$(path, framenr, file_format):\n\
    mantaMsg('Smoke save data')\n\
    fluid_file_export_s$ID$(dict=smoke_data_dict_s$ID$, path=path, framenr=framenr, file_format=file_format)\n";

const std::string smoke_save_noise = "\n\
def smoke_save_noise_$ID$(path, framenr, file_format):\n\
    mantaMsg('Smoke save noise')\n\
    fluid_file_export_s$ID$(dict=smoke_noise_dict_s$ID$, path=path, framenr=framenr, file_format=file_format)\n";

//////////////////////////////////////////////////////////////////////
// OTHER SETUPS
//////////////////////////////////////////////////////////////////////

const std::string smoke_wavelet_turbulence_noise = "\n\
# wavelet turbulence noise field\n\
mantaMsg('Smoke wavelet noise')\n\
wltnoise_sn$ID$ = sn$ID$.create(NoiseField, loadFromFile=True)\n";

const std::string smoke_inflow_low = "\n\
def apply_inflow_$ID$():\n\
    mantaMsg('Applying inflow')\n\
    if using_heat_s$ID$ and using_colors_s$ID$ and using_fire_s$ID$:\n\
        applyInflow(density=density_s$ID$, emission=inflow_s$ID$, heat=heat_s$ID$, fuel=fuel_s$ID$, react=react_s$ID$, red=color_r_s$ID$, green=color_g_s$ID$, blue=color_b_s$ID$)\n\
    elif using_heat_s$ID$ and using_colors_s$ID$ and not using_fire_s$ID$:\n\
        applyInflow(density=density_s$ID$, emission=inflow_s$ID$, heat=heat_s$ID$, red=color_r_s$ID$, green=color_g_s$ID$, blue=color_b_s$ID$)\n\
    elif using_heat_s$ID$ and not using_colors_s$ID$ and using_fire_s$ID$:\n\
        applyInflow(density=density_s$ID$, emission=inflow_s$ID$, heat=heat_s$ID$, fuel=fuel_s$ID$, react=react_s$ID$)\n\
    elif using_heat_s$ID$ and not using_colors_s$ID$ and not using_fire_s$ID$:\n\
        applyInflow(density=density_s$ID$, emission=inflow_s$ID$, heat=heat_s$ID$)\n\
    elif not using_heat_s$ID$ and using_colors_s$ID$ and using_fire_s$ID$:\n\
        applyInflow(density=density_s$ID$, emission=inflow_s$ID$, fuel=fuel_s$ID$, react=react_s$ID$, red=color_r_s$ID$, green=color_g_s$ID$, blue=color_b_s$ID$)\n\
    elif not using_heat_s$ID$ and using_colors_s$ID$ and not using_fire_s$ID$:\n\
        applyInflow(density=density_s$ID$, emission=inflow_s$ID$, red=color_r_s$ID$, green=color_g_s$ID$, blue=color_b_s$ID$)\n\
    elif not using_heat_s$ID$ and not using_colors_s$ID$ and using_fire_s$ID$:\n\
        applyInflow(density=density_s$ID$, emission=inflow_s$ID$, fuel=fuel_s$ID$, react=react_s$ID$)\n\
    else:\n\
        applyInflow(density=density_s$ID$, emission=inflow_s$ID$)\n";

const std::string smoke_inflow_high = "\n\
# TODO\n";

//////////////////////////////////////////////////////////////////////
// STANDALONE MODE
//////////////////////////////////////////////////////////////////////

const std::string smoke_standalone_load = "\n\
# import *.uni files\n\
path_prefix_$ID$ = '$MANTA_EXPORT_PATH$'\n\
load_smoke_data_low_$ID$(path_prefix_$ID$)\n\
if using_highres_s$ID$:\n\
    load_smoke_data_high_$ID$(path_prefix_$ID$)\n";
