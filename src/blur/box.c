#include "box.h"

void set_box_blur_types(obs_properties_t *props)
{
	obs_property_t *p = obs_properties_get(props, "blur_type");
	obs_property_list_clear(p);
	obs_property_list_add_int(p, obs_module_text(TYPE_AREA_LABEL),
				  TYPE_AREA);
	obs_property_list_add_int(p, obs_module_text(TYPE_DIRECTIONAL_LABEL),
				  TYPE_DIRECTIONAL);
	obs_property_list_add_int(p, obs_module_text(TYPE_ZOOM_LABEL),
				  TYPE_ZOOM);
	// obs_property_list_add_int(p, obs_module_text(TYPE_MOTION_LABEL),
	// 			  TYPE_MOTION);
	obs_property_list_add_int(p, obs_module_text(TYPE_TILTSHIFT_LABEL),
				  TYPE_TILTSHIFT);
}

void box_setup_callbacks(struct composite_blur_filter_data *data)
{
	data->video_render = render_video_box;
	data->load_effect = load_effect_box;
	data->update = NULL;
}

void render_video_box(struct composite_blur_filter_data *data)
{
	switch (data->blur_type) {
	case TYPE_AREA:
		box_area_blur(data);
		break;
	case TYPE_DIRECTIONAL:
		box_directional_blur(data);
		break;
	case TYPE_ZOOM:
		box_zoom_blur(data);
		break;
	case TYPE_TILTSHIFT:
		box_tilt_shift_blur(data);
		break;
	}
}

void load_effect_box(struct composite_blur_filter_data *filter)
{
	switch (filter->blur_type) {
	case TYPE_AREA:
		load_1d_box_effect(filter);
		break;
	case TYPE_DIRECTIONAL:
		load_1d_box_effect(filter);
		break;
	case TYPE_ZOOM:
		load_radial_box_effect(filter);
		break;
	case TYPE_TILTSHIFT:
		load_tiltshift_box_effect(filter);
		break;
	}
}

/*
 *  Performs an area blur using the box kernel.  Blur is
 *  equal in both x and y directions.
 */
static void box_area_blur(struct composite_blur_filter_data *data)
{
	gs_effect_t *effect = data->effect;
	gs_effect_t *composite_effect = data->composite_effect;

	gs_texture_t *texture = gs_texrender_get_texture(data->input_texrender);

	if (!effect || !texture) {
		return;
	}

	texture = blend_composite(texture, data);

	for (int i = 0; i < data->passes; i++) {
		data->render2 = create_or_reset_texrender(data->render2);

		gs_eparam_t *image =
			gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, texture);

		const float radius = (float)data->radius;
		gs_eparam_t *radius_param =
			gs_effect_get_param_by_name(effect, "radius");
		gs_effect_set_float(radius_param, radius);

		gs_eparam_t *texel_step =
			gs_effect_get_param_by_name(effect, "texel_step");
		struct vec2 direction;

		// 1. First pass- apply 1D blur kernel to horizontal dir.

		direction.x = 1.0f / data->width;
		direction.y = 0.0f;
		gs_effect_set_vec2(texel_step, &direction);

		set_blending_parameters();
		//set_render_parameters();

		if (gs_texrender_begin(data->render2, data->width,
				       data->height)) {
			while (gs_effect_loop(effect, "Draw"))
				gs_draw_sprite(texture, 0, data->width,
					       data->height);
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
				gs_draw_sprite(texture, 0, data->width,
					       data->height);
			gs_texrender_end(data->output_texrender);
		}

		texture = gs_texrender_get_texture(data->output_texrender);
		gs_blend_state_pop();
	}
}

/*
 *  Performs a directional blur using the gaussian kernel.
 */
static void box_directional_blur(struct composite_blur_filter_data *data)
{
	gs_effect_t *effect = data->effect;
	gs_effect_t *composite_effect = data->composite_effect;

	gs_texture_t *texture = gs_texrender_get_texture(data->input_texrender);

	if (!effect || !texture) {
		return;
	}

	texture = blend_composite(texture, data);

	for (int i = 0; i < data->passes; i++) {
		gs_texrender_t *tmp = data->render2;
		data->render2 = data->output_texrender;
		data->output_texrender = tmp;

		gs_eparam_t *image =
			gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, texture);

		const float radius = (float)data->radius;
		gs_eparam_t *radius_param =
			gs_effect_get_param_by_name(effect, "radius");
		gs_effect_set_float(radius_param, radius);

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
				gs_draw_sprite(texture, 0, data->width,
					       data->height);
			gs_texrender_end(data->output_texrender);
		}
		texture = gs_texrender_get_texture(data->output_texrender);
		gs_blend_state_pop();
	}
}

