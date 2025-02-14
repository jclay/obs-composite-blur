#include "gaussian.h"

void set_gaussian_blur_types(obs_properties_t *props)
{
	obs_log(LOG_INFO, "set gaussian blur types...");
	obs_property_t *p = obs_properties_get(props, "blur_type");
	obs_property_list_clear(p);
	obs_property_list_add_int(p, obs_module_text(TYPE_AREA_LABEL),
				  TYPE_AREA);
	obs_property_list_add_int(p, obs_module_text(TYPE_DIRECTIONAL_LABEL),
				  TYPE_DIRECTIONAL);
	obs_property_list_add_int(p, obs_module_text(TYPE_ZOOM_LABEL),
				  TYPE_ZOOM);
	obs_property_list_add_int(p, obs_module_text(TYPE_MOTION_LABEL),
				  TYPE_MOTION);
	// obs_property_list_add_int(p,
	// 			  obs_module_text(TYPE_TILTSHIFT_LABEL),
	// 			  TYPE_TILTSHIFT);
}

void gaussian_setup_callbacks(struct composite_blur_filter_data *data)
{
	data->video_render = render_video_gaussian;
	data->load_effect = load_effect_gaussian;
	data->update = update_gaussian;
}

void update_gaussian(struct composite_blur_filter_data *data)
{
	if (data->radius != data->radius_last) {
		data->radius_last = data->radius;
		sample_kernel(data->radius, data);
	}
}

void render_video_gaussian(struct composite_blur_filter_data *data)
{
	switch (data->blur_type) {
	case TYPE_AREA:
		gaussian_area_blur(data);
		break;
	case TYPE_DIRECTIONAL:
		gaussian_directional_blur(data);
		break;
	case TYPE_ZOOM:
		gaussian_zoom_blur(data);
		break;
	case TYPE_MOTION:
		gaussian_motion_blur(data);
		break;
	}
}

void load_effect_gaussian(struct composite_blur_filter_data *filter)
{
	switch (filter->blur_type) {
	case TYPE_AREA:
		load_1d_gaussian_effect(filter);
		break;
	case TYPE_DIRECTIONAL:
		load_1d_gaussian_effect(filter);
		break;
	case TYPE_ZOOM:
		load_radial_gaussian_effect(filter);
		break;
	case TYPE_MOTION:
		load_motion_gaussian_effect(filter);
		break;
	}
}

/*
 *  Performs an area blur using the gaussian kernel.  Blur is
 *  equal in both x and y directions.
 */
static void gaussian_area_blur(struct composite_blur_filter_data *data)
{
	gs_effect_t *effect = data->effect;
	gs_effect_t *composite_effect = data->composite_effect;

	gs_texture_t *texture = gs_texrender_get_texture(data->input_texrender);

	if (!effect || !texture) {
		return;
	}

	texture = blend_composite(texture, data);

	data->render2 = create_or_reset_texrender(data->render2);

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, texture);

	gs_eparam_t *weight = gs_effect_get_param_by_name(effect, "weight");

	gs_effect_set_val(weight, data->kernel.array,
			  data->kernel.num * sizeof(float));

	gs_eparam_t *offset = gs_effect_get_param_by_name(effect, "offset");
	gs_effect_set_val(offset, data->offset.array,
			  data->offset.num * sizeof(float));

	const int k_size = (int)data->kernel_size;
	gs_eparam_t *kernel_size =
		gs_effect_get_param_by_name(effect, "kernel_size");
	gs_effect_set_int(kernel_size, k_size);

	gs_eparam_t *texel_step =
		gs_effect_get_param_by_name(effect, "texel_step");
	struct vec2 direction;

	// 1. First pass- apply 1D blur kernel to horizontal dir.

	direction.x = 1.0f / data->width;
	direction.y = 0.0f;
	gs_effect_set_vec2(texel_step, &direction);

	set_blending_parameters();
	//set_render_parameters();

	if (gs_texrender_begin(data->render2, data->width, data->height)) {
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(texture, 0, data->width, data->height);
		gs_texrender_end(data->render2);
	}

	// 2. Save texture from first pass in variable "texture"
	texture = gs_texrender_get_texture(data->render2);

	// 3. Second Pass- Apply 1D blur kernel vertically.
	image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, texture);

	direction.x = 0.0f;
	direction.y = 1.0f / data->height;
	gs_effect_set_vec2(texel_step, &direction);

	data->output_texrender =
		create_or_reset_texrender(data->output_texrender);

	if (gs_texrender_begin(data->output_texrender, data->width,
			       data->height)) {
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(texture, 0, data->width, data->height);
		gs_texrender_end(data->output_texrender);
	}

	gs_blend_state_pop();
}

