#include "openvr_runtime_d3d11.hpp"

#include "dll_log.hpp"

namespace reshade::openvr
{

	openvr_runtime_d3d11::openvr_runtime_d3d11(const vr::Texture_t *pTexture, const vr::VRTextureBounds_t *pBounds, submit_function orig_submit, void *orig_compositor) :
		_orig_submit(orig_submit), _orig_compositor(orig_compositor), _num_submitted(0)
	{
		ID3D11Texture2D *texture = static_cast<ID3D11Texture2D *>(pTexture->handle);
		texture->GetDevice(&_device);
		_device->GetImmediateContext(&_context);

		D3D11_TEXTURE2D_DESC td;
		texture->GetDesc(&td);
		// derive our backing texture size from the submitted texture and bounds, then use double width to fit both eyes
		_width = 2 * td.Width * (pBounds->uMax - pBounds->uMin);
		_height = td.Height * (pBounds->vMax - pBounds->vMin);

		_state.reset(new d3d11::state_tracking_context(_context.get()));
		_runtime.reset(new d3d11::runtime_d3d11(_device.get(), _width, _height, td.Format, _state.get()));

		LOG(DEBUG) << "Created OpenVR D3D11 runtime";
	}

	bool openvr_runtime_d3d11::on_submit( vr::EVREye eEye, const vr::Texture_t *pTexture, const vr::VRTextureBounds_t *pBounds )
	{
		assert(pTexture->eType == vr::TextureType_DirectX);

		ID3D11Texture2D *texture = static_cast<ID3D11Texture2D*>(pTexture->handle);
		ID3D11Texture2D *target = _runtime->get_backbuffer();
		D3D11_TEXTURE2D_DESC td;
		texture->GetDesc(&td);
		D3D11_BOX region;
		region.left = td.Width * pBounds->uMin;
		region.right = td.Width * pBounds->uMax;
		region.top = td.Height * pBounds->vMin;
		region.bottom = td.Height * pBounds->vMax;
		region.front = 0;
		region.back = 1;
		_context->CopySubresourceRegion(target, 0, eEye == vr::Eye_Left ? 0 : _width/2, 0, 0, texture, 0, &region);

		// TODO: handle resize if necessary

		if (++_num_submitted == 2)
		{
			// both eyes submitted, apply effect chain and submit to original compositor
			_runtime->on_submit_vr();

			vr::Texture_t tex;
			tex.eType = vr::TextureType_DirectX;
			tex.eColorSpace = vr::ColorSpace_Auto; // FIXME what should we put here?
			tex.handle = target;
			vr::VRTextureBounds_t left_eye_bounds, right_eye_bounds;
			left_eye_bounds.uMin = left_eye_bounds.vMin = right_eye_bounds.vMin = 0;
			left_eye_bounds.uMax = right_eye_bounds.uMin = .5f;
			left_eye_bounds.vMax = right_eye_bounds.uMax = right_eye_bounds.vMax = 1;

			_orig_submit(_orig_compositor, vr::Eye_Left, &tex, &left_eye_bounds, vr::Submit_Default);
			_orig_submit(_orig_compositor, vr::Eye_Right, &tex, &right_eye_bounds, vr::Submit_Default);

			_num_submitted = 0;
		}		

		return true;
	}
}
