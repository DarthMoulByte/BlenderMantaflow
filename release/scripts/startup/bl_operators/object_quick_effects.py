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

# <pep8-80 compliant>

from mathutils import Vector
import bpy
from bpy.types import Operator
from bpy.props import (
    BoolProperty,
    EnumProperty,
    FloatProperty,
    FloatVectorProperty,
    IntProperty,
)


def object_ensure_material(obj, mat_name):
    """ Use an existing material or add a new one.
    """
    mat = mat_slot = None
    for mat_slot in obj.material_slots:
        mat = mat_slot.material
        if mat:
            break
    if mat is None:
        mat = bpy.data.materials.new(mat_name)
        if mat_slot:
            mat_slot.material = mat
        else:
            obj.data.materials.append(mat)
    return mat


class QuickFur(Operator):
    bl_idname = "object.quick_fur"
    bl_label = "Quick Fur"
    bl_options = {'REGISTER', 'UNDO'}

    density = EnumProperty(
            name="Fur Density",
            items=(('LIGHT', "Light", ""),
                   ('MEDIUM', "Medium", ""),
                   ('HEAVY', "Heavy", "")),
            default='MEDIUM',
            )
    view_percentage = IntProperty(
            name="View %",
            min=1, max=100,
            soft_min=1, soft_max=100,
            default=10,
            )
    length = FloatProperty(
            name="Length",
            min=0.001, max=100,
            soft_min=0.01, soft_max=10,
            default=0.1,
            )

    def execute(self, context):
        fake_context = context.copy()
        mesh_objects = [obj for obj in context.selected_objects
                        if obj.type == 'MESH' and obj.mode == 'OBJECT']

        if not mesh_objects:
            self.report({'ERROR'}, "Select at least one mesh object")
            return {'CANCELLED'}

        mat = bpy.data.materials.new("Fur Material")
        mat.strand.tip_size = 0.25
        mat.strand.blend_distance = 0.5

        for obj in mesh_objects:
            fake_context["object"] = obj
            bpy.ops.object.particle_system_add(fake_context)

            psys = obj.particle_systems[-1]
            psys.settings.type = 'HAIR'

            if self.density == 'LIGHT':
                psys.settings.count = 100
            elif self.density == 'MEDIUM':
                psys.settings.count = 1000
            elif self.density == 'HEAVY':
                psys.settings.count = 10000

            psys.settings.child_nbr = self.view_percentage
            psys.settings.hair_length = self.length
            psys.settings.use_strand_primitive = True
            psys.settings.use_hair_bspline = True
            psys.settings.child_type = 'INTERPOLATED'

            obj.data.materials.append(mat)
            psys.settings.material = len(obj.data.materials)

        return {'FINISHED'}