/*
 *  Performs a directional blur using the gaussian kernel.
 */
static void gaussian_directional_blur(struct composite_blur_filter_data *data)
{
	gs_effect_t *effect = data->effect;
	gs_effect_t *composite_effect = data->composite_effect;

	gs_texture_t *texture = gs_texrender_get_texture(data->input_texrender);

	if (!effect || !texture) {
		return;
	}

	texture = blend_composite(texture, data);

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, texture);

	gs_eparam_t *weight = gs_effect_get_param_by_name(effect, "weight");

	gs_effect_set_val(weight, data->kernel.array,
			  data->kernel.num * sizeof(float));

	gs_eparam_t *offset = gs_effect_get_param_by_name(effect, "offset");
	gs_effect_set_val(offset, data->offset.array,
			  data->offset.num * sizeof(float));

	const int k_size = (int)data->kernel_size;
	gs_eparam_t *kernel_size =
		gs_effect_get_param_by_name(effect, "kernel_size");
	gs_effect_set_int(kernel_size, k_size);

	gs_eparam_t *texel_step =
		gs_effect_get_param_by_name(effect, "texel_step");
	struct vec2 direction;

	// 1. Single pass- blur only in one direction
	float rads = -data->angle * (M_PI / 180.0f);
	direction.x = (float)cos(rads) / data->width;
	direction.y = (float)sin(rads) / data->height;
	gs_effect_set_vec2(texel_step, &direction);

	set_blending_parameters();
	//set_render_parameters();

	data->output_texrender =
		create_or_reset_texrender(data->output_texrender);

	if (gs_texrender_begin(data->output_texrender, data->width,
			       data->height)) {
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(texture, 0, data->width, data->height);
		gs_texrender_end(data->output_texrender);
	}

	gs_blend_state_pop();
}

/*
 *  Performs a motion blur using the gaussian kernel.
 */
static void gaussian_motion_blur(struct composite_blur_filter_data *data)
{
	gs_effect_t *effect = data->effect;
	gs_effect_t *composite_effect = data->composite_effect;

	gs_texture_t *texture = gs_texrender_get_texture(data->input_texrender);

	if (!effect || !texture) {
		return;
	}

	texture = blend_composite(texture, data);

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, texture);

	gs_eparam_t *weight = gs_effect_get_param_by_name(effect, "weight");

	gs_effect_set_val(weight, data->kernel.array,
			  data->kernel.num * sizeof(float));

	gs_eparam_t *offset = gs_effect_get_param_by_name(effect, "offset");
	gs_effect_set_val(offset, data->offset.array,
			  data->offset.num * sizeof(float));

	const int k_size = (int)data->kernel_size;
	gs_eparam_t *kernel_size =
		gs_effect_get_param_by_name(effect, "kernel_size");
	gs_effect_set_int(kernel_size, k_size);

	gs_eparam_t *texel_step =
		gs_effect_get_param_by_name(effect, "texel_step");
	struct vec2 direction;

	// 1. Single pass- blur only in one direction
	float rads = -data->angle * (M_PI / 180.0f);
	direction.x = (float)cos(rads) / data->width;
	direction.y = (float)sin(rads) / data->height;
	gs_effect_set_vec2(texel_step, &direction);

	set_blending_parameters();
	//set_render_parameters();

	data->output_texrender =
		create_or_reset_texrender(data->output_texrender);

	if (gs_texrender_begin(data->output_texrender, data->width,
			       data->height)) {
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(texture, 0, data->width, data->height);
		gs_texrender_end(data->output_texrender);
	}

	gs_blend_state_pop();
}