/*
 *  Performs a zoom blur using the gaussian kernel. Blur for a pixel
 *  is performed in direction of zoom center point.
 */
static void box_zoom_blur(struct composite_blur_filter_data *data)
{
	gs_effect_t *effect = data->effect;
	gs_effect_t *composite_effect = data->composite_effect;

	gs_texture_t *texture = gs_texrender_get_texture(data->input_texrender);

	if (!effect || !texture) {
		return;
	}

	texture = blend_composite(texture, data);

	for (int i = 0; i < data->passes; i++) {
		gs_texrender_t *tmp = data->render2;
		data->render2 = data->output_texrender;
		data->output_texrender = tmp;

		gs_eparam_t *image =
			gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, texture);

		const float radius = (float)data->radius;
		gs_eparam_t *radius_param =
			gs_effect_get_param_by_name(effect, "radius");
		gs_effect_set_float(radius_param, radius);

		gs_eparam_t *texel_step =
			gs_effect_get_param_by_name(effect, "texel_step");

		gs_eparam_t *radial_center =
			gs_effect_get_param_by_name(effect, "radial_center");

		struct vec2 coord;

		coord.x = data->center_x;
		coord.y = data->center_y;

		// 1. Single pass- blur only in one direction
		gs_effect_set_vec2(radial_center, &coord);

		gs_eparam_t *uv_size =
			gs_effect_get_param_by_name(effect, "uv_size");

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
				gs_draw_sprite(texture, 0, data->width,
					       data->height);
			gs_texrender_end(data->output_texrender);
		}
		texture = gs_texrender_get_texture(data->output_texrender);
		gs_blend_state_pop();
	}
}

/*
 *  Performs an area blur using the box kernel.  Blur is
 *  equal in both x and y directions.
 */
static void box_tilt_shift_blur(struct composite_blur_filter_data *data)
{
	gs_effect_t *effect = data->effect;
	gs_effect_t *composite_effect = data->composite_effect;

	gs_texture_t *texture = gs_texrender_get_texture(data->input_texrender);

	if (!effect || !texture) {
		return;
	}

	texture = blend_composite(texture, data);

	for (int i = 0; i < data->passes; i++) {
		data->render2 = create_or_reset_texrender(data->render2);

		gs_eparam_t *image =
			gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, texture);
		const float radius = (float)data->radius;
		gs_eparam_t *radius_param =
			gs_effect_get_param_by_name(effect, "radius");
		gs_effect_set_float(radius_param, radius);

		const int radius_i = (int)data->radius;
		gs_eparam_t *radius_i_param =
			gs_effect_get_param_by_name(effect, "radius_i");
		gs_effect_set_int(radius_i_param, radius_i);

		const float bottom = 1.0f - (float)data->tilt_shift_bottom;
		gs_eparam_t *bottom_param =
			gs_effect_get_param_by_name(effect, "bottom");
		gs_effect_set_float(bottom_param, bottom);

		const float top = (float)data->tilt_shift_top;
		gs_eparam_t *top_param =
			gs_effect_get_param_by_name(effect, "top");
		gs_effect_set_float(top_param, top);

		gs_eparam_t *texel_step =
			gs_effect_get_param_by_name(effect, "texel_step");
		struct vec2 direction;

		// 1. First pass- apply 1D blur kernel to horizontal dir.

		direction.x = 1.0f / data->width;
		direction.y = 0.0f;

		// obs_log(LOG_INFO, "%f, %f, %f, %f, %f", radius, bottom, top,
		// 	direction.x, direction.y);

		gs_effect_set_vec2(texel_step, &direction);

		gs_eparam_t *uv_size =
			gs_effect_get_param_by_name(effect, "uv_size");

		struct vec2 size;
		size.x = (float)data->width;
		size.y = (float)data->height;

		gs_effect_set_vec2(uv_size, &size);

		set_blending_parameters();
		//set_render_parameters();

		if (gs_texrender_begin(data->render2, data->width,
				       data->height)) {
			while (gs_effect_loop(effect, "Draw"))
				gs_draw_sprite(texture, 0, data->width,
					       data->height);
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
				gs_draw_sprite(texture, 0, data->width,
					       data->height);
			gs_texrender_end(data->output_texrender);
		}
		texture = gs_texrender_get_texture(data->output_texrender);
		gs_blend_state_pop();
	}
}

static void load_1d_box_effect(struct composite_blur_filter_data *filter)
{
	const char *effect_file_path = "/shaders/box_1d.effect";
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

static void load_tiltshift_box_effect(struct composite_blur_filter_data *filter)
{
	const char *effect_file_path = "/shaders/box_tiltshift.effect";
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

static void load_radial_box_effect(struct composite_blur_filter_data *filter)
{
	const char *effect_file_path = "/shaders/box_radial.effect";
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