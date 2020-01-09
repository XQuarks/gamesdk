// This file is the "source of truth" for sets and bindings indices
// and it dictates the descriptors layout for meshes
//
// Shaders and host code use this defines

//////// MATERIAL BINDINGS

#define BINDING_SET_MATERIAL 0
#define FRAGMENT_BINDING_MATERIAL_ATTRIBUTES 0
#define FRAGMENT_BINDING_MATERIAL_COUNT 1
//// Next n bindings for texture

//////// LIGHTS BINDINGS

#define BINDING_SET_LIGHTS 1
#define FRAGMENT_BINDING_LIGHTS 0

//////// MESH BINDINGS

#define BINDING_SET_MESH  2
#define VERTEX_BINDING_MODEL_VIEW_PROJECTION 0

//////// FONT BINDINGS

#define FONT_BINDING_SET 0
#define FONT_VERTEX_UBO_BINDING 0
#define FONT_FRAG_SAMPLER_BINDING 1
