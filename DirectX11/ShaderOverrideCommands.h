#pragma once

#include <memory>

#include "HackerDevice.h"
#include "HackerContext.h"

// Forward declarations to resolve circular includes (we include Hacker*.h,
// which includes Globals.h, which includes us):
class HackerDevice;
class HackerContext;

struct ShaderOverrideState {
	// Used to avoid querying the render target dimensions twice in the
	// common case we are going to store both width & height in separate
	// ini params:
	float rt_width, rt_height;

	// Anything that needs to be updated at the end of the command list:
	bool update_params;

	ShaderOverrideState() :
		rt_width(-1),
		rt_height(-1),
		update_params(false)
	{}
};

class ShaderOverrideCommand {
public:
	virtual ~ShaderOverrideCommand() {};

	virtual void run(HackerContext*, ID3D11Device*, ID3D11DeviceContext*, ShaderOverrideState*) = 0;
};

// Using vector of pointers to allow mixed types, and unique_ptr to handle
// destruction of each object:
typedef std::vector<std::unique_ptr<ShaderOverrideCommand>> ShaderOverrideCommandList;

enum class ParamOverrideType {
	INVALID,
	VALUE,
	RT_WIDTH,
	RT_HEIGHT,
	RES_WIDTH,
	RES_HEIGHT,
	TEXTURE,	// Needs shader type and slot number specified in
			// [ShaderOverride]. [TextureOverride] sections can
			// specify filter_index=N to define the value passed in
			// here. Special values for no [TextureOverride]
			// section = 0.0, or [TextureOverride] with no
			// filter_index = 1.0
	// TODO:
	// DEPTH_ACTIVE
	// VERTEX_SHADER    (how best to pass these in?
	// HULL_SHADER       Maybe the low/hi 32bits of hash? Or all 64bits split in two?
	// DOMAIN_SHADER     Maybe an index or some other mapping? Perhaps something like Helix mod's texture CRCs?
	// GEOMETRY_SHADER   Or... maybe don't bother! We can already achieve this by setting the value in
	// PIXEL_SHADER      the partner shaders instead! Limiting to a single draw call would be helpful)
	// etc.
};
static EnumName_t<const wchar_t *, ParamOverrideType> ParamOverrideTypeNames[] = {
	{L"rt_width", ParamOverrideType::RT_WIDTH},
	{L"rt_height", ParamOverrideType::RT_HEIGHT},
	{L"res_width", ParamOverrideType::RES_WIDTH},
	{L"res_height", ParamOverrideType::RES_HEIGHT},
	{NULL, ParamOverrideType::INVALID} // End of list marker
};
class ParamOverride : public ShaderOverrideCommand {
public:
	int param_idx;
	float DirectX::XMFLOAT4::*param_component;

	ParamOverrideType type;
	float val;

	// For texture filters:
	wchar_t shader_type;
	unsigned texture_slot;

	// TODO: Ability to override value until:
	// a) From now on
	// b) Single draw call only
	// c) Until end of this frame (e.g. mark when post processing starts)
	// d) Until end of next frame (e.g. for scene detection)
	// Since the duration of the convergence and separation settings are
	// not currently consistent between [ShaderOverride] and [Key] sections
	// we could also make this apply to them to make it consistent, but
	// still allow for the existing behaviour for the fixes that depend on
	// it (like DG2).

	ParamOverride() :
		param_idx(-1),
		param_component(NULL),
		type(ParamOverrideType::INVALID),
		val(FLT_MAX),
		shader_type(NULL),
		texture_slot(INT_MAX)
	{}

	void run(HackerContext*, ID3D11Device*, ID3D11DeviceContext*, ShaderOverrideState*) override;
};

enum class ResourceCopyTargetType {
	INVALID,
	EMPTY,
	CONSTANT_BUFFER,
	SHADER_RESOURCE,
	// TODO: SAMPLER, // Not really a resource, but might still be useful
	VERTEX_BUFFER,
	INDEX_BUFFER,
	STREAM_OUTPUT,
	RENDER_TARGET,
	DEPTH_STENCIL_TARGET,
	// TODO: UNORDERED_ACCESS_VIEW,
	// TODO: CUSTOM_RESOURCE,
};

class ResourceCopyTarget {
public:
	ResourceCopyTargetType type;
	wchar_t shader_type;
	unsigned slot;

	ResourceCopyTarget() :
		type(ResourceCopyTargetType::INVALID),
		shader_type(L'\0'),
		slot(0)
	{}

	bool ParseTarget(const wchar_t *target, bool allow_null);
	ID3D11Resource *GetResource(
			ID3D11DeviceContext *mOrigContext,
			ID3D11View **view,
			UINT *stride,
			UINT *offset,
			DXGI_FORMAT *format);
	void SetResource(
			ID3D11DeviceContext *mOrigContext,
			ID3D11Resource *res,
			ID3D11View *view,
			UINT stride,
			UINT offset,
			DXGI_FORMAT format);
	D3D11_BIND_FLAG BindFlags();
};

enum class ResourceCopyOptions {
	INVALID         = 0,
	COPY            = 0x00000001,
	REFERENCE       = 0x00000002,
	COPY_TYPE_MASK  = 0x00000003,
	UNLESS_NULL     = 0x00000004,
};
SENSIBLE_ENUM(ResourceCopyOptions);
static EnumName_t<wchar_t *, ResourceCopyOptions> ResourceCopyOptionNames[] = {
	{L"copy", ResourceCopyOptions::COPY},
	{L"ref", ResourceCopyOptions::REFERENCE},
	{L"reference", ResourceCopyOptions::REFERENCE},
	{L"unless_null", ResourceCopyOptions::UNLESS_NULL},
	{NULL, ResourceCopyOptions::INVALID} // End of list marker
};
// TODO: Add support for more behaviour modifiers, here's a few ideas
// off the top of my head - I don't intend to implement all these
// unless we have a proven need for them or maybe if they are trivial
// and have real potential to be useful later. For now they are just
// food for thought:
//
// stereo - Set driver heuristic when creating a resource for a copy operation
// mono - Set driver heuristic when creating a resource for a copy operation
// reverse_stereo_blit - Use reverse stereo blit to copy both left + right eyes into a single resource
// res_format= - override DXGI Format when creating a resource
// view_format= - override DXGI Format when creating a view
// restore - back up whatever resource was previously assigned and restore it afterwards
// persist - leave resource assigned after the draw call
// once_per_frame - only perform this particular operation at most once per frame
// if_dest_is_null - only perform the operation if the destination is not currently assigned
// if_dest_is_compatible - only perform the operation if the destination exists, and is compatible with the source
// if_dest_is_null_or_incompatible - only perform the operation if the destination is not currently assigned, or is incompatible
// copy_subresource_region=... - Use copy_subresource_region instead of copy_resource
// mip_map, array, etc. - create a view that exposes only part of the resource
// overwrite - instead of creating a new resource for a copy operation, overwrite the resource already assigned to the destination (if it exists and is compatible)


class ResourceCopyOperation : public ShaderOverrideCommand {
public:
	ResourceCopyTarget src;
	ResourceCopyTarget dst;
	ResourceCopyOptions options;

	ID3D11Resource *cached_resource;
	ID3D11View *cached_view;

	ResourceCopyOperation();
	~ResourceCopyOperation();

	void run(HackerContext*, ID3D11Device*, ID3D11DeviceContext*, ShaderOverrideState*) override;
};


void RunShaderOverrideCommandList(HackerDevice *mHackerDevice,
		HackerContext *mHackerContext,
		ShaderOverrideCommandList *command_list);

bool ParseShaderOverrideIniParamOverride(wstring *key, wstring *val,
		ShaderOverrideCommandList *command_list);
bool ParseShaderOverrideResourceCopyDirective(wstring *key, wstring *val,
		ShaderOverrideCommandList *command_list);
