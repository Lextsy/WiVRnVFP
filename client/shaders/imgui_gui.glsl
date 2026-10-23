/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Textured quad used to composite the imgui canvas into the projection
// image when the runtime has no quad layer support (Wave shim). The UV
// rectangle of each window is baked into the vertex data on the CPU.

#version 450

#ifdef WIVRN_MULTIVIEW
#extension GL_EXT_multiview : enable
#define wivrn_view gl_ViewIndex
#else
#define wivrn_view 0
#endif

layout (constant_id = 0) const int nb_texcoords = 2;
layout (constant_id = 1) const bool dithering = true;
layout (constant_id = 2) const bool alpha_cutout = false;
layout (constant_id = 3) const bool skinning = false;

const int nb_views = 2;

layout(set = 0, binding = 0) uniform scene_ubo
{
        mat4 view[nb_views];
        vec4 light_position;
        vec4 ambient_color;
        vec4 light_color;
} scene;

layout(set = 0, binding = 1) uniform mesh_ubo
{
        mat4 model;
        mat4 modelview[nb_views];
        mat4 modelviewproj[nb_views];
} mesh;

layout(set = 0, binding = 2) uniform joints_ubo
{
        mat4 joint_matrices[32];
} joints;

#ifdef FRAG_SHADER
layout(set = 0, binding = 3) uniform sampler2D base_color;
layout(set = 0, binding = 4) uniform sampler2D metallic_roughness;
layout(set = 0, binding = 5) uniform sampler2D occlusion;
layout(set = 0, binding = 6) uniform sampler2D emissive;
layout(set = 0, binding = 7) uniform sampler2D normal_map;
layout(set = 0, binding = 8) uniform material_ubo
{
        vec4 base_color_factor;
        vec4 base_emissive_factor;
        float metallic_factor;
        float roughness_factor;
        float occlusion_strength;
        float normal_scale;
        float alpha_cutoff;

        int base_color_texcoord;
        int metallic_roughness_texcoord;
        int occlusion_texcoord;
        int emissive_texcoord;
        int normal_texcoord;
} material;
#endif


// Adreno 540 (Vulkan 1.0) null-derefs inside vkUpdateDescriptorSets when a
// descriptor set layout binding is absent from the bound shader stage's SPIR-V
// interface. Touch every layout binding in every stage so the interface matches
// layout_bindings_0; the body is empty, so rendering is unchanged.
layout(constant_id = 4) const bool force_bindings = true;
void wivrn_force_bindings()
{
    if (force_bindings)
    {
        float x = scene.view[0][0][0] + mesh.model[0][0] + joints.joint_matrices[0][0][0];
#ifdef FRAG_SHADER
        x += texture(base_color, vec2(0.0)).x + texture(metallic_roughness, vec2(0.0)).x + texture(occlusion, vec2(0.0)).x + texture(emissive, vec2(0.0)).x + texture(normal_map, vec2(0.0)).x + material.base_color_factor.x;
#endif
        if (x == 1.234567812345678)
        {
            // unreachable: keeps the reads (and thus the bindings) in the interface
        }
    }
}

// Vertex input
#ifdef VERT_SHADER
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_texcoord;
#endif

// Vertex-to-fragment
#ifdef VERT_SHADER
#define VERT_TO_FRAG out
#else
#define VERT_TO_FRAG in
#endif

layout(location = 0) VERT_TO_FRAG vec2 gui_texcoord;

// Shader code
#ifdef VERT_SHADER
out gl_PerVertex
{
        vec4 gl_Position;
};

void main()
{
        wivrn_force_bindings();
        gl_Position = mesh.modelviewproj[wivrn_view] * vec4(in_position, 1.0);
        gui_texcoord = in_texcoord;
}
#endif

#ifdef FRAG_SHADER
layout(location = 0) out vec4 out_color;
void main()
{
        wivrn_force_bindings();
        // The imgui canvas stores straight alpha (its backend blends with
        // SRC_ALPHA), but this pipeline blends with premultiplied factors
        // (ONE, ONE_MINUS_SRC_ALPHA), so premultiply here.
        vec4 c = texture(base_color, gui_texcoord);
        out_color = vec4(c.rgb * c.a, c.a);
}
#endif