class QuickExplode(Operator):
    bl_idname = "object.quick_explode"
    bl_label = "Quick Explode"
    bl_options = {'REGISTER', 'UNDO'}

    style = EnumProperty(
            name="Explode Style",
            items=(('EXPLODE', "Explode", ""),
                   ('BLEND', "Blend", "")),
            default='EXPLODE',
            )
    amount = IntProperty(
            name="Amount of pieces",
            min=2, max=10000,
            soft_min=2, soft_max=10000,
            default=100,
            )
    frame_duration = IntProperty(
            name="Duration",
            min=1, max=300000,
            soft_min=1, soft_max=10000,
            default=50,
            )

    frame_start = IntProperty(
            name="Start Frame",
            min=1, max=300000,
            soft_min=1, soft_max=10000,
            default=1,
            )
    frame_end = IntProperty(
            name="End Frame",
            min=1, max=300000,
            soft_min=1, soft_max=10000,
            default=10,
            )

    velocity = FloatProperty(
            name="Outwards Velocity",
            min=0, max=300000,
            soft_min=0, soft_max=10,
            default=1,
            )

    fade = BoolProperty(
            name="Fade",
            description="Fade the pieces over time",
            default=True,
            )

    def execute(self, context):
        fake_context = context.copy()
        obj_act = context.active_object

        if obj_act is None or obj_act.type != 'MESH':
            self.report({'ERROR'}, "Active object is not a mesh")
            return {'CANCELLED'}

        mesh_objects = [obj for obj in context.selected_objects
                        if obj.type == 'MESH' and obj != obj_act]
        mesh_objects.insert(0, obj_act)

        if self.style == 'BLEND' and len(mesh_objects) != 2:
            self.report({'ERROR'}, "Select two mesh objects")
            self.style = 'EXPLODE'
            return {'CANCELLED'}
        elif not mesh_objects:
            self.report({'ERROR'}, "Select at least one mesh object")
            return {'CANCELLED'}

        for obj in mesh_objects:
            if obj.particle_systems:
                self.report({'ERROR'},
                            "Object %r already has a "
                            "particle system" % obj.name)

                return {'CANCELLED'}

        if self.fade:
            tex = bpy.data.textures.new("Explode fade", 'BLEND')
            tex.use_color_ramp = True

            if self.style == 'BLEND':
                tex.color_ramp.elements[0].position = 0.333
                tex.color_ramp.elements[1].position = 0.666

            tex.color_ramp.elements[0].color[3] = 1.0
            tex.color_ramp.elements[1].color[3] = 0.0

        if self.style == 'BLEND':
            from_obj = mesh_objects[1]
            to_obj = mesh_objects[0]

        for obj in mesh_objects:
            fake_context["object"] = obj
            bpy.ops.object.particle_system_add(fake_context)

            settings = obj.particle_systems[-1].settings
            settings.count = self.amount
            # first set frame end, to prevent frame start clamping
            settings.frame_end = self.frame_end - self.frame_duration
            settings.frame_start = self.frame_start
            settings.lifetime = self.frame_duration
            settings.normal_factor = self.velocity
            settings.render_type = 'NONE'

            explode = obj.modifiers.new(name='Explode', type='EXPLODE')
            explode.use_edge_cut = True

            if self.fade:
                explode.show_dead = False
                uv = obj.data.uv_textures.new("Explode fade")
                explode.particle_uv = uv.name

                mat = object_ensure_material(obj, "Explode Fade")

                mat.use_transparency = True
                mat.use_transparent_shadows = True
                mat.alpha = 0.0
                mat.specular_alpha = 0.0

                tex_slot = mat.texture_slots.add()

                tex_slot.texture = tex
                tex_slot.texture_coords = 'UV'
                tex_slot.uv_layer = uv.name

                tex_slot.use_map_alpha = True

                if self.style == 'BLEND':
                    if obj == to_obj:
                        tex_slot.alpha_factor = -1.0
                        elem = tex.color_ramp.elements[1]
                    else:
                        elem = tex.color_ramp.elements[0]
                    # Keep already defined alpha!
                    elem.color[:3] = mat.diffuse_color
                else:
                    tex_slot.use_map_color_diffuse = False

            if self.style == 'BLEND':
                settings.physics_type = 'KEYED'
                settings.use_emit_random = False
                settings.rotation_mode = 'NOR'

                psys = obj.particle_systems[-1]

                fake_context["particle_system"] = obj.particle_systems[-1]
                bpy.ops.particle.new_target(fake_context)
                bpy.ops.particle.new_target(fake_context)

                if obj == from_obj:
                    psys.targets[1].object = to_obj
                else:
                    psys.targets[0].object = from_obj
                    settings.normal_factor = -self.velocity
                    explode.show_unborn = False
                    explode.show_dead = True
            else:
                settings.factor_random = self.velocity
                settings.angular_velocity_factor = self.velocity / 10.0

        return {'FINISHED'}

    def invoke(self, context, event):
        self.frame_start = context.scene.frame_current
        self.frame_end = self.frame_start + self.frame_duration
        return self.execute(context)


def obj_bb_minmax(obj, min_co, max_co):
    for i in range(0, 8):
        bb_vec = obj.matrix_world * Vector(obj.bound_box[i])

        min_co[0] = min(bb_vec[0], min_co[0])
        min_co[1] = min(bb_vec[1], min_co[1])
        min_co[2] = min(bb_vec[2], min_co[2])
        max_co[0] = max(bb_vec[0], max_co[0])
        max_co[1] = max(bb_vec[1], max_co[1])
        max_co[2] = max(bb_vec[2], max_co[2])


def grid_location(x, y):
    return (x * 200, y * 150)


