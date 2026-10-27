#include "fs2_screen.h"

#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void Fs2Screen::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_frames_left", "n"), &Fs2Screen::set_frames_left);
	ClassDB::bind_method(D_METHOD("get_frames_committed"), &Fs2Screen::get_frames_committed);
	ClassDB::bind_method(D_METHOD("get_image"), &Fs2Screen::get_image);
	ClassDB::bind_method(D_METHOD("save_png", "path"), &Fs2Screen::save_png);
}

void Fs2Screen::_ready()
{
	viewport = memnew(SubViewport);
	viewport->set_size(Vector2i(640, 480));
	viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
	viewport->set_disable_3d(true);
	viewport->set_transparent_background(false);
	add_child(viewport);

	view = memnew(TextureRect);
	view->set_texture(viewport->get_texture());
	view->set_anchors_preset(Control::PRESET_FULL_RECT);
	view->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	view->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	view->set_texture_filter(CanvasItem::TEXTURE_FILTER_NEAREST);
	add_child(view);

	sink.init(viewport);
}

void Fs2Screen::_exit_tree()
{
	sink.shutdown();
}

Ref<Image> Fs2Screen::get_image()
{
	if (viewport == nullptr) {
		return Ref<Image>();
	}
	Ref<Texture2D> tex = viewport->get_texture();
	return tex.is_valid() ? tex->get_image() : Ref<Image>();
}

bool Fs2Screen::save_png(const String &path)
{
	Ref<Image> img = get_image();
	return img.is_valid() && !img->is_empty() && img->save_png(path) == OK;
}