/*
 *  Performs a zoom blur using the gaussian kernel. Blur for a pixel
 *  is performed in direction of zoom center point.
 */
static void gaussian_zoom_blur(struct composite_blur_filter_data *data)
{
	gs_effect_t *effect = data->effect;
	gs_effect_t *composite_effect = data->composite_effect;

	gs_texture_t *texture = gs_texrender_get_texture(data->input_texrender);

	if (!effect || !texture) {
		return;
	}

	texture = blend_composite(texture, data);

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, texture);

	gs_eparam_t *weight = gs_effect_get_param_by_name(effect, "weight");

	gs_effect_set_val(weight, data->kernel.array,
			  data->kernel.num * sizeof(float));

	gs_eparam_t *offset = gs_effect_get_param_by_name(effect, "offset");
	gs_effect_set_val(offset, data->offset.array,
			  data->offset.num * sizeof(float));

	const int k_size = (int)data->kernel_size;
	gs_eparam_t *kernel_size =
		gs_effect_get_param_by_name(effect, "kernel_size");
	gs_effect_set_int(kernel_size, k_size);

	gs_eparam_t *radial_center =
		gs_effect_get_param_by_name(effect, "radial_center");

	struct vec2 coord;

	coord.x = data->center_x;
	coord.y = data->center_y;

	// 1. Single pass- blur only in one direction
	gs_effect_set_vec2(radial_center, &coord);

	gs_eparam_t *uv_size = gs_effect_get_param_by_name(effect, "uv_size");

	struct vec2 size;
	size.x = (float)data->width;
	size.y = (float)data->height;

	gs_effect_set_vec2(uv_size, &size);

	set_blending_parameters();
	//set_render_parameters();

	data->output_texrender =
		create_or_reset_texrender(data->output_texrender);

	if (gs_texrender_begin(data->output_texrender, data->width,
			       data->height)) {
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(texture, 0, data->width, data->height);
		gs_texrender_end(data->output_texrender);
	}

	gs_blend_state_pop();
}

static void load_1d_gaussian_effect(struct composite_blur_filter_data *filter)
{
	const char *effect_file_path = "/shaders/gaussian_1d.effect";
	filter->effect = load_shader_effect(filter->effect, effect_file_path);
	if (filter->effect) {
		size_t effect_count = gs_effect_get_num_params(filter->effect);
		for (size_t effect_index = 0; effect_index < effect_count;
		     effect_index++) {
			gs_eparam_t *param = gs_effect_get_param_by_idx(
				filter->effect, effect_index);
			struct gs_effect_param_info info;
			gs_effect_get_param_info(param, &info);
			if (strcmp(info.name, "uv_size") == 0) {
				filter->param_uv_size = param;
			} else if (strcmp(info.name, "dir") == 0) {
				filter->param_dir = param;
			}
		}
	}
}

static void
load_motion_gaussian_effect(struct composite_blur_filter_data *filter)
{
	const char *effect_file_path = "/shaders/gaussian_motion.effect";
	filter->effect = load_shader_effect(filter->effect, effect_file_path);
	if (filter->effect) {
		size_t effect_count = gs_effect_get_num_params(filter->effect);
		for (size_t effect_index = 0; effect_index < effect_count;
		     effect_index++) {
			gs_eparam_t *param = gs_effect_get_param_by_idx(
				filter->effect, effect_index);
			struct gs_effect_param_info info;
			gs_effect_get_param_info(param, &info);
			if (strcmp(info.name, "uv_size") == 0) {
				filter->param_uv_size = param;
			} else if (strcmp(info.name, "dir") == 0) {
				filter->param_dir = param;
			}
		}
	}
}

static void
load_radial_gaussian_effect(struct composite_blur_filter_data *filter)
{
	const char *effect_file_path = "/shaders/gaussian_radial.effect";
	filter->effect = load_shader_effect(filter->effect, effect_file_path);
	if (filter->effect) {
		size_t effect_count = gs_effect_get_num_params(filter->effect);
		for (size_t effect_index = 0; effect_index < effect_count;
		     effect_index++) {
			gs_eparam_t *param = gs_effect_get_param_by_idx(
				filter->effect, effect_index);
			struct gs_effect_param_info info;
			gs_effect_get_param_info(param, &info);
			if (strcmp(info.name, "uv_size") == 0) {
				filter->param_uv_size = param;
			} else if (strcmp(info.name, "dir") == 0) {
				filter->param_dir = param;
			}
		}
	}
}

static void sample_kernel(float radius,
			  struct composite_blur_filter_data *filter)
{
	const size_t max_size = 128;
	const float max_radius = 250.0;
	const float min_radius = 0.0;
	size_t d_kernel_size = 0;

	fDarray d_weights;
	da_init(d_weights);

	fDarray weights;
	da_init(weights);

	radius *= 3.0f;
	radius = max(min(radius, max_radius), min_radius);

	// 1. Calculate discrete weights
	const float bins_per_pixel =
		((2.f * (float)gaussian_kernel_size - 1.f)) /
		(1.f + 2.f * radius);
	size_t current_bin = 0;
	float fractional_bin = 0.5f;
	float ceil_radius = (radius - (float)floor(radius)) < 0.001f
				    ? radius
				    : (float)ceil(radius);
	float fractional_extra = 1.0f - (ceil_radius - radius);

	for (int i = 0; i <= (int)ceil_radius; i++) {
		float cur_radius = (float)i;
		float fractional_pixel = i < (int)ceil_radius ? 1.0f
					 : fractional_extra < 0.002f
						 ? 1.0f
						 : fractional_extra;
		float bpp_mult = i == 0 ? 0.5f : 1.0f;
		float weight = 1.0f / bpp_mult * fractional_bin *
			       gaussian_kernel[current_bin];
		float remaining_bins =
			bpp_mult * fractional_pixel * bins_per_pixel -
			fractional_bin;
		while ((int)floor(remaining_bins) > 0) {
			current_bin++;
			weight +=
				1.0f / bpp_mult * gaussian_kernel[current_bin];
			remaining_bins -= 1.f;
		}
		current_bin++;
		if (remaining_bins > 1.e-6f) {
			weight += 1.0f / bpp_mult *
				  gaussian_kernel[current_bin] * remaining_bins;
			fractional_bin = 1.0f - remaining_bins;
		} else {
			fractional_bin = 1.0f;
		}
		if (weight > 1.0001f || weight < 0.0f) {
			obs_log(LOG_WARNING,
				"   === BAD WEIGHT VALUE FOR GAUSSIAN === [%d] %f",
				weights.num + 1, weight);
			weight = 0.0;
		}
		da_push_back(d_weights, &weight);
	}

	fDarray offsets;
	da_init(offsets);

	fDarray d_offsets;
	da_init(d_offsets);

	// 2. Calculate discrete offsets
	for (int i = 0; i <= (int)ceil_radius; i++) {
		float val = (float)i;
		da_push_back(d_offsets, &val);
	}

	// 3. Calculate linear sampled weights and offsets
	da_push_back(weights, &d_weights.array[0]);
	da_push_back(offsets, &d_offsets.array[0]);

	for (size_t i = 1; i < d_weights.num - 1; i += 2) {
		const float weight =
			d_weights.array[i] + d_weights.array[i + 1];
		da_push_back(weights, &weight);
		const float offset =
			(d_offsets.array[i] * d_weights.array[i] +
			 d_offsets.array[i + 1] * d_weights.array[i + 1]) /
			weight;
		da_push_back(offsets, &offset);
	}
	if (d_weights.num % 2 == 0) {
		const float weight = d_weights.array[d_weights.num - 1];
		const float offset = d_offsets.array[d_offsets.num - 1];
		da_push_back(weights, &weight);
		da_push_back(offsets, &offset);
	}

	// 4. Pad out kernel arrays to length of max_size
	const size_t padding = max_size - weights.num;
	filter->kernel_size = weights.num;

	for (size_t i = 0; i < padding; i++) {
		float pad = 0.0f;
		da_push_back(weights, &pad);
	}
	da_free(filter->kernel);
	filter->kernel = weights;

	for (size_t i = 0; i < padding; i++) {
		float pad = 0.0f;
		da_push_back(offsets, &pad);
	}

	da_free(filter->offset);
	filter->offset = offsets;
}