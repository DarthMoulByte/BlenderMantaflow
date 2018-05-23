# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>
import bpy
import os
from copy import deepcopy
from bpy.types import Panel, Menu

from .properties_physics_common import (
    point_cache_ui,
    effector_weights_ui,
)

class SMOKE_MT_presets(Menu):
    bl_label = "Fluid Presets"
    preset_subdir = "smoke"
    preset_operator = "script.execute_preset"
    draw = Menu.draw_preset

class PhysicButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "physics"

    @classmethod
    def poll(cls, context):
        ob = context.object
        rd = context.scene.render
        return (ob and ob.type == 'MESH') and (rd.engine in cls.COMPAT_ENGINES) and (context.smoke)


class PHYSICS_PT_smoke(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Simulation"
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    def draw(self, context):
        layout = self.layout

        if not bpy.app.build_options.manta:
            layout.label("Built without Fluid Mantaflow modifier")
            return

        md = context.smoke
        ob = context.object
        scene = context.scene

        layout.row().prop(md, "smoke_type", expand=True)

        if md.smoke_type == 'DOMAIN':
            domain = md.domain_settings
            flow = md.flow_settings

            baking_any = domain.cache_baking_data or domain.cache_baking_mesh or domain.cache_baking_particles or domain.cache_baking_noise

            row = layout.row()
            row.enabled = not  domain.cache_baked_data and not baking_any
            row.prop(domain, "smoke_domain_type", expand=False)

            split = layout.split()
            split.enabled = not baking_any
            split.label(text="Border collisions:")

            split = layout.split()
            split.enabled = not baking_any

            col = split.column()
            col.prop(domain, "use_collision_border_front", text="Front")
            col.prop(domain, "use_collision_border_back", text="Back")

            col = split.column()
            col.prop(domain, "use_collision_border_right", text="Right")
            col.prop(domain, "use_collision_border_left", text="Left")

            col = split.column()
            col.prop(domain, "use_collision_border_top", text="Top")
            col.prop(domain, "use_collision_border_bottom", text="Bottom")

            split = layout.split()
            split.enabled = not baking_any

            col = split.column(align=True)
            col1 = col.column(align=True)
            col.label(text="Domain:")
            col.prop(domain, "resolution_max", text="Resolution")
            col.prop(domain, "time_scale", text="Time")
            col.prop(domain, "use_adaptive_stepping", text="Adaptive stepping")
            col.prop(domain, "cfl_condition", text="CFL")
            
            col = split.column()
            if scene.use_gravity:
                col.label(text="Use Scene Gravity", icon='SCENE_DATA')
                sub = col.column()
                sub.enabled = False
                sub.prop(domain, "gravity", text="")
            else:
                col.label(text="Gravity:")
                col.prop(domain, "gravity", text="")
            # TODO (sebas): Clipping var useful for manta openvdb caching?
            # col.prop(domain, "clipping")

            if domain.smoke_domain_type in {'GAS'}:
                split = layout.split()
                split.enabled = not baking_any

                col = split.column(align=True)
                col.label(text="Smoke:")
                col.prop(domain, "alpha")
                col.prop(domain, "beta", text="Temp. Diff.")
                col.prop(domain, "vorticity")
                col.prop(domain, "use_dissolve_smoke", text="Dissolve")
                sub = col.column()
                sub.active = domain.use_dissolve_smoke
                sub.prop(domain, "dissolve_speed", text="Time")
                sub.prop(domain, "use_dissolve_smoke_log", text="Slow")

                col = split.column(align=True)
                col.label(text="Fire:")
                col.prop(domain, "burning_rate")
                col.prop(domain, "flame_smoke")
                col.prop(domain, "flame_vorticity")
                col.prop(domain, "flame_ignition")
                col.prop(domain, "flame_max_temp")
                col.prop(domain, "flame_smoke_color")

            if domain.smoke_domain_type in {'LIQUID'}:
                split = layout.split()
                split.enabled = not baking_any

                col = split.column(align=True)
                col.label(text="Liquid:")
                col.prop(domain, "particle_maximum")
                col.prop(domain, "particle_minimum")
                col.prop(domain, "use_flip_particles", text="Show FLIP")

                col = split.column(align=True)
                col.label()
                col.prop(domain, "particle_number")
                col.prop(domain, "particle_band_width")
                col.prop(domain, "particle_randomness")

            split = layout.split()
            bake_incomplete = domain.cache_frame_pause_data
            if domain.cache_baked_data and not domain.cache_baking_data and bake_incomplete:
                col = split.column()
                col.operator("manta.bake_data", text="Resume")
                col = split.column()
                col.operator("manta.free_data", text="Free")
            elif not domain.cache_baked_data and domain.cache_baking_data:
                split.operator("manta.pause_bake", text="Pause Data")
            elif not domain.cache_baked_data and not domain.cache_baking_data:
                split.operator("manta.bake_data", text="Bake Data")
            else:
                split.operator("manta.free_data", text="Free Data")

        elif md.smoke_type == 'FLOW':
            flow = md.flow_settings

            layout.prop(flow, "smoke_flow_type", expand=False)

            split = layout.split()
            col = split.column()
                
            col.label(text="Sampling:")
            col.prop(flow, "subframes")

            col = split.column()
            
            col.label(text="Flow behavior:")
            col.prop(flow, "smoke_flow_behavior", expand=False, text="")

            if not flow.smoke_flow_behavior == 'OUTFLOW':

                split = layout.split()
                col = split.column()

                if flow.smoke_flow_type in {'SMOKE', 'BOTH', 'FIRE'}:
                    col.label(text="Initial Values:")
                if flow.smoke_flow_type in {'SMOKE', 'BOTH'}:
                    col.prop(flow, "density")
                    col.prop(flow, "temperature")
                if flow.smoke_flow_type in {'FIRE', 'BOTH'}:
                    col.prop(flow, "fuel_amount")

                col = split.column()

                if flow.smoke_flow_behavior in {'INFLOW'}:
                    col.prop(flow, "use_inflow")
                if flow.smoke_flow_type in {'SMOKE', 'BOTH', 'FIRE'}:
                    col.prop(flow, "use_absolute")
                if flow.smoke_flow_type in {'SMOKE', 'BOTH'}:
                    col.prop(flow, "smoke_color")

        elif md.smoke_type == 'EFFECTOR':
            effec = md.effec_settings

            layout.prop(effec, "effec_type")

            split = layout.split()
            col = split.column()

            col.label(text="Surface thickness:")
            col = split.column()

            col.prop(effec, "surface_distance")

class PHYSICS_PT_smoke_flow_source(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Source"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        return md and (md.smoke_type == 'FLOW')

    def draw(self, context):
        layout = self.layout
        ob = context.object
        flow = context.smoke.flow_settings
        
        split = layout.split()
        
        col = split.column()
        col.label(text="Flow source:")
        col.prop(flow, "smoke_flow_source", expand=False, text="")
        if flow.smoke_flow_source == 'MESH':
            col.prop(flow, "surface_distance")
            if flow.smoke_flow_type in {'SMOKE', 'BOTH', 'FIRE'}:
                col.prop(flow, "volume_density")
        if flow.smoke_flow_source == 'PARTICLES':
            col.prop_search(flow, "particle_system", ob, "particle_systems", text="")
            col.prop(flow, "use_particle_size", text="Set Size")
            sub = col.column()
            sub.active = flow.use_particle_size
            sub.prop(flow, "particle_size")

        col = split.column()
        col.label(text="Flow velocity:")
        col.prop(flow, "use_initial_velocity")

        sub = col.column()
        sub.active = flow.use_initial_velocity
        sub.prop(flow, "velocity_factor")
        if flow.smoke_flow_source == 'MESH':
            sub.prop(flow, "velocity_normal")
            #sub.prop(flow, "velocity_random")

class PHYSICS_PT_smoke_flow_advanced(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Flow Advanced"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        return md and (md.smoke_type == 'FLOW') and (md.flow_settings.smoke_flow_type in {'SMOKE', 'BOTH', 'FIRE'}) and (md.flow_settings.smoke_flow_source in {'MESH'})

    def draw(self, context):
        layout = self.layout
        ob = context.object
        flow = context.smoke.flow_settings

        split = layout.split()

        col = split.column()
        col.prop(flow, "use_texture")
        sub = col.column()
        sub.active = flow.use_texture
        sub.prop(flow, "noise_texture", text="")
        sub.label(text="Mapping:")
        sub.prop(flow, "texture_map_type", expand=False, text="")
        if flow.texture_map_type == 'UV':
            sub.prop_search(flow, "uv_layer", ob.data, "uv_textures", text="")
        if flow.texture_map_type == 'AUTO':
            sub.prop(flow, "texture_size")
        sub.prop(flow, "texture_offset")

        col = split.column()
        col.label(text="Vertex Group:")
        col.prop_search(flow, "density_vertex_group", ob, "vertex_groups", text="")

class PHYSICS_PT_smoke_adaptive_domain(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Adaptive Domain"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        # Adaptive domain only for smoke right now
        # TODO (sebbas): Disable for now - not working with new manta cache right now
        return False #md and (md.smoke_type == 'DOMAIN') and (md.domain_settings.smoke_domain_type in {'GAS'})

    def draw_header(self, context):
        md = context.smoke.domain_settings
        self.layout.prop(md, "use_adaptive_domain", text="")
        
    def draw(self, context):
        layout = self.layout

        domain = context.smoke.domain_settings
        layout.active = domain.use_adaptive_domain
        
        split = layout.split()
        baking_any = domain.cache_baking_data or domain.cache_baking_mesh or domain.cache_baking_particles or domain.cache_baking_noise
        split.enabled = not domain.cache_baked_data and not baking_any

        col = split.column(align=True)
        col.label(text="Resolution:")
        col.prop(domain, "additional_res")
        col.prop(domain, "adapt_margin")

        col = split.column(align=True)
        col.label(text="Advanced:")
        col.prop(domain, "adapt_threshold")


class PHYSICS_PT_smoke_quality(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Quality"
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        # Disable for now
        return False #md and (md.smoke_type == 'DOMAIN') and (rd.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout
        domain = context.smoke.domain_settings

        split = layout.split()

        col = split.column()
        # TODO (sebbas): Disabling render display switch for now. Needs some more consideration
        col.label(text="Render Display:")
        col.prop(domain, "render_display_mode", text="")

        col = split.column()
        col.label(text="Viewport Display:")
        col.prop(domain, "viewport_display_mode", text="")

class PHYSICS_PT_smoke_noise(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Noise"
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        return md and (md.smoke_type == 'DOMAIN') and (md.domain_settings.smoke_domain_type in {'GAS'})

    def draw_header(self, context):
        md = context.smoke.domain_settings
        domain = context.smoke.domain_settings
        baking_any = domain.cache_baking_data or domain.cache_baking_mesh or domain.cache_baking_particles or domain.cache_baking_noise
        self.layout.enabled = not baking_any
        self.layout.prop(md, "use_noise", text="")

    def draw(self, context):
        layout = self.layout
        domain = context.smoke.domain_settings

        layout.active = domain.use_noise

        baking_any = domain.cache_baking_data or domain.cache_baking_mesh or domain.cache_baking_particles or domain.cache_baking_noise

        split = layout.split()
        split.enabled = not baking_any

        col = split.column(align=True)
        col.prop(domain, "noise_scale", text="Upres")
        # TODO (sebbas): Mantaflow only supports wavelet noise. Do we really need fft noise? Maybe get rid of noise type ...
        col.label(text="Noise Method:")
        col.prop(domain, "noise_type", text="")

        col = split.column(align=True)
        col.prop(domain, "strength")
        col.prop(domain, "noise_pos_scale")
        col.prop(domain, "noise_time_anim")

        split = layout.split()
        split.enabled = domain.cache_baked_data
        bake_incomplete = domain.cache_frame_pause_noise
        if domain.cache_baked_noise and not domain.cache_baking_noise and bake_incomplete:
            col = split.column()
            col.operator("manta.bake_noise", text="Resume")
            col = split.column()
            col.operator("manta.free_noise", text="Free")
        elif not domain.cache_baked_noise and domain.cache_baking_noise:
            split.operator("manta.pause_bake", text="Pause Noise")
        elif not domain.cache_baked_noise and not domain.cache_baking_noise:
            split.operator("manta.bake_noise", text="Bake Noise")
        else:
            split.operator("manta.free_noise", text="Free Noise")

class PHYSICS_PT_smoke_mesh(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Mesh"
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        return md and (md.smoke_type == 'DOMAIN') and (md.domain_settings.smoke_domain_type in {'LIQUID'})

    def draw_header(self, context):
        md = context.smoke.domain_settings
        domain = context.smoke.domain_settings
        baking_any = domain.cache_baking_data or domain.cache_baking_mesh or domain.cache_baking_particles or domain.cache_baking_noise
        self.layout.enabled = not baking_any
        self.layout.prop(md, "use_mesh", text="")

    def draw(self, context):
        layout = self.layout
        domain = context.smoke.domain_settings

        layout.active = domain.use_mesh

        baking_any = domain.cache_baking_data or domain.cache_baking_mesh or domain.cache_baking_particles or domain.cache_baking_noise

        split = layout.split()
        split.enabled = not baking_any

        col = split.column(align=True)
        col.prop(domain, "mesh_scale", text="Upres")
        col.prop(domain, "particle_radius")

        col = split.column(align=True)
        col.label(text="Generator:")
        col.prop(domain, "mesh_generator", text="")

        if domain.mesh_generator in {'IMPROVED'}:
            split = layout.split()
            split.enabled = not baking_any

            col = split.column(align=True)
            col.label(text="Smoothening")
            col.prop(domain, "mesh_smoothen_pos")
            col.prop(domain, "mesh_smoothen_neg")

            col = split.column(align=True)
            col.label(text="")
            col.prop(domain, "mesh_smoothen_upper")
            col.prop(domain, "mesh_smoothen_lower")

        # TODO (sebbas): for now just interpolate any upres grids, ie not sampling highres grids 
        #col = split.column()
        #col.label(text="Flow Sampling:")
        #col.prop(domain, "highres_sampling", text="")

        split = layout.split()
        split.enabled = domain.cache_baked_data
        bake_incomplete = domain.cache_frame_pause_mesh
        if domain.cache_baked_mesh and not domain.cache_baking_mesh and bake_incomplete:
            col = split.column()
            col.operator("manta.bake_mesh", text="Resume")
            col = split.column()
            col.operator("manta.free_mesh", text="Free")
        elif not domain.cache_baked_mesh and domain.cache_baking_mesh:
            split.operator("manta.pause_bake", text="Pause Mesh")
        elif not domain.cache_baked_mesh and not domain.cache_baking_mesh:
            split.operator("manta.bake_mesh", text="Bake Mesh")
        else:
            split.operator("manta.free_mesh", text="Free Mesh")

class PHYSICS_PT_smoke_particles(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Particles"
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        # Fluid particles only enabled for liquids for now. Future update might include particles for gas domain, e.g. fire sparks.
        return md and (md.smoke_type == 'DOMAIN') and (rd.engine in cls.COMPAT_ENGINES) and (md.domain_settings.smoke_domain_type in {'LIQUID'}) 

    def draw(self, context):
        layout = self.layout
        domain = context.smoke.domain_settings

        baking_any = domain.cache_baking_data or domain.cache_baking_mesh or domain.cache_baking_particles or domain.cache_baking_noise

        split = layout.split()
        split.prop(domain, "particle_scale", text="Upres")
        split.enabled = not baking_any

        split = layout.split()
        split.enabled = not baking_any

        col = split.column()
        col.enabled = not domain.point_cache.is_baked
        col.prop(domain, "use_spray_particles", text="Drop")
        col.prop(domain, "use_spray_particles", text="Drop")
        sub = col.column(align=True)
        sub.active = domain.use_spray_particles
        sub.prop(domain, "particle_droplet_threshold", text="Threshold")
        sub.prop(domain, "particle_droplet_amount", text="Generate")
        sub.prop(domain, "particle_droplet_life", text="Life")
        sub.prop(domain, "particle_droplet_max", text="Maximum")
        sub2 = col.column()
        sub2.active = domain.use_spray_particles
        sub2.prop(domain, "use_bubble_particles", text="Bubble")
        sub3 = col.column(align=True)
        sub3.active = domain.use_spray_particles and domain.use_bubble_particles
        sub3.prop(domain, "particle_bubble_rise", text="Rise")
        sub3.prop(domain, "particle_bubble_life", text="Life")
        sub3.prop(domain, "particle_bubble_max", text="Maximum")

        col = split.column()
        col.prop(domain, "use_foam_particles", text="Float")
        sub = col.column(align=True)
        sub.active = domain.use_foam_particles
        sub.prop(domain, "particle_floater_amount", text="Generate")
        sub.prop(domain, "particle_floater_life", text="Life")
        sub.prop(domain, "particle_floater_max", text="Maximum")
        col.prop(domain, "use_tracer_particles", text="Tracer")
        sub2 = col.column(align=True)
        sub2.active = domain.use_tracer_particles
        sub2.prop(domain, "particle_tracer_amount", text="Amount")
        sub2.prop(domain, "particle_tracer_life", text="Life")
        sub2.prop(domain, "particle_tracer_max", text="Maximum")
        sub3 = col.column()
        sub3.prop(domain, "use_flip_particles", text="FLIP")

        split = layout.split()
        split.enabled = domain.cache_baked_data and (domain.use_drop_particles or domain.use_bubble_particles or domain.use_floater_particles or domain.use_tracer_particles)
        bake_incomplete = domain.cache_frame_pause_particles
        if domain.cache_baked_particles and not domain.cache_baking_particles and bake_incomplete:
            col = split.column()
            col.operator("manta.bake_particles", text="Resume")
            col = split.column()
            col.operator("manta.free_particles", text="Free")
        elif not domain.cache_baked_particles and domain.cache_baking_particles:
            split.operator("manta.pause_bake", text="Pause Particles")
        elif not domain.cache_baked_particles and not domain.cache_baking_particles:
            split.operator("manta.bake_particles", text="Bake Particles")
        else:
            split.operator("manta.free_particles", text="Free Particles")


class PHYSICS_PT_smoke_secondary_particles(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Secondary Particles"
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        # Fluid particles only enabled for liquids for now. Future update might include particles for gas domain, e.g. fire sparks.
        return md and (md.smoke_type == 'DOMAIN') and (rd.engine in cls.COMPAT_ENGINES) and (md.domain_settings.smoke_domain_type in {'LIQUID'}) 

    def draw(self, context):
        layout = self.layout
        domain = context.smoke.domain_settings

        top = layout.column()
        top.enabled = not domain.cache_baked_particles_low
        top.label("Exported Particles:")
        sub1 = top.row()
        sub1.prop(domain, "use_flip_particles", text="FLIP")
        subSpray = sub1.column()
        subSpray.enabled = (domain.sndparticle_combined_export == 'OFF') or (domain.sndparticle_combined_export == 'FOAM + BUBBLES')
        subSpray.prop(domain, "use_spray_particles", text="Spray")
        subFoam = sub1.column()
        subFoam.enabled = (domain.sndparticle_combined_export == 'OFF') or (domain.sndparticle_combined_export == 'SPRAY + BUBBLES')
        subFoam.prop(domain, "use_foam_particles", text="Foam")
        subBubbles = sub1.column()
        subBubbles.enabled = (domain.sndparticle_combined_export == 'OFF') or (domain.sndparticle_combined_export == 'SPRAY + FOAM')
        subBubbles.prop(domain, "use_bubble_particles", text="Bubbles")
        middle = top.row()
        middle.enabled = domain.use_spray_particles or domain.use_foam_particles or domain.use_bubble_particles
        middle.prop(domain, "sndparticle_combined_export")

        split = layout.split()
        split.enabled = not domain.cache_baked_particles_low
        sub1 = split.column()
        sub1.enabled = domain.use_spray_particles or domain.use_foam_particles or domain.use_bubble_particles
        sub2 = sub1.column(align=True)
        sub2.label(text="Wave Crest Potential:")
        sub2.prop(domain, "sndparticle_tau_min_wc", text="min")
        sub2.prop(domain, "sndparticle_tau_max_wc", text="max")
        sub3 = sub1.column(align=True)
        sub3.label(text="Trapped Air Potential:")
        sub3.prop(domain, "sndparticle_tau_min_ta", text="min")
        sub3.prop(domain, "sndparticle_tau_max_ta", text="max")
        sub4 = sub1.column(align=True)
        sub4.label(text="Kinetic Energy Potential:")
        sub4.prop(domain, "sndparticle_tau_min_k", text="min")
        sub4.prop(domain, "sndparticle_tau_max_k", text="max")
        sub1.label("Potential Resolution:")
        sub1.prop(domain, "sndparticle_potential_resolution", text="")
        sub1.label("Particles in Boundary:")
        sub1.prop(domain, "sndparticle_boundary", text="")

        second = split.column()
        sub1 = second.column()
        sub1.enabled = domain.use_spray_particles or domain.use_foam_particles or domain.use_bubble_particles
        sub2 = sub1.column(align=True)
        sub2.label(text="Particle Sampling:")
        sub2.prop(domain, "sndparticle_k_wc", text="Wave Crests")
        sub2.prop(domain, "sndparticle_k_ta", text="Trapped Air")
        sub3 = sub1.column(align=True)
        sub3.label(text="Particle Lifetime:")
        sub3.prop(domain, "sndparticle_l_min", text="min")
        sub3.prop(domain, "sndparticle_l_max", text="max")
        sub4 = sub1.column(align=True)
        sub4.label(text="Bubble Movement:")
        sub4.prop(domain, "sndparticle_k_b", text="Buoyancy")
        sub4.prop(domain, "sndparticle_k_d", text="Drag")
        sub1.label("Potential Quality:")
        sub1.prop(domain, "sndparticle_potential_quality", text="")
        sub1.label("Save Potential Grids:")
        sub1.prop(domain, "sndparticle_potential_grid_save", text="")

        #bottom = layout.column()
        #bottom.enabled = domain.use_spray_particles or domain.use_foam_particles or domain.use_bubble_particles
        

class PHYSICS_PT_smoke_diffusion(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Diffusion"
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        # Fluid diffusion only enabled for liquids (surface tension and viscosity not relevant for smoke)
        return md and (md.smoke_type == 'DOMAIN') and (rd.engine in cls.COMPAT_ENGINES) and (md.domain_settings.smoke_domain_type in {'LIQUID'}) 

    def draw(self, context):
        layout = self.layout
        domain = context.smoke.domain_settings

        split = layout.split()
        baking_any = domain.cache_baking_data or domain.cache_baking_mesh or domain.cache_baking_particles or domain.cache_baking_noise
        split.enabled = not baking_any

        col = split.column()

        col.label(text="Viscosity Presets:")
        sub = col.row(align=True)
        sub.menu("SMOKE_MT_presets", text=bpy.types.SMOKE_MT_presets.bl_label)
        sub.operator("smoke.preset_add", text="", icon='ZOOMIN')
        sub.operator("smoke.preset_add", text="", icon='ZOOMOUT').remove_active = True

        sub = col.column(align=True)
        sub.prop(domain, "viscosity_base", text="Base")
        sub.prop(domain, "viscosity_exponent", text="Exponent", slider=True)

        col = split.column()
        col.label(text="Real World Size:")
        col.prop(domain, "domain_size", text="Meters")
        col.label(text="Surface tension:")
        col.prop(domain, "surface_tension", text="Tension")

class PHYSICS_PT_smoke_guiding(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Guiding"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        return md and (md.smoke_type == 'DOMAIN') and (rd.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout
        domain = context.smoke.domain_settings

        split = layout.split()
        baking_any = domain.cache_baking_data or domain.cache_baking_mesh or domain.cache_baking_particles or domain.cache_baking_noise
        split.enabled = not baking_any

        col = split.column()
        col.prop(domain, "guiding_alpha", text="Weight")

        col = split.column()
        col.prop(domain, "guiding_beta", text="Size")

class PHYSICS_PT_smoke_groups(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Groups"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        return md and (md.smoke_type == 'DOMAIN') and (rd.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout
        domain = context.smoke.domain_settings

        split = layout.split()

        col = split.column()
        col.label(text="Flow Group:")
        col.prop(domain, "fluid_group", text="")

        #col.label(text="Effector Group:")
        #col.prop(domain, "effector_group", text="")

        col = split.column()
        col.label(text="Collision Group:")
        col.prop(domain, "collision_group", text="")

class PHYSICS_PT_smoke_cache(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Cache"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        # TODO (sebbas): Merge old cache with new cache functionality
        return False #md and (md.smoke_type == 'DOMAIN') and (rd.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout

        domain = context.smoke.domain_settings
        cache_surface_format = domain.cache_surface_format
        cache_volume_format = domain.cache_volume_format

        split = layout.split()

        col = split.column()
        col.prop(domain, "use_surface_cache", text="Surface format:")
        sub = col.column()
        sub.active = domain.use_surface_cache
        sub.prop(domain, "cache_surface_format", text="")

        col = split.column()
        col.prop(domain, "use_volume_cache", text="Volumetric format:")
        sub = col.column()
        sub.active = domain.use_volume_cache
        sub.prop(domain, "cache_volume_format", text="")

        split = layout.split()

        if cache_volume_format == 'POINTCACHE':
            layout.label(text="Compression:")
            layout.row().prop(domain, "point_cache_compress_type", expand=True)
        elif cache_volume_format == 'OPENVDB':
            if not bpy.app.build_options.openvdb:
                layout.label("Built without OpenVDB support")
                return

            layout.label(text="Compression:")
            layout.row().prop(domain, "openvdb_cache_compress_type", expand=True)
            row = layout.row()
            row.label("Data Depth:")
            row.prop(domain, "data_depth", expand=True, text="Data Depth")

        cache = domain.point_cache
        point_cache_ui(self, context, cache, (cache.is_baked is False), 'SMOKE')

class PHYSICS_PT_manta_cache(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Cache"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        return md and (md.smoke_type == 'DOMAIN') and (rd.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout
        md = context.smoke
        domain = context.smoke.domain_settings

        split = layout.split()
        split.prop(domain, "cache_directory", text="")

        row = layout.row(align=True)
        row.prop(domain, "cache_frame_start")
        row.prop(domain, "cache_frame_end")

        split = layout.split()

        row = layout.row(align=True)
        row.label(text="Data file format:")
        row.prop(domain, "cache_volume_format", text="")

        if md.domain_settings.smoke_domain_type in {'GAS'}:
            if domain.use_noise:
                row = layout.row(align=True)
                row.label(text="Noise file format:")
                row.prop(domain, "cache_noise_format", text="")

        if md.domain_settings.smoke_domain_type in {'LIQUID'}:
            # File format for all particle systemes (FLIP and secondary)
            row = layout.row(align=True)
            row.label(text="Particle file format:")
            row.prop(domain, "cache_particle_format", text="")

            if domain.use_mesh:
                row = layout.row(align=True)
                row.label(text="Mesh file format:")
                row.prop(domain, "cache_surface_format", text="")


class PHYSICS_PT_smoke_field_weights(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Field Weights"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_RENDER'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        rd = context.scene.render
        return md and (md.smoke_type == 'DOMAIN') and (rd.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        domain = context.smoke.domain_settings
        effector_weights_ui(self, context, domain.effector_weights, 'SMOKE')

class PHYSICS_PT_smoke_export_manta(PhysicButtonsPanel, Panel):
    bl_label = "Fluid Export"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        md = context.smoke
        return md and (md.smoke_type == 'DOMAIN')
        
    def draw(self, context):
        layout = self.layout
        
        domain = context.smoke.domain_settings
        split = layout.split()
        split.operator("manta.make_file", text="Export Mantaflow Script")
        split = layout.split()
        split.prop(domain, "manta_filepath")
        split = layout.split()

class PHYSICS_PT_smoke_display_settings(PhysicButtonsPanel, Panel):
    bl_label = "Smoke Display Settings"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        md = context.smoke

        rd = context.scene.render
        return md and (md.smoke_type == 'DOMAIN') and (not rd.use_game_engine) and (md.domain_settings.smoke_domain_type in {'GAS'})

    def draw(self, context):
        domain = context.smoke.domain_settings
        layout = self.layout

        layout.prop(domain, "display_thickness")

        layout.separator()
        layout.label(text="Slicing:")
        layout.prop(domain, "slice_method")

        slice_method = domain.slice_method
        axis_slice_method = domain.axis_slice_method

        do_axis_slicing = (slice_method == 'AXIS_ALIGNED')
        do_full_slicing = (axis_slice_method == 'FULL')

        row = layout.row()
        row.enabled = do_axis_slicing
        row.prop(domain, "axis_slice_method")

        col = layout.column()
        col.enabled = not do_full_slicing and do_axis_slicing
        col.prop(domain, "slice_axis")
        col.prop(domain, "slice_depth")

        row = layout.row()
        row.enabled = do_full_slicing or not do_axis_slicing
        row.prop(domain, "slice_per_voxel")

        layout.separator()
        layout.label(text="Debug:")
        layout.prop(domain, "draw_velocity")
        col = layout.column()
        col.enabled = domain.draw_velocity
        col.prop(domain, "vector_draw_type")
        col.prop(domain, "vector_scale")

        layout.separator()
        layout.label(text="Color Mapping:")
        layout.prop(domain, "use_color_ramp")
        col = layout.column()
        col.enabled = domain.use_color_ramp
        col.prop(domain, "coba_field")
        col.template_color_ramp(domain, "color_ramp", expand=True)

class PHYSICS_PT_liquid_display_settings(PhysicButtonsPanel, Panel):
    bl_label = "Liquid Display Settings"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        md = context.smoke

        rd = context.scene.render
        return md and (md.smoke_type == 'DOMAIN') and (not rd.use_game_engine) and (md.domain_settings.smoke_domain_type in {'LIQUID'})

    def draw(self, context):
        domain = context.smoke.domain_settings
        layout = self.layout
        layout.enabled = domain.cache_baked_particles_low
        split = layout.split()
        first = split.column()
        first.prop(domain, "use_color_ramp", text="Enable Display")
        second = split.column()
        second.enabled = domain.use_color_ramp and (domain.sndparticle_potential_grid_save == 'LOW ONLY' or domain.sndparticle_potential_grid_save == 'ON')
        second.prop(domain, "coba_field_liquid", text="")


        split = layout.split()
        first = split.column()
        first.enabled = domain.use_color_ramp
        first.label("Method:")
        first.prop(domain, "axis_slice_method", text="")
        first2 = first.column()
        first2.enabled = (domain.axis_slice_method == 'FULL')
        first2.prop(domain, "slice_per_voxel", text="Slices per Voxel")
        second = split.column()
        second.enabled =  domain.use_color_ramp and (domain.axis_slice_method == 'SINGLE')
        second.label("Axis:")
        second.prop(domain, "slice_axis", text="")
        second.prop(domain, "slice_depth")


        layout.label()
        split = layout.split()
        first = split.column()
        first.enabled = domain.use_color_ramp and False #disable for now, until velocity components (x_vel, y_vel and z_vel) are saved and loaded or removed
        first.prop(domain, "draw_velocity")
        second = split.column()
        second.enabled = domain.draw_velocity and domain.use_color_ramp
        second.prop(domain, "vector_draw_type", text="")
        sub4 = layout.column()
        sub4.enabled = domain.draw_velocity and domain.use_color_ramp
        sub4.prop(domain, "vector_scale")

        layout.label()
        sub5 = layout.column()
        sub5.enabled = domain.use_color_ramp
        split = sub5.split(percentage=0.33)
        first = split.column() 
        first.label(text="Color Mapping:")
        second = split.column()
        second.prop(domain, "display_thickness", text="Density")
        sub5.template_color_ramp(domain, "color_ramp", expand=True)

        sub6 = layout.row(align=True)
        sub6.enabled = domain.use_color_ramp and (domain.coba_field_liquid == "PHI")
        sub6.label(text="Field Remapping:")
        sub6.prop(domain, "coba_field_remap_offset")
        sub6.prop(domain, "coba_field_remap_slope")



classes = (
    SMOKE_MT_presets,
    PHYSICS_PT_smoke,
    PHYSICS_PT_smoke_flow_source,
    PHYSICS_PT_smoke_flow_advanced,
    PHYSICS_PT_smoke_adaptive_domain,
    PHYSICS_PT_smoke_quality,
    PHYSICS_PT_smoke_noise,
    PHYSICS_PT_smoke_mesh,
    PHYSICS_PT_smoke_particles,
    PHYSICS_PT_smoke_secondary_particles,
    PHYSICS_PT_smoke_diffusion,
    PHYSICS_PT_smoke_guiding,
    PHYSICS_PT_smoke_groups,
    PHYSICS_PT_smoke_cache,
    PHYSICS_PT_manta_cache,
    PHYSICS_PT_smoke_field_weights,
    PHYSICS_PT_smoke_export_manta,
    PHYSICS_PT_smoke_display_settings,
    PHYSICS_PT_liquid_display_settings,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