class QuickSmoke(Operator):
    bl_idname = "object.quick_smoke"
    bl_label = "Quick Smoke"
    bl_options = {'REGISTER', 'UNDO'}

    style = EnumProperty(
            name="Smoke Style",
            items=(('SMOKE', "Smoke", ""),
                   ('FIRE', "Fire", ""),
                   ('BOTH', "Smoke + Fire", ""),
                   ),
            default='SMOKE',
            )

    show_flows = BoolProperty(
            name="Render Smoke Objects",
            description="Keep the smoke objects visible during rendering",
            default=False,
            )

    def execute(self, context):
        if not bpy.app.build_options.manta:
            self.report({'ERROR'}, "Built without Fluid Mantaflow modifier")
            return {'CANCELLED'}

        fake_context = context.copy()
        mesh_objects = [obj for obj in context.selected_objects
                        if obj.type == 'MESH']
        min_co = Vector((100000.0, 100000.0, 100000.0))
        max_co = -min_co

        if not mesh_objects:
            self.report({'ERROR'}, "Select at least one mesh object")
            return {'CANCELLED'}

        for obj in mesh_objects:
            fake_context["object"] = obj
            # make each selected object a smoke flow
            bpy.ops.object.modifier_add(fake_context, type='SMOKE')
            obj.modifiers[-1].smoke_type = 'FLOW'

            # set type
            obj.modifiers[-1].flow_settings.smoke_flow_type = self.style

            if not self.show_flows:
                obj.draw_type = 'WIRE'

            # store bounding box min/max for the domain object
            obj_bb_minmax(obj, min_co, max_co)

        # add the smoke domain object
        bpy.ops.mesh.primitive_cube_add()
        obj = context.active_object
        obj.name = "Smoke Domain"

        # give the smoke some room above the flows
        obj.location = 0.5 * (max_co + min_co) + Vector((0.0, 0.0, 1.0))
        obj.scale = 0.5 * (max_co - min_co) + Vector((1.0, 1.0, 2.0))

        # setup smoke domain
        bpy.ops.object.modifier_add(type='SMOKE')
        obj.modifiers[-1].smoke_type = 'DOMAIN'

        # set correct cache file format for smoke
        obj.modifiers[-1].domain_settings.use_surface_cache = False
        obj.modifiers[-1].domain_settings.use_volume_cache = True
        obj.modifiers[-1].domain_settings.cache_volume_format = 'POINTCACHE'

		# Setup material

        # Cycles
        if context.scene.render.use_shading_nodes:
            bpy.ops.object.material_slot_add()

            mat = bpy.data.materials.new("Smoke Domain Material")
            obj.material_slots[0].material = mat

            # Make sure we use nodes
            mat.use_nodes = True

            # Set node variables and clear the default nodes
            tree = mat.node_tree
            nodes = tree.nodes
            links = tree.links

            nodes.clear()

            # Create shader nodes

            # Material output
            node_out = nodes.new(type='ShaderNodeOutputMaterial')
            node_out.location = grid_location(6, 1)

            # Add Principled Volume
            node_principled = nodes.new(type='ShaderNodeVolumePrincipled')
            node_principled.location = grid_location(4, 1)
            links.new(node_principled.outputs["Volume"],
                    node_out.inputs["Volume"])

            node_principled.inputs["Density"].default_value = 5.0

            if self.style in {'FIRE', 'BOTH'}:
                node_principled.inputs["Blackbody Intensity"].default_value = 1.0

        # Blender Internal
        else:
            # create a volume material with a voxel data texture for the domain
            bpy.ops.object.material_slot_add()

            mat = bpy.data.materials.new("Smoke Domain Material")
            obj.material_slots[0].material = mat
            mat.type = 'VOLUME'
            mat.volume.density = 0
            mat.volume.density_scale = 5
            mat.volume.step_size = 0.1

            tex = bpy.data.textures.new("Smoke Density", 'VOXEL_DATA')
            tex.voxel_data.domain_object = obj
            tex.voxel_data.interpolation = 'TRICUBIC_BSPLINE'

            tex_slot = mat.texture_slots.add()
            tex_slot.texture = tex
            tex_slot.texture_coords = 'ORCO'
            tex_slot.use_map_color_emission = False
            tex_slot.use_map_density = True
            tex_slot.use_map_color_reflection = True

            # for fire add a second texture for flame emission
            mat.volume.emission_color = Vector((0.0, 0.0, 0.0))
            tex = bpy.data.textures.new("Flame", 'VOXEL_DATA')
            tex.voxel_data.domain_object = obj
            tex.voxel_data.smoke_data_type = 'SMOKEFLAME'
            tex.voxel_data.interpolation = 'TRICUBIC_BSPLINE'
            tex.use_color_ramp = True

            tex_slot = mat.texture_slots.add()
            tex_slot.texture = tex
            tex_slot.texture_coords = 'ORCO'

            # add color ramp for flame color
            ramp = tex.color_ramp
            # dark orange
            elem = ramp.elements.new(0.333)
            elem.color = (0.2, 0.03, 0.0, 1.0)

            # yellow glow
            elem = ramp.elements.new(0.666)
            elem.color = (1, 0.65, 0.25, 1.0)

            mat.texture_slots[1].use_map_density = True
            mat.texture_slots[1].use_map_emission = True
            mat.texture_slots[1].emission_factor = 5

        return {'FINISHED'}


class QuickFluid(Operator):
    bl_idname = "object.quick_fluid"
    bl_label = "Quick Fluid"
    bl_options = {'REGISTER', 'UNDO'}

    style = EnumProperty(
            name="Fluid Style",
            items=(('INFLOW', "Inflow", ""),
                   ('BASIC', "Basic", "")),
            default='BASIC',
            )
    initial_velocity = FloatVectorProperty(
            name="Initial Velocity",
            description="Initial velocity of the fluid",
            min=-100.0, max=100.0,
            default=(0.0, 0.0, 0.0),
            subtype='VELOCITY',
            )
    show_flows = BoolProperty(
            name="Render Fluid Objects",
            description="Keep the fluid objects visible during rendering",
            default=False,
            )
    start_baking = BoolProperty(
            name="Start Fluid Bake",
            description=("Start baking the fluid immediately "
                         "after creating the domain object"),
            default=False,
            )

    def execute(self, context):
        if not bpy.app.build_options.mod_fluid:
            self.report({'ERROR'}, "Built without Fluid modifier support")
            return {'CANCELLED'}

        fake_context = context.copy()
        mesh_objects = [obj for obj in context.selected_objects
                        if (obj.type == 'MESH' and 0.0 not in obj.dimensions)]
        min_co = Vector((100000.0, 100000.0, 100000.0))
        max_co = -min_co

        if not mesh_objects:
            self.report({'ERROR'}, "Select at least one mesh object")
            return {'CANCELLED'}

        for obj in mesh_objects:
            fake_context["object"] = obj
            # make each selected object a fluid
            bpy.ops.object.modifier_add(fake_context, type='FLUID_SIMULATION')

            # fluid has to be before constructive modifiers,
            # so it might not be the last modifier
            for mod in obj.modifiers:
                if mod.type == 'FLUID_SIMULATION':
                    break

            if self.style == 'INFLOW':
                mod.settings.type = 'INFLOW'
                mod.settings.inflow_velocity = self.initial_velocity
            else:
                mod.settings.type = 'FLUID'
                mod.settings.initial_velocity = self.initial_velocity

            obj.hide_render = not self.show_flows
            if not self.show_flows:
                obj.draw_type = 'WIRE'

            # store bounding box min/max for the domain object
            obj_bb_minmax(obj, min_co, max_co)

        # add the fluid domain object
        bpy.ops.mesh.primitive_cube_add()
        obj = context.active_object
        obj.name = "Fluid Domain"

        # give the fluid some room below the flows
        # and scale with initial velocity
        v = 0.5 * self.initial_velocity
        obj.location = 0.5 * (max_co + min_co) + Vector((0.0, 0.0, -1.0)) + v
        obj.scale = (0.5 * (max_co - min_co) +
                     Vector((1.0, 1.0, 2.0)) +
                     Vector((abs(v[0]), abs(v[1]), abs(v[2])))
                     )

        # setup smoke domain
        bpy.ops.object.modifier_add(type='FLUID_SIMULATION')
        obj.modifiers[-1].settings.type = 'DOMAIN'

        # make the domain smooth so it renders nicely
        bpy.ops.object.shade_smooth()

        # create a ray-transparent material for the domain
        bpy.ops.object.material_slot_add()

        mat = bpy.data.materials.new("Fluid Domain Material")
        obj.material_slots[0].material = mat

        mat.specular_intensity = 1
        mat.specular_hardness = 100
        mat.use_transparency = True
        mat.alpha = 0.0
        mat.transparency_method = 'RAYTRACE'
        mat.raytrace_transparency.ior = 1.33
        mat.raytrace_transparency.depth = 4

        if self.start_baking:
            bpy.ops.fluid.bake('INVOKE_DEFAULT')

        return {'FINISHED'}

class QuickLiquid(Operator):
    bl_idname = "object.quick_liquid"
    bl_label = "Quick Liquid"
    bl_options = {'REGISTER', 'UNDO'}

    show_flows = BoolProperty(
            name="Render Liquid Objects",
            description="Keep the liquid objects visible during rendering",
            default=False,
            )

    def execute(self, context):
        fake_context = context.copy()
        mesh_objects = [obj for obj in context.selected_objects
                        if obj.type == 'MESH']
        min_co = Vector((100000.0, 100000.0, 100000.0))
        max_co = -min_co

        if not mesh_objects:
            self.report({'ERROR'}, "Select at least one mesh object")
            return {'CANCELLED'}

        for obj in mesh_objects:
            fake_context["object"] = obj
            # make each selected object a liquid flow
            bpy.ops.object.modifier_add(fake_context, type='SMOKE')
            obj.modifiers[-1].smoke_type = 'FLOW'

            # set type
            obj.modifiers[-1].flow_settings.smoke_flow_type = 'LIQUID'

            # set flow behavior
            obj.modifiers[-1].flow_settings.smoke_flow_behavior = 'GEOMETRY'

            if not self.show_flows:
                obj.draw_type = 'WIRE'

            # store bounding box min/max for the domain object
            obj_bb_minmax(obj, min_co, max_co)

        # add the liquid domain object
        bpy.ops.mesh.primitive_cube_add()
        obj = context.active_object
        obj.name = "Liquid Domain"

        # give the liquid some room above the flows
        obj.location = 0.5 * (max_co + min_co) + Vector((0.0, 0.0, -1.0))
        obj.scale = 0.5 * (max_co - min_co) + Vector((1.0, 1.0, 2.0))

        # setup liquid domain
        bpy.ops.object.modifier_add(type='SMOKE')
        obj.modifiers[-1].smoke_type = 'DOMAIN'
        obj.modifiers[-1].domain_settings.smoke_domain_type = 'LIQUID'
        # set all domain borders to obstacle 
        obj.modifiers[-1].domain_settings.use_collision_border_front = True
        obj.modifiers[-1].domain_settings.use_collision_border_back = True
        obj.modifiers[-1].domain_settings.use_collision_border_right = True
        obj.modifiers[-1].domain_settings.use_collision_border_left = True
        obj.modifiers[-1].domain_settings.use_collision_border_top = True
        obj.modifiers[-1].domain_settings.use_collision_border_bottom = True

        # set correct cache file format for liquid
        obj.modifiers[-1].domain_settings.use_surface_cache = True
        obj.modifiers[-1].domain_settings.use_volume_cache = False
        obj.modifiers[-1].domain_settings.cache_surface_format = 'OBJECT'
		
        # make domain solid so that liquid becomes better visible
        obj.draw_type = 'SOLID'

        # make the domain smooth so it renders nicely
        bpy.ops.object.shade_smooth()

        # create a ray-transparent material for the domain
        bpy.ops.object.material_slot_add()

        mat = bpy.data.materials.new("Liquid Domain Material")
        obj.material_slots[0].material = mat

        mat.specular_intensity = 1
        mat.specular_hardness = 100
        mat.use_transparency = True
        mat.alpha = 0.0
        mat.transparency_method = 'RAYTRACE'
        mat.raytrace_transparency.ior = 1.33
        mat.raytrace_transparency.depth = 4

        return {'FINISHED'}


