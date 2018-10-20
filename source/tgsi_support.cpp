#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "pipe/p_shader_tokens.h"

extern "C"
{
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_emulate.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_ureg.h"
#include "tgsi/tgsi_from_mesa.h"
//#include "tgsi/tgsi_dump.h"
}

#include "state_tracker/st_glsl_to_tgsi.h"

#include "glsl_frontend.h"

#define ST_DOUBLE_ATTRIB_PLACEHOLDER 0xff

// Defined in glsl_frontend.cpp
struct glsl_to_tgsi_visitor*
_glsl_program_get_tgsi_visitor(struct gl_program *prog);

// Defined in glsl_frontend.cpp
void
_glsl_program_attach_tgsi_tokens(struct gl_program *prog, const tgsi_token *tokens, unsigned int num);

// Based off st_translate_vertex_program
bool tgsi_translate_vertex(struct gl_context *ctx, struct gl_program *prog)
{
	unsigned num_outputs = 0;
	unsigned attr;
	ubyte output_semantic_name[VARYING_SLOT_MAX] = {0};
	ubyte output_semantic_index[VARYING_SLOT_MAX] = {0};

	// maps a TGSI input index back to a Mesa VERT_ATTRIB_x
	ubyte index_to_input[PIPE_MAX_ATTRIBS];
	ubyte num_inputs = 0;
	// Reverse mapping of the above
	ubyte input_to_index[VERT_ATTRIB_MAX];

	// Maps VARYING_SLOT_x to slot
	ubyte result_to_output[VARYING_SLOT_MAX];

	memset(input_to_index, ~0, sizeof(input_to_index));

	// Determine number of inputs, the mappings between VERT_ATTRIB_x
	// and TGSI generic input indexes, plus input attrib semantic info.
	for (attr = 0; attr < VERT_ATTRIB_MAX; attr++) {
		if ((prog->info.inputs_read & BITFIELD64_BIT(attr)) != 0) {
			input_to_index[attr] = num_inputs;
			index_to_input[num_inputs] = attr;
			num_inputs++;
			if ((prog->DualSlotInputs & BITFIELD64_BIT(attr)) != 0) {
				// add placeholder for second part of a double attribute
				index_to_input[num_inputs] = ST_DOUBLE_ATTRIB_PLACEHOLDER;
				num_inputs++;
			}
		}
	}
	// bit of a hack, presetup potentially unused edgeflag input
	input_to_index[VERT_ATTRIB_EDGEFLAG] = num_inputs;
	index_to_input[num_inputs] = VERT_ATTRIB_EDGEFLAG;

	// Compute mapping of vertex program outputs to slots.
	for (attr = 0; attr < VARYING_SLOT_MAX; attr++) {
		if ((prog->info.outputs_written & BITFIELD64_BIT(attr)) == 0) {
			result_to_output[attr] = ~0;
		}
		else {
			unsigned slot = num_outputs++;

			result_to_output[attr] = slot;

			unsigned semantic_name, semantic_index;
			tgsi_get_gl_varying_semantic(gl_varying_slot(attr), true, &semantic_name, &semantic_index);
			output_semantic_name[slot] = semantic_name;
			output_semantic_index[slot] = semantic_index;
		}
	}
	// similar hack to above, presetup potentially unused edgeflag output
	result_to_output[VARYING_SLOT_EDGE] = num_outputs;
	output_semantic_name[num_outputs] = TGSI_SEMANTIC_EDGEFLAG;
	output_semantic_index[num_outputs] = 0;

	struct ureg_program *ureg = ureg_create(PIPE_SHADER_VERTEX);
	if (!ureg)
		return false;

	if (prog->info.clip_distance_array_size)
		ureg_property(ureg, TGSI_PROPERTY_NUM_CLIPDIST_ENABLED, prog->info.clip_distance_array_size);
	if (prog->info.cull_distance_array_size)
		ureg_property(ureg, TGSI_PROPERTY_NUM_CULLDIST_ENABLED, prog->info.cull_distance_array_size);

	enum pipe_error error = st_translate_program(ctx,
		PIPE_SHADER_VERTEX,
		ureg,
		_glsl_program_get_tgsi_visitor(prog),
		prog,
		num_inputs, input_to_index, NULL, NULL, NULL, NULL,
		num_outputs, result_to_output, output_semantic_name, output_semantic_index);

	if (error != PIPE_OK)
	{
		ureg_destroy(ureg);
		return false;
	}

	// We get back the tgsi!!
	unsigned int num_tokens = 0;
	const struct tgsi_token *tokens = ureg_get_tokens(ureg, &num_tokens);
	ureg_destroy(ureg);

	//tgsi_dump_to_file(tokens, TGSI_DUMP_FLOAT_AS_HEX, stdout);
	_glsl_program_attach_tgsi_tokens(prog, tokens, num_tokens);

	return true;
}

// Based off st_translate_vertex_program
bool tgsi_translate_fragment(struct gl_context *ctx, struct gl_program *prog)
{
	ubyte outputMapping[2 * FRAG_RESULT_MAX];
	ubyte inputMapping[VARYING_SLOT_MAX];
	ubyte inputSlotToAttr[VARYING_SLOT_MAX];
	ubyte interpMode[PIPE_MAX_SHADER_INPUTS];  /* XXX size? */
	GLuint attr;
	GLbitfield64 inputsRead;

	GLboolean write_all = GL_FALSE;

	ubyte input_semantic_name[PIPE_MAX_SHADER_INPUTS];
	ubyte input_semantic_index[PIPE_MAX_SHADER_INPUTS];
	uint fs_num_inputs = 0;

	ubyte fs_output_semantic_name[PIPE_MAX_SHADER_OUTPUTS];
	ubyte fs_output_semantic_index[PIPE_MAX_SHADER_OUTPUTS];
	uint fs_num_outputs = 0;

	memset(inputSlotToAttr, ~0, sizeof(inputSlotToAttr));

	// Convert Mesa program inputs to TGSI input register semantics.
	inputsRead = prog->info.inputs_read;
	for (attr = 0; attr < VARYING_SLOT_MAX; attr++) {
		if ((inputsRead & BITFIELD64_BIT(attr)) != 0) {
			const GLuint slot = fs_num_inputs++;

			inputMapping[attr] = slot;
			inputSlotToAttr[slot] = attr;

			switch (attr) {
				case VARYING_SLOT_POS:
					input_semantic_name[slot] = TGSI_SEMANTIC_POSITION;
					input_semantic_index[slot] = 0;
					interpMode[slot] = TGSI_INTERPOLATE_LINEAR;
					break;
				case VARYING_SLOT_COL0:
					input_semantic_name[slot] = TGSI_SEMANTIC_COLOR;
					input_semantic_index[slot] = 0;
					interpMode[slot] = TGSI_INTERPOLATE_COUNT;
					break;
				case VARYING_SLOT_COL1:
					input_semantic_name[slot] = TGSI_SEMANTIC_COLOR;
					input_semantic_index[slot] = 1;
					interpMode[slot] = TGSI_INTERPOLATE_COUNT;
					break;
				case VARYING_SLOT_FOGC:
					input_semantic_name[slot] = TGSI_SEMANTIC_FOG;
					input_semantic_index[slot] = 0;
					interpMode[slot] = TGSI_INTERPOLATE_PERSPECTIVE;
					break;
				case VARYING_SLOT_FACE:
					input_semantic_name[slot] = TGSI_SEMANTIC_FACE;
					input_semantic_index[slot] = 0;
					interpMode[slot] = TGSI_INTERPOLATE_CONSTANT;
					break;
				case VARYING_SLOT_PRIMITIVE_ID:
					input_semantic_name[slot] = TGSI_SEMANTIC_PRIMID;
					input_semantic_index[slot] = 0;
					interpMode[slot] = TGSI_INTERPOLATE_CONSTANT;
					break;
				case VARYING_SLOT_LAYER:
					input_semantic_name[slot] = TGSI_SEMANTIC_LAYER;
					input_semantic_index[slot] = 0;
					interpMode[slot] = TGSI_INTERPOLATE_CONSTANT;
					break;
				case VARYING_SLOT_VIEWPORT:
					input_semantic_name[slot] = TGSI_SEMANTIC_VIEWPORT_INDEX;
					input_semantic_index[slot] = 0;
					interpMode[slot] = TGSI_INTERPOLATE_CONSTANT;
					break;
				case VARYING_SLOT_CLIP_DIST0:
					input_semantic_name[slot] = TGSI_SEMANTIC_CLIPDIST;
					input_semantic_index[slot] = 0;
					interpMode[slot] = TGSI_INTERPOLATE_PERSPECTIVE;
					break;
				case VARYING_SLOT_CLIP_DIST1:
					input_semantic_name[slot] = TGSI_SEMANTIC_CLIPDIST;
					input_semantic_index[slot] = 1;
					interpMode[slot] = TGSI_INTERPOLATE_PERSPECTIVE;
					break;
				case VARYING_SLOT_CULL_DIST0:
				case VARYING_SLOT_CULL_DIST1:
					/* these should have been lowered by GLSL */
					assert(0);
					break;
					/* In most cases, there is nothing special about these
					* inputs, so adopt a convention to use the generic
					* semantic name and the mesa VARYING_SLOT_ number as the
					* index.
					*
					* All that is required is that the vertex shader labels
					* its own outputs similarly, and that the vertex shader
					* generates at least every output required by the
					* fragment shader plus fixed-function hardware (such as
					* BFC).
					*
					* However, some drivers may need us to identify the PNTC and TEXi
					* varyings if, for example, their capability to replace them with
					* sprite coordinates is limited.
					*/
				case VARYING_SLOT_PNTC:
					input_semantic_name[slot] = TGSI_SEMANTIC_PCOORD;
					input_semantic_index[slot] = 0;
					interpMode[slot] = TGSI_INTERPOLATE_LINEAR;
					break;
				case VARYING_SLOT_TEX0:
				case VARYING_SLOT_TEX1:
				case VARYING_SLOT_TEX2:
				case VARYING_SLOT_TEX3:
				case VARYING_SLOT_TEX4:
				case VARYING_SLOT_TEX5:
				case VARYING_SLOT_TEX6:
				case VARYING_SLOT_TEX7:
					input_semantic_name[slot] = TGSI_SEMANTIC_TEXCOORD;
					input_semantic_index[slot] = attr - VARYING_SLOT_TEX0;
					interpMode[slot] = TGSI_INTERPOLATE_COUNT;
					break;
				case VARYING_SLOT_VAR0:
				default:
					/* Semantic indices should be zero-based because drivers may choose
					* to assign a fixed slot determined by that index.
					* This is useful because ARB_separate_shader_objects uses location
					* qualifiers for linkage, and if the semantic index corresponds to
					* these locations, linkage passes in the driver become unecessary.
					*
					* If needs_texcoord_semantic is true, no semantic indices will be
					* consumed for the TEXi varyings, and we can base the locations of
					* the user varyings on VAR0.  Otherwise, we use TEX0 as base index.
					*/
					assert(attr >= VARYING_SLOT_VAR0 || attr == VARYING_SLOT_PNTC ||
						(attr >= VARYING_SLOT_TEX0 && attr <= VARYING_SLOT_TEX7));
					input_semantic_name[slot] = TGSI_SEMANTIC_GENERIC;
					input_semantic_index[slot] = tgsi_get_generic_gl_varying_index(gl_varying_slot(attr), true);
					if (attr == VARYING_SLOT_PNTC)
						interpMode[slot] = TGSI_INTERPOLATE_LINEAR;
					else
						interpMode[slot] = TGSI_INTERPOLATE_COUNT;
					break;
			}
		}
		else {
			inputMapping[attr] = -1;
		}
	}

	// Semantics and mapping for outputs
	GLbitfield64 outputsWritten = prog->info.outputs_written;

	/* if z is written, emit that first */
	if (outputsWritten & BITFIELD64_BIT(FRAG_RESULT_DEPTH)) {
		fs_output_semantic_name[fs_num_outputs] = TGSI_SEMANTIC_POSITION;
		fs_output_semantic_index[fs_num_outputs] = 0;
		outputMapping[FRAG_RESULT_DEPTH] = fs_num_outputs;
		fs_num_outputs++;
		outputsWritten &= ~(1 << FRAG_RESULT_DEPTH);
	}

	if (outputsWritten & BITFIELD64_BIT(FRAG_RESULT_STENCIL)) {
		fs_output_semantic_name[fs_num_outputs] = TGSI_SEMANTIC_STENCIL;
		fs_output_semantic_index[fs_num_outputs] = 0;
		outputMapping[FRAG_RESULT_STENCIL] = fs_num_outputs;
		fs_num_outputs++;
		outputsWritten &= ~(1 << FRAG_RESULT_STENCIL);
	}

	if (outputsWritten & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK)) {
		fs_output_semantic_name[fs_num_outputs] = TGSI_SEMANTIC_SAMPLEMASK;
		fs_output_semantic_index[fs_num_outputs] = 0;
		outputMapping[FRAG_RESULT_SAMPLE_MASK] = fs_num_outputs;
		fs_num_outputs++;
		outputsWritten &= ~(1 << FRAG_RESULT_SAMPLE_MASK);
	}

	/* handle remaining outputs (color) */
	for (attr = 0; attr < ARRAY_SIZE(outputMapping); attr++) {
		const GLbitfield64 written = attr < FRAG_RESULT_MAX ? outputsWritten :
			prog->SecondaryOutputsWritten;
		const unsigned loc = attr % FRAG_RESULT_MAX;

		if (written & BITFIELD64_BIT(loc)) {
			switch (loc) {
				case FRAG_RESULT_DEPTH:
				case FRAG_RESULT_STENCIL:
				case FRAG_RESULT_SAMPLE_MASK:
					/* handled above */
					assert(0);
					break;
				case FRAG_RESULT_COLOR:
					write_all = GL_TRUE; /* fallthrough */
				default:
				{
					int index;
					assert(loc == FRAG_RESULT_COLOR ||
						(FRAG_RESULT_DATA0 <= loc && loc < FRAG_RESULT_MAX));

					index = (loc == FRAG_RESULT_COLOR) ? 0 : (loc - FRAG_RESULT_DATA0);

					if (attr >= FRAG_RESULT_MAX) {
						/* Secondary color for dual source blending. */
						assert(index == 0);
						index++;
					}

					fs_output_semantic_name[fs_num_outputs] = TGSI_SEMANTIC_COLOR;
					fs_output_semantic_index[fs_num_outputs] = index;
					outputMapping[attr] = fs_num_outputs;
					break;
				}
			}

			fs_num_outputs++;
		}
	}

	struct ureg_program *ureg = ureg_create(PIPE_SHADER_FRAGMENT);
	if (!ureg)
		return false;

	if (write_all == GL_TRUE)
		ureg_property(ureg, TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS, 1);

	if (prog->info.fs.depth_layout != FRAG_DEPTH_LAYOUT_NONE) {
		switch (prog->info.fs.depth_layout) {
			case FRAG_DEPTH_LAYOUT_ANY:
				ureg_property(ureg, TGSI_PROPERTY_FS_DEPTH_LAYOUT, TGSI_FS_DEPTH_LAYOUT_ANY);
				break;
			case FRAG_DEPTH_LAYOUT_GREATER:
				ureg_property(ureg, TGSI_PROPERTY_FS_DEPTH_LAYOUT, TGSI_FS_DEPTH_LAYOUT_GREATER);
				break;
			case FRAG_DEPTH_LAYOUT_LESS:
				ureg_property(ureg, TGSI_PROPERTY_FS_DEPTH_LAYOUT, TGSI_FS_DEPTH_LAYOUT_LESS);
				break;
			case FRAG_DEPTH_LAYOUT_UNCHANGED:
				ureg_property(ureg, TGSI_PROPERTY_FS_DEPTH_LAYOUT, TGSI_FS_DEPTH_LAYOUT_UNCHANGED);
				break;
			default:
				assert(0);
		}
	}

	enum pipe_error error = st_translate_program(ctx,
		PIPE_SHADER_FRAGMENT,
		ureg,
		_glsl_program_get_tgsi_visitor(prog),
		prog,
		fs_num_inputs, inputMapping, inputSlotToAttr, input_semantic_name, input_semantic_index, interpMode,
		fs_num_outputs, outputMapping, fs_output_semantic_name, fs_output_semantic_index);

	if (error != PIPE_OK)
	{
		ureg_destroy(ureg);
		return false;
	}

	// We get back the tgsi!!
	unsigned int num_tokens = 0;
	const struct tgsi_token *tokens = ureg_get_tokens(ureg, &num_tokens);
	ureg_destroy(ureg);

	_glsl_program_attach_tgsi_tokens(prog, tokens, num_tokens);

	return true;
}
