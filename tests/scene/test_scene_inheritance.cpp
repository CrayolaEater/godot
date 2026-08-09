/**************************************************************************/
/*  test_scene_inheritance.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_scene_inheritance)

#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"
#include "tests/test_utils.h"

namespace TestSceneInheritance {

class _TestRootBase : public Node {
	GDCLASS(_TestRootBase, Node);

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_base_value", "value"), &_TestRootBase::set_base_value);
		ClassDB::bind_method(D_METHOD("get_base_value"), &_TestRootBase::get_base_value);

		ADD_PROPERTY(PropertyInfo(Variant::INT, "base_value"), "set_base_value", "get_base_value");
	}

public:
	int base_value = 0;

	void set_base_value(int p_value) { base_value = p_value; }
	int get_base_value() const { return base_value; }
};

class _TestRootSub : public _TestRootBase {
	GDCLASS(_TestRootSub, _TestRootBase);

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_sub_value", "value"), &_TestRootSub::set_sub_value);
		ClassDB::bind_method(D_METHOD("get_sub_value"), &_TestRootSub::get_sub_value);

		ADD_PROPERTY(PropertyInfo(Variant::INT, "sub_value"), "set_sub_value", "get_sub_value");
	}

public:
	int sub_value = 0;

	void set_sub_value(int p_value) { sub_value = p_value; }
	int get_sub_value() const { return sub_value; }
};

// Deliberately not related to `_TestRootBase`, to exercise the fallback path.
class _TestRootUnrelated : public Node {
	GDCLASS(_TestRootUnrelated, Node);

protected:
	static void _bind_methods() {}
};

static void register_test_types() {
	GDREGISTER_CLASS(_TestRootBase);
	GDREGISTER_CLASS(_TestRootSub);
	GDREGISTER_CLASS(_TestRootUnrelated);
}

// Mirrors the property transfer SceneTreeDock::_replace_node() performs before swapping a node:
// every stored value that differs from the old node's class default is carried to the new node.
// Without it the new node would hold class defaults, which pack() would then record as overrides.
static void copy_non_default_properties(Node *p_from, Node *p_to) {
	Node *defaults = Object::cast_to<Node>(ClassDB::instantiate(p_from->get_class()));
	REQUIRE(defaults != nullptr);

	List<PropertyInfo> pinfo;
	p_from->get_property_list(&pinfo);
	for (const PropertyInfo &E : pinfo) {
		if (!(E.usage & PROPERTY_USAGE_STORAGE)) {
			continue;
		}

		bool valid = false;
		const Variant default_val = defaults->get(E.name, &valid);
		if (!valid || default_val != p_from->get(E.name)) {
			p_to->set(E.name, p_from->get(E.name));
		}
	}

	memdelete(defaults);
}

// Builds a scene whose root is a `_TestRootBase` named "Base" with one child, saves it, and
// returns it loaded back from disk with its state path set the way the editor sets it.
static Ref<PackedScene> make_base_scene(const String &p_path) {
	_TestRootBase *root = memnew(_TestRootBase);
	root->set_name("Base");
	root->set_base_value(7);

	Node *child = memnew(Node);
	child->set_name("Child");
	root->add_child(child);
	child->set_owner(root);

	Ref<PackedScene> packed = memnew(PackedScene);
	REQUIRE(packed->pack(root) == OK);
	REQUIRE(ResourceSaver::save(packed, p_path) == OK);
	memdelete(root);

	Error err = OK;
	Ref<PackedScene> loaded = ResourceLoader::load(p_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE(err == OK);
	REQUIRE(loaded.is_valid());
	loaded->get_state()->set_path(p_path);
	return loaded;
}

TEST_CASE("[SceneTree][SceneInheritance] Root type of a packed scene") {
	register_test_types();

	const String base_path = TestUtils::get_temp_path("scene_inheritance_base.tscn");
	Ref<PackedScene> base = make_base_scene(base_path);

	CHECK(base->get_root_type() == StringName("_TestRootBase"));

	DirAccess::remove_file_or_error(base_path);
}

TEST_CASE("[SceneTree][SceneInheritance] Inherited scene root retyped to a subclass") {
	register_test_types();

	const String base_path = TestUtils::get_temp_path("scene_inheritance_base.tscn");
	const String derived_path = TestUtils::get_temp_path("scene_inheritance_derived.tscn");
	Ref<PackedScene> base = make_base_scene(base_path);

	// Open the base scene as an inherited scene, the way EditorNode::load_scene() does.
	Node *inherited = base->instantiate(PackedScene::GEN_EDIT_STATE_MAIN_INHERITED);
	REQUIRE(inherited != nullptr);
	inherited->set_scene_inherited_state(base->get_state());
	inherited->set_scene_file_path(String());
	CHECK(inherited->get_class() == "_TestRootBase");

	// Retype the root, the way SceneTreeDock::_replace_node() does.
	_TestRootSub *new_root = memnew(_TestRootSub);
	copy_non_default_properties(inherited, new_root);
	inherited->replace_by(new_root, true);
	new_root->set_scene_inherited_state(base->get_state());
	memdelete(inherited);

	new_root->set_sub_value(42);

	SUBCASE("the subclass is stored as the root type when packing") {
		Ref<PackedScene> derived = memnew(PackedScene);
		REQUIRE(derived->pack(new_root) == OK);

		Ref<SceneState> state = derived->get_state();
		CHECK(state->get_node_type(0) == StringName("_TestRootSub"));
		// The base scene is still recorded, so this remains an inherited scene.
		CHECK(state->get_base_scene_state().is_valid());
		CHECK(derived->get_root_type() == StringName("_TestRootSub"));
	}

	SUBCASE("the subclass survives a save/load round trip") {
		Ref<PackedScene> derived = memnew(PackedScene);
		REQUIRE(derived->pack(new_root) == OK);
		REQUIRE(ResourceSaver::save(derived, derived_path) == OK);

		Error err = OK;
		Ref<PackedScene> reloaded = ResourceLoader::load(derived_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
		REQUIRE(err == OK);
		REQUIRE(reloaded.is_valid());

		Node *instantiated = reloaded->instantiate(PackedScene::GEN_EDIT_STATE_MAIN);
		REQUIRE(instantiated != nullptr);

		// The root is the subclass, but everything the base scene contributed is still applied.
		CHECK(instantiated->get_class() == "_TestRootSub");
		CHECK(Object::cast_to<_TestRootBase>(instantiated)->get_base_value() == 7);
		CHECK(Object::cast_to<_TestRootSub>(instantiated)->get_sub_value() == 42);
		REQUIRE(instantiated->get_child_count() == 1);
		CHECK(instantiated->get_child(0)->get_name() == "Child");

		memdelete(instantiated);
		DirAccess::remove_file_or_error(derived_path);
	}

	memdelete(new_root);
	DirAccess::remove_file_or_error(base_path);
}

TEST_CASE("[SceneTree][SceneInheritance] Instantiated sub-scene root retyped to a subclass") {
	register_test_types();

	const String sub_path = TestUtils::get_temp_path("scene_inheritance_sub.tscn");
	const String outer_path = TestUtils::get_temp_path("scene_inheritance_outer.tscn");
	Ref<PackedScene> sub = make_base_scene(sub_path);

	Node *outer = memnew(Node);
	outer->set_name("Outer");

	Node *instance = sub->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	REQUIRE(instance != nullptr);
	instance->set_name("Sub");
	outer->add_child(instance);
	instance->set_owner(outer);
	CHECK(instance->get_class() == "_TestRootBase");

	// Retype the instance root, carrying the scene link across as the dock does.
	_TestRootSub *new_instance = memnew(_TestRootSub);
	const Ref<SceneState> instance_state = instance->get_scene_instance_state();
	copy_non_default_properties(instance, new_instance);
	instance->replace_by(new_instance, true);
	new_instance->set_scene_instance_state(instance_state);
	new_instance->set_name("Sub");
	memdelete(instance);

	Ref<PackedScene> packed_outer = memnew(PackedScene);
	REQUIRE(packed_outer->pack(outer) == OK);

	Ref<SceneState> state = packed_outer->get_state();
	REQUIRE(state->get_node_count() == 2);
	CHECK(state->get_node_type(0) == StringName("Node"));
	CHECK(state->get_node_type(1) == StringName("_TestRootSub"));
	CHECK(state->get_node_instance(1).is_valid());

	REQUIRE(ResourceSaver::save(packed_outer, outer_path) == OK);

	Error err = OK;
	Ref<PackedScene> reloaded = ResourceLoader::load(outer_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE(err == OK);
	Node *instantiated = reloaded->instantiate(PackedScene::GEN_EDIT_STATE_MAIN);
	REQUIRE(instantiated != nullptr);
	REQUIRE(instantiated->get_child_count() == 1);

	Node *reloaded_sub = instantiated->get_child(0);
	CHECK(reloaded_sub->get_class() == "_TestRootSub");
	CHECK(Object::cast_to<_TestRootBase>(reloaded_sub)->get_base_value() == 7);
	// The sub-scene's own children are still built.
	REQUIRE(reloaded_sub->get_child_count() == 1);
	CHECK(reloaded_sub->get_child(0)->get_name() == "Child");

	memdelete(instantiated);
	memdelete(outer);
	DirAccess::remove_file_or_error(outer_path);
	DirAccess::remove_file_or_error(sub_path);
}

TEST_CASE("[SceneTree][SceneInheritance] Root type override validation") {
	register_test_types();

	const String base_path = TestUtils::get_temp_path("scene_inheritance_base.tscn");
	Ref<PackedScene> base = make_base_scene(base_path);

	SUBCASE("a subclass override is applied") {
		Node *node = base->instantiate_with_root_type(PackedScene::GEN_EDIT_STATE_DISABLED, "_TestRootSub");
		REQUIRE(node != nullptr);
		CHECK(node->get_class() == "_TestRootSub");
		// Base scene values are still applied to the subclass.
		CHECK(Object::cast_to<_TestRootBase>(node)->get_base_value() == 7);
		memdelete(node);
	}

	SUBCASE("an unrelated type falls back to the base type") {
		ERR_PRINT_OFF;
		Node *node = base->instantiate_with_root_type(PackedScene::GEN_EDIT_STATE_DISABLED, "_TestRootUnrelated");
		ERR_PRINT_ON;
		REQUIRE(node != nullptr);
		CHECK(node->get_class() == "_TestRootBase");
		memdelete(node);
	}

	SUBCASE("a missing type falls back to the base type") {
		ERR_PRINT_OFF;
		Node *node = base->instantiate_with_root_type(PackedScene::GEN_EDIT_STATE_DISABLED, "_TestRootDoesNotExist");
		ERR_PRINT_ON;
		REQUIRE(node != nullptr);
		CHECK(node->get_class() == "_TestRootBase");
		memdelete(node);
	}

	SUBCASE("an empty override keeps the declared type") {
		Node *node = base->instantiate_with_root_type(PackedScene::GEN_EDIT_STATE_DISABLED, StringName());
		REQUIRE(node != nullptr);
		CHECK(node->get_class() == "_TestRootBase");
		memdelete(node);
	}

	DirAccess::remove_file_or_error(base_path);
}

} // namespace TestSceneInheritance