class QuickLiquidParticles(Operator):
    bl_idname = "object.quick_liquid_particles"
    bl_label = "Quick Liquid with Particles"
    bl_options = {'REGISTER', 'UNDO'}

    style = EnumProperty(
        name="Particle Style",
        items=(('SECONDARY', "Spray, Foam and Bubbles", "Enhance the liquid with particles for a secondary whitewater effect"),
               #('TRACER', "Tracer", "Use tracer particles to simulate tiny objects floating along with the liquid (e.g. dirt)"),
               ),
               default='SECONDARY',)

    liquid_display = BoolProperty(
        name="Use Liquid Display",
        description="Display simulation grids for easier parameter adjustment",
        default=True,)

    color_coding = BoolProperty(
            name="Enable Color Coding",
            description="Render some particle attribute like speed or age by the particle color",
            default=False,)

    combined_export = BoolProperty(
            name="Combined Particle Export",
            description="Several particle types end up in the same particle system for significantly faster rendering",
            default=True,)

    show_flows = BoolProperty(
            name="Render Liquid Objects",
            description="Keep the liquid objects visible during rendering",
            default=False,)

    def execute(self, context):
        fake_context = context.copy()
        mesh_objects = [obj for obj in context.selected_objects
                        if obj.type == 'MESH']
        min_co = Vector((100000.0, 100000.0, 100000.0))
        max_co = -min_co

        if not mesh_objects:
            self.report({'ERROR'}, "Select at least one mesh object")
            return {'CANCELLED'}

        for obj in mesh_objects:
            fake_context["object"] = obj
            # make each selected object a liquid flow
            bpy.ops.object.modifier_add(fake_context, type='SMOKE')
            obj.modifiers[-1].smoke_type = 'FLOW'

            # set type
            obj.modifiers[-1].flow_settings.smoke_flow_type = 'LIQUID'

            # set flow behavior
            obj.modifiers[-1].flow_settings.smoke_flow_behavior = 'GEOMETRY'

            if not self.show_flows:
                obj.hide_render = True
                obj.draw_type = 'WIRE'

            # store bounding box min/max for the domain object
            obj_bb_minmax(obj, min_co, max_co)

        # -----------------------
        # ADD LIQUID DOMAIN
        # -----------------------
        bpy.ops.mesh.primitive_cube_add()
        dom = context.active_object
        dom.name = "Liquid Domain"

        # give the liquid some room above the flows
        dom.location = 0.5 * (max_co + min_co) + Vector((0.0, 0.0, -1.0))
        dom.scale = 0.5 * (max_co - min_co) + Vector((1.0, 1.0, 2.0))
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

        # setup liquid domain
        bpy.ops.object.modifier_add(type='SMOKE')
        dom.modifiers[-1].smoke_type = 'DOMAIN'
        dom.modifiers[-1].domain_settings.smoke_domain_type = 'LIQUID'

        # set correct cache file format for liquid
        dom.modifiers[-1].domain_settings.use_surface_cache = True
        dom.modifiers[-1].domain_settings.use_volume_cache = True
        dom.modifiers[-1].domain_settings.cache_surface_format = 'OBJECT'
        dom.modifiers[-1].domain_settings.cache_volume_format = 'POINTCACHE'

        # setup liquid display
        if self.liquid_display:
            dom.modifiers[0].domain_settings.axis_slice_method = "SINGLE"
            dom.modifiers[0].domain_settings.slice_axis = "X"
            dom.modifiers[0].domain_settings.coba_field_liquid = "KINETIC_ENERGY"
            dom.modifiers[0].domain_settings.use_color_ramp = True
            dom.modifiers[0].domain_settings.color_ramp.color_mode = "HSV"
            dom.modifiers[0].domain_settings.color_ramp.hue_interpolation = "CCW"

            alpha = 0.7
            el = dom.modifiers[-1].domain_settings.color_ramp.elements
            el[0].position = 0.01
            el[0].color = (0, 0, 0, 0) #first point transparent
            el[1].position = 0.011
            el[1].color = (0.05, 0.05, 1.0, 0.7) # desaturated blue
            el.new(1.0)
            el[2].color = (1.0, 0.05, 0.15, 0.7) # desaturated red

        # make domain solid so that liquid becomes better visible
        dom.draw_type = 'SOLID'

        # make the domain smooth so it renders nicely
        bpy.ops.object.shade_smooth()

        # setup options according to style
        if self.style == 'SECONDARY':
            dom.modifiers[0].domain_settings.use_spray_particles = True
            dom.modifiers[0].domain_settings.use_foam_particles = True
            dom.modifiers[0].domain_settings.use_bubble_particles = True
            # Cycles: Material setup
            if context.scene.render.use_shading_nodes:
                bpy.ops.object.material_slot_add()

                mat = bpy.data.materials.new("Liquid Material")
                dom.material_slots[0].material = mat

                # Make sure we use nodes
                mat.use_nodes = True

                # Set node variables and clear the default nodes
                nodes = mat.node_tree.nodes
                links = mat.node_tree.links

                nodes.clear()

                # Material output
                node_out = nodes.new(type='ShaderNodeOutputMaterial')
                node_out.location = grid_location(6, 1)

                # Add Glass Shader
                node_glass = nodes.new(type='ShaderNodeBsdfGlass')
                node_glass.location = grid_location(4, 2)
                node_glass.inputs["IOR"].default_value = 1.33
                node_glass.inputs["Color"].default_value = (0.48, 0.64, 0.79, 1)
                links.new(node_glass.outputs[0], node_out.inputs[0])

                # Add Volume Absorption Shader
                node_abs = nodes.new(type='ShaderNodeVolumeAbsorption')
                node_abs.location = grid_location(4, 0)
                node_abs.inputs["Color"].default_value = (0.86, 0.93, 0.96, 1)
                links.new(node_abs.outputs[0], node_out.inputs[1])
                
            if self.combined_export:
                # -----------------------
                # ADD SPRAY+FOAM+BUBBLE VOLUME
                # -----------------------
                dom.modifiers[0].domain_settings.sndparticle_combined_export = 'SPRAY + FOAM + BUBBLES'

                bpy.ops.mesh.primitive_cube_add()
                sfb = context.active_object
                sfb.name = "Volume Spray+Foam+Bubbles"

                # Set volume as child of liquid domain
                sfb.location = (0,0,0)
                sfb.parent = dom
                sfb.dimensions = dom.dimensions
                bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

                # only draw volume edges
                sfb.draw_type = 'WIRE'

                # Cycles: Material setup
                if context.scene.render.use_shading_nodes:
                    bpy.ops.object.material_slot_add()

                    mat = bpy.data.materials.new("Spray+Foam+Bubbles Material")
                    sfb.material_slots[0].material = mat

                    # Make sure we use nodes
                    mat.use_nodes = True

                    # Set node variables and clear the default nodes
                    tree = mat.node_tree
                    nodes = tree.nodes
                    links = tree.links

                    nodes.clear()

                    # Material output
                    node_out = nodes.new(type='ShaderNodeOutputMaterial')
                    node_out.location = grid_location(6, 1)

                    # Include Add Shader
                    node_add = nodes.new(type='ShaderNodeAddShader')
                    node_add.location = grid_location(5, 1)
                    links.new(node_add.outputs[0], node_out.inputs[1])

                    # Add Volume Absorption Shader
                    node_abs = nodes.new(type='ShaderNodeVolumeAbsorption')
                    node_abs.location = grid_location(4, 2)
                    links.new(node_abs.outputs[0], node_add.inputs[0])

                    # Add Volume Scatter Shader
                    node_scatter = nodes.new(type='ShaderNodeVolumeScatter')
                    node_scatter.location = grid_location(4, 0)
                    links.new(node_scatter.outputs[0], node_add.inputs[1])

                    # Add Math Node to control density
                    node_math = nodes.new(type='ShaderNodeMath')
                    node_math.location = grid_location(3, 0)
                    node_math.label = 'Density Control'
                    node_math.operation = 'MULTIPLY'
                    node_math.inputs[1].default_value = 10
                    links.new(node_math.outputs[0], node_abs.inputs[1])
                    links.new(node_math.outputs[0], node_scatter.inputs[1])

                    # Add Point Density Texture Node to create renderable representation from particles
                    node_dens = nodes.new(type='ShaderNodeTexPointDensity')
                    node_dens.location = grid_location(1, 1)
                    node_dens.width = 250
                    node_dens.point_source = 'PARTICLE_SYSTEM'
                    node_dens.object = dom
                    node_dens.particle_system = dom.particle_systems['Spray + Foam + Bubble Particles']
                    node_dens.space = 'WORLD'
                    node_dens.radius = 0.05
                    node_dens.interpolation = 'Linear'
                    node_dens.resolution = 256
                    node_dens.particle_color_source = 'PARTICLE_VELOCITY'
                    links.new(node_dens.outputs[1], node_math.inputs[0])

                    if self.color_coding:
                        links.new(node_dens.outputs[0], node_abs.inputs[0])
                        links.new(node_dens.outputs[0], node_scatter.inputs[0])
                    else:
                        # Add RGB node to control spray color
                        node_rgb = nodes.new(type='ShaderNodeRGB')
                        node_rgb.location = grid_location(3, 2)
                        node_rgb.label = 'Particle Color'
                        node_rgb.outputs[0].default_value = (0.9, 0.9, 0.9, 1)
                        links.new(node_rgb.outputs[0], node_abs.inputs[0])
                        links.new(node_rgb.outputs[0], node_scatter.inputs[0])
            else:
                # -----------------------
                # ADD SPRAY VOLUME
                # -----------------------
                bpy.ops.mesh.primitive_cube_add()
                spray = context.active_object
                spray.name = "Volume Spray"

                # Set volume as child of liquid domain
                spray.location = (0,0,0)
                spray.parent = dom
                spray.dimensions = dom.dimensions
                bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

                # only draw volume edges
                spray.draw_type = 'WIRE'

                # Cycles: Material setup
                if context.scene.render.use_shading_nodes:
                    bpy.ops.object.material_slot_add()

                    mat = bpy.data.materials.new("Particle Spray Material")
                    spray.material_slots[0].material = mat

                    # Make sure we use nodes
                    mat.use_nodes = True

                    # Set node variables and clear the default nodes
                    tree = mat.node_tree
                    nodes = tree.nodes
                    links = tree.links

                    nodes.clear()

                    # Material output
                    node_out = nodes.new(type='ShaderNodeOutputMaterial')
                    node_out.location = grid_location(6, 1)

                    # Include Add Shader
                    node_add = nodes.new(type='ShaderNodeAddShader')
                    node_add.location = grid_location(5, 1)
                    links.new(node_add.outputs[0], node_out.inputs[1])

                    # Add Volume Absorption Shader
                    node_abs = nodes.new(type='ShaderNodeVolumeAbsorption')
                    node_abs.location = grid_location(4, 2)
                    links.new(node_abs.outputs[0], node_add.inputs[0])

                    # Add Volume Scatter Shader
                    node_scatter = nodes.new(type='ShaderNodeVolumeScatter')
                    node_scatter.location = grid_location(4, 0)
                    links.new(node_scatter.outputs[0], node_add.inputs[1])

                    # Add Math Node to control density
                    node_math = nodes.new(type='ShaderNodeMath')
                    node_math.location = grid_location(3, 0)
                    node_math.label = 'Density Control'
                    node_math.operation = 'MULTIPLY'
                    node_math.inputs[1].default_value = 10
                    links.new(node_math.outputs[0], node_abs.inputs[1])
                    links.new(node_math.outputs[0], node_scatter.inputs[1])

                    # Add Point Density Texture Node to create renderable representation from particles
                    node_dens = nodes.new(type='ShaderNodeTexPointDensity')
                    node_dens.location = grid_location(1, 1)
                    node_dens.width = 250
                    node_dens.point_source = 'PARTICLE_SYSTEM'
                    node_dens.object = dom
                    node_dens.particle_system = dom.particle_systems['Spray Particles']
                    node_dens.space = 'WORLD'
                    node_dens.radius = 0.05
                    node_dens.interpolation = 'Linear'
                    node_dens.resolution = 128
                    node_dens.particle_color_source = 'PARTICLE_VELOCITY'
                    links.new(node_dens.outputs[1], node_math.inputs[0])

                    if self.color_coding:
                        links.new(node_dens.outputs[0], node_abs.inputs[0])
                        links.new(node_dens.outputs[0], node_scatter.inputs[0])
                    else:
                        # Add RGB node to control spray color
                        node_rgb = nodes.new(type='ShaderNodeRGB')
                        node_rgb.location = grid_location(3, 2)
                        node_rgb.label = 'Spray Color'
                        node_rgb.outputs[0].default_value = (0.9, 0.9, 0.9, 1)
                        links.new(node_rgb.outputs[0], node_abs.inputs[0])
                        links.new(node_rgb.outputs[0], node_scatter.inputs[0])

                # -----------------------
                # ADD FOAM VOLUME
                # -----------------------
                bpy.ops.mesh.primitive_cube_add()
                foam = context.active_object
                foam.name = "Volume Foam"

                # Set volume as child of liquid domain
                foam.location = (0,0,0)
                foam.parent = dom
                foam.dimensions = dom.dimensions
                bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

                # only draw volume edges
                foam.draw_type = 'WIRE'

                # Cycles: Material setup
                if context.scene.render.use_shading_nodes:
                    bpy.ops.object.material_slot_add()

                    mat = bpy.data.materials.new("Particle Foam Material")
                    foam.material_slots[0].material = mat

                    # Make sure we use nodes
                    mat.use_nodes = True

                    # Set node variables and clear the default nodes
                    tree = mat.node_tree
                    nodes = tree.nodes
                    links = tree.links

                    nodes.clear()

                    # Material output
                    node_out = nodes.new(type='ShaderNodeOutputMaterial')
                    node_out.location = grid_location(6, 1)

                    # Include Add Shader
                    node_add = nodes.new(type='ShaderNodeAddShader')
                    node_add.location = grid_location(5, 1)
                    links.new(node_add.outputs[0], node_out.inputs[1])

                    # Add Volume Absorption Shader
                    node_abs = nodes.new(type='ShaderNodeVolumeAbsorption')
                    node_abs.location = grid_location(4, 2)
                    links.new(node_abs.outputs[0], node_add.inputs[0])

                    # Add Volume Scatter Shader
                    node_scatter = nodes.new(type='ShaderNodeVolumeScatter')
                    node_scatter.location = grid_location(4, 0)
                    links.new(node_scatter.outputs[0], node_add.inputs[1])

                    # Add Math Node to control density
                    node_math = nodes.new(type='ShaderNodeMath')
                    node_math.location = grid_location(3, 0)
                    node_math.label = 'Density Control'
                    node_math.operation = 'MULTIPLY'
                    node_math.inputs[1].default_value = 10
                    links.new(node_math.outputs[0], node_abs.inputs[1])
                    links.new(node_math.outputs[0], node_scatter.inputs[1])

                    # Add Point Density Texture Node to create renderable representation from particles
                    node_dens = nodes.new(type='ShaderNodeTexPointDensity')
                    node_dens.location = grid_location(1, 1)
                    node_dens.width = 250
                    node_dens.point_source = 'PARTICLE_SYSTEM'
                    node_dens.object = dom
                    node_dens.particle_system = dom.particle_systems['Foam Particles']
                    node_dens.space = 'WORLD'
                    node_dens.radius = 0.05
                    node_dens.interpolation = 'Linear'
                    node_dens.resolution = 128
                    node_dens.particle_color_source = 'PARTICLE_VELOCITY'
                    links.new(node_dens.outputs[1], node_math.inputs[0])

                    if self.color_coding:
                        links.new(node_dens.outputs[0], node_abs.inputs[0])
                        links.new(node_dens.outputs[0], node_scatter.inputs[0])
                    else:
                        # Add RGB node to control foam color
                        node_rgb = nodes.new(type='ShaderNodeRGB')
                        node_rgb.location = grid_location(3, 2)
                        node_rgb.label = 'Foam Color'
                        node_rgb.outputs[0].default_value = (0.9, 0.9, 0.9, 1)
                        links.new(node_rgb.outputs[0], node_abs.inputs[0])
                        links.new(node_rgb.outputs[0], node_scatter.inputs[0])

                # -----------------------
                # ADD BUBBLE VOLUME
                # -----------------------
                bpy.ops.mesh.primitive_cube_add()
                bubbles = context.active_object
                bubbles.name = "Volume Bubbles"

                # Set volume as child of liquid domain
                bubbles.location = (0,0,0)
                bubbles.parent = dom
                bubbles.dimensions = dom.dimensions
                bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

                # only draw volume edges
                bubbles.draw_type = 'WIRE'

                # Cycles: Material setup
                if context.scene.render.use_shading_nodes:
                    bpy.ops.object.material_slot_add()

                    mat = bpy.data.materials.new("Particle Bubble Material")
                    bubbles.material_slots[0].material = mat

                    # Make sure we use nodes
                    mat.use_nodes = True

                    # Set node variables and clear the default nodes
                    tree = mat.node_tree
                    nodes = tree.nodes
                    links = tree.links

                    nodes.clear()

                    # Material output
                    node_out = nodes.new(type='ShaderNodeOutputMaterial')
                    node_out.location = grid_location(6, 1)

                    # Include Add Shader
                    node_add = nodes.new(type='ShaderNodeAddShader')
                    node_add.location = grid_location(5, 1)
                    links.new(node_add.outputs[0], node_out.inputs[1])

                    # Add Volume Absorption Shader
                    node_abs = nodes.new(type='ShaderNodeVolumeAbsorption')
                    node_abs.location = grid_location(4, 2)
                    links.new(node_abs.outputs[0], node_add.inputs[0])

                    # Add Volume Scatter Shader
                    node_scatter = nodes.new(type='ShaderNodeVolumeScatter')
                    node_scatter.location = grid_location(4, 0)
                    links.new(node_scatter.outputs[0], node_add.inputs[1])

                    # Add Math Node to control density
                    node_math = nodes.new(type='ShaderNodeMath')
                    node_math.location = grid_location(3, 0)
                    node_math.label = 'Density Control'
                    node_math.operation = 'MULTIPLY'
                    node_math.inputs[1].default_value = 10
                    links.new(node_math.outputs[0], node_abs.inputs[1])
                    links.new(node_math.outputs[0], node_scatter.inputs[1])

                    # Add Point Density Texture Node to create renderable representation from particles
                    node_dens = nodes.new(type='ShaderNodeTexPointDensity')
                    node_dens.location = grid_location(1, 1)
                    node_dens.width = 250
                    node_dens.point_source = 'PARTICLE_SYSTEM'
                    node_dens.object = dom
                    node_dens.particle_system = dom.particle_systems['Bubble Particles']
                    node_dens.space = 'WORLD'
                    node_dens.radius = 0.05
                    node_dens.interpolation = 'Linear'
                    node_dens.resolution = 128
                    node_dens.particle_color_source = 'PARTICLE_VELOCITY'
                    links.new(node_dens.outputs[1], node_math.inputs[0])

                    if self.color_coding:
                        links.new(node_dens.outputs[0], node_abs.inputs[0])
                        links.new(node_dens.outputs[0], node_scatter.inputs[0])
                    else:
                        # Add RGB node to control bubble color
                        node_rgb = nodes.new(type='ShaderNodeRGB')
                        node_rgb.location = grid_location(3, 2)
                        node_rgb.label = 'Bubble Color'
                        node_rgb.outputs[0].default_value = (0.9, 0.9, 0.9, 1)
                        links.new(node_rgb.outputs[0], node_abs.inputs[0])
                        links.new(node_rgb.outputs[0], node_scatter.inputs[0])

        # TODO after tracers are added
        #if self.style == 'TRACER':

        return {'FINISHED'}
classes = (
    QuickExplode,
    QuickFluid,
    QuickFur,
    QuickSmoke,
    QuickLiquid,
    QuickLiquidParticles,
)
