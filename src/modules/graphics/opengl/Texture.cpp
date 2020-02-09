/**
 * Copyright (c) 2006-2020 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

#include "Texture.h"

#include "graphics/Graphics.h"
#include "Graphics.h"
#include "common/int.h"

// STD
#include <algorithm> // for min/max

namespace love
{
namespace graphics
{
namespace opengl
{

static GLenum createFBO(GLuint &framebuffer, TextureType texType, PixelFormat format, GLuint texture, int layers, bool clear)
{
	// get currently bound fbo to reset to it later
	GLuint current_fbo = gl.getFramebuffer(OpenGL::FRAMEBUFFER_ALL);

	glGenFramebuffers(1, &framebuffer);
	gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, framebuffer);

	if (texture != 0)
	{
		if (isPixelFormatDepthStencil(format) && (GLAD_ES_VERSION_3_0 || !GLAD_ES_VERSION_2_0))
		{
			// glDrawBuffers is an ext in GL2. glDrawBuffer doesn't exist in ES3.
			GLenum none = GL_NONE;
			if (GLAD_ES_VERSION_3_0)
				glDrawBuffers(1, &none);
			else
				glDrawBuffer(GL_NONE);
			glReadBuffer(GL_NONE);
		}

		bool unusedSRGB = false;
		OpenGL::TextureFormat fmt = OpenGL::convertPixelFormat(format, false, unusedSRGB);

		int faces = texType == TEXTURE_CUBE ? 6 : 1;

		// Make sure all faces and layers of the texture are initialized to
		// transparent black. This is unfortunately probably pretty slow for
		// 2D-array and 3D textures with a lot of layers...
		for (int layer = layers - 1; layer >= 0; layer--)
		{
			for (int face = faces - 1; face >= 0; face--)
			{
				for (GLenum attachment : fmt.framebufferAttachments)
				{
					if (attachment == GL_NONE)
						continue;

					gl.framebufferTexture(attachment, texType, texture, 0, layer, face);
				}

				if (clear)
				{
					if (isPixelFormatDepthStencil(format))
					{
						bool hadDepthWrites = gl.hasDepthWrites();
						if (!hadDepthWrites) // glDepthMask also affects glClear.
							gl.setDepthWrites(true);

						gl.clearDepth(1.0);
						glClearStencil(0);
						glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

						if (!hadDepthWrites)
							gl.setDepthWrites(hadDepthWrites);
					}
					else
					{
						glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
						glClear(GL_COLOR_BUFFER_BIT);
					}
				}
			}
		}
	}

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, current_fbo);
	return status;
}

static bool newRenderbuffer(int width, int height, int &samples, PixelFormat pixelformat, GLuint &buffer)
{
	int reqsamples = samples;
	bool unusedSRGB = false;
	OpenGL::TextureFormat fmt = OpenGL::convertPixelFormat(pixelformat, true, unusedSRGB);

	GLuint current_fbo = gl.getFramebuffer(OpenGL::FRAMEBUFFER_ALL);

	// Temporary FBO used to clear the renderbuffer.
	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, fbo);

	if (isPixelFormatDepthStencil(pixelformat) && (GLAD_ES_VERSION_3_0 || !GLAD_ES_VERSION_2_0))
	{
		// glDrawBuffers is an ext in GL2. glDrawBuffer doesn't exist in ES3.
		GLenum none = GL_NONE;
		if (GLAD_ES_VERSION_3_0)
			glDrawBuffers(1, &none);
		else
			glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	}

	glGenRenderbuffers(1, &buffer);
	glBindRenderbuffer(GL_RENDERBUFFER, buffer);

	if (samples > 1)
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, fmt.internalformat, width, height);
	else
		glRenderbufferStorage(GL_RENDERBUFFER, fmt.internalformat, width, height);

	for (GLenum attachment : fmt.framebufferAttachments)
	{
		if (attachment != GL_NONE)
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, buffer);
	}

	if (samples > 1)
		glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);

	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (status == GL_FRAMEBUFFER_COMPLETE && (reqsamples <= 1 || samples > 1))
	{
		if (isPixelFormatDepthStencil(pixelformat))
		{
			bool hadDepthWrites = gl.hasDepthWrites();
			if (!hadDepthWrites) // glDepthMask also affects glClear.
				gl.setDepthWrites(true);

			gl.clearDepth(1.0);
			glClearStencil(0);
			glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

			if (!hadDepthWrites)
				gl.setDepthWrites(hadDepthWrites);
		}
		else
		{
			// Initialize the buffer to transparent black.
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
	}
	else
	{
		glDeleteRenderbuffers(1, &buffer);
		buffer = 0;
		samples = 1;
	}

	gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, current_fbo);
	gl.deleteFramebuffer(fbo);

	return status == GL_FRAMEBUFFER_COMPLETE;
}

Texture::Texture(const Settings &settings, const Slices *data)
	: love::graphics::Texture(settings, data)
	, slices(settings.type)
	, fbo(0)
	, texture(0)
	, renderbuffer(0)
	, framebufferStatus(GL_FRAMEBUFFER_COMPLETE)
	, actualSamples(1)
{
	if (data != nullptr)
		slices = *data;
	loadVolatile();
}

Texture::~Texture()
{
	unloadVolatile();
}

bool Texture::createTexture()
{
	// The base class handles some validation. For example, if ImageData is
	// given then it must exist for all mip levels, a render target can't use
	// a compressed format, etc.

	glGenTextures(1, &texture);
	gl.bindTextureToUnit(this, 0, false);

	// Use a default texture if the size is too big for the system.
	// validateDimensions is also called in the base class for RTs and
	// non-readable textures.
	if (!renderTarget && !validateDimensions(false))
	{
		usingDefaultTexture = true;

		setSamplerState(samplerState);

		bool isSRGB = false;
		gl.rawTexStorage(texType, 1, PIXELFORMAT_RGBA8_UNORM, isSRGB, 2, 2, 1);

		// A nice friendly checkerboard to signify invalid textures...
		GLubyte px[] = {0xFF,0xFF,0xFF,0xFF, 0xFF,0xA0,0xA0,0xFF,
						0xFF,0xA0,0xA0,0xFF, 0xFF,0xFF,0xFF,0xFF};

		int slices = texType == TEXTURE_CUBE ? 6 : 1;
		Rect rect = {0, 0, 2, 2};
		for (int slice = 0; slice < slices; slice++)
			uploadByteData(PIXELFORMAT_RGBA8_UNORM, px, sizeof(px), 0, slice, rect, nullptr);

		return true;
	}

	GLenum gltype = OpenGL::getGLTextureType(texType);
	if (renderTarget && GLAD_ANGLE_texture_usage)
		glTexParameteri(gltype, GL_TEXTURE_USAGE_ANGLE, GL_FRAMEBUFFER_ATTACHMENT_ANGLE);

	setSamplerState(samplerState);

	int mipcount = getMipmapCount();
	int slicecount = 1;

	if (texType == TEXTURE_VOLUME)
		slicecount = getDepth();
	else if (texType == TEXTURE_2D_ARRAY)
		slicecount = getLayerCount();
	else if (texType == TEXTURE_CUBE)
		slicecount = 6;

	if (!isCompressed())
		gl.rawTexStorage(texType, mipcount, format, sRGB, pixelWidth, pixelHeight, texType == TEXTURE_VOLUME ? depth : layers);

	int w = pixelWidth;
	int h = pixelHeight;
	int d = depth;

	OpenGL::TextureFormat fmt = gl.convertPixelFormat(format, false, sRGB);

	for (int mip = 0; mip < mipcount; mip++)
	{
		if (isCompressed() && (texType == TEXTURE_2D_ARRAY || texType == TEXTURE_VOLUME))
		{
			size_t mipsize = 0;

			if (texType == TEXTURE_2D_ARRAY || texType == TEXTURE_VOLUME)
			{
				for (int slice = 0; slice < slices.getSliceCount(mip); slice++)
				{
					auto id = slices.get(slice, mip);
					if (id != nullptr)
						mipsize += id->getSize();
				}
			}

			if (mipsize > 0)
				glCompressedTexImage3D(gltype, mip, fmt.internalformat, w, h, d, 0, mipsize, nullptr);
		}

		for (int slice = 0; slice < slicecount; slice++)
		{
			love::image::ImageDataBase *id = slices.get(slice, mip);
			if (id != nullptr)
				uploadImageData(id, mip, slice, 0, 0);
		}

		w = std::max(w / 2, 1);
		h = std::max(h / 2, 1);

		if (texType == TEXTURE_VOLUME)
			d = std::max(d / 2, 1);
	}

	bool hasdata = slices.get(0, 0) != nullptr;

	// Create a local FBO used for glReadPixels as well as MSAA blitting.
	if (isRenderTarget())
	{
		bool clear = !hasdata;
		int slices = texType == TEXTURE_VOLUME ? depth : layers;
		framebufferStatus = createFBO(fbo, texType, format, texture, slices, clear);
	}
	else if (!hasdata)
	{
		// Initialize all slices to transparent black.
		std::vector<uint8> emptydata(getPixelFormatSliceSize(format, w, h));

		Rect r = {0, 0, w, h};
		int slices = texType == TEXTURE_VOLUME ? depth : layers;
		slices = texType == TEXTURE_CUBE ? 6 : slices;
		for (int i = 0; i < slices; i++)
			uploadByteData(format, emptydata.data(), emptydata.size(), 0, i, r);
	}

	// Non-readable textures can't have mipmaps (enforced in the base class),
	// so generateMipmaps here is fine - when they aren't already initialized.
	if (getMipmapCount() > 1 && slices.getMipmapCount() <= 1)
		generateMipmaps();

	return true;
}

bool Texture::createRenderbuffer()
{
	if (isReadable() && actualSamples <= 1)
		return true;

	return newRenderbuffer(pixelWidth, pixelHeight, actualSamples, format, renderbuffer);
}

bool Texture::loadVolatile()
{
	if (texture != 0 || renderbuffer != 0)
		return true;

	OpenGL::TempDebugGroup debuggroup("Texture load");

	// NPOT textures don't support mipmapping without full NPOT support.
	if ((GLAD_ES_VERSION_2_0 && !(GLAD_ES_VERSION_3_0 || GLAD_OES_texture_npot))
		&& (pixelWidth != nextP2(pixelWidth) || pixelHeight != nextP2(pixelHeight)))
	{
		mipmapCount = 1;
		samplerState.mipmapFilter = SamplerState::MIPMAP_FILTER_NONE;
	}

	actualSamples = std::max(1, std::min(getRequestedMSAA(), gl.getMaxSamples()));

	while (glGetError() != GL_NO_ERROR); // Clear errors.

	try
	{
		if (isReadable())
			createTexture();
		if (!isReadable() || actualSamples > 1)
			createRenderbuffer();

		GLenum glerr = glGetError();
		if (glerr != GL_NO_ERROR)
			throw love::Exception("Cannot create texture (OpenGL error: %s)", OpenGL::errorString(glerr));
	}
	catch (love::Exception &)
	{
		unloadVolatile();
		throw;
	}

	int64 memsize = 0;

	for (int mip = 0; mip < getMipmapCount(); mip++)
	{
		int w = getPixelWidth(mip);
		int h = getPixelHeight(mip);
		int slices = getDepth(mip) * layers * (texType == TEXTURE_CUBE ? 6 : 1);
		memsize += getPixelFormatSliceSize(format, w, h) * slices;
	}

	if (actualSamples > 1 && isReadable())
	{
		int slices = depth * layers * (texType == TEXTURE_CUBE ? 6 : 1);
		memsize += getPixelFormatSliceSize(format, pixelWidth, pixelHeight) * slices * actualSamples;
	}
	else if (actualSamples > 1)
		memsize *= actualSamples;

	setGraphicsMemorySize(memsize);

	usingDefaultTexture = false;
	return true;
}

void Texture::unloadVolatile()
{
	if (isRenderTarget() && (fbo != 0 || renderbuffer != 0 || texture != 0))
	{
		// This is a bit ugly, but we need some way to destroy the cached FBO
		// when this texture's texture is destroyed.
		auto gfx = Module::getInstance<Graphics>(Module::M_GRAPHICS);
		if (gfx != nullptr)
			gfx->cleanupRenderTexture(this);
	}

	if (fbo != 0)
		gl.deleteFramebuffer(fbo);

	if (renderbuffer != 0)
		glDeleteRenderbuffers(1, &renderbuffer);

	if (texture != 0)
		gl.deleteTexture(texture);

	fbo = 0;
	renderbuffer = 0;
	texture = 0;

	setGraphicsMemorySize(0);
}

void Texture::uploadByteData(PixelFormat pixelformat, const void *data, size_t size, int level, int slice, const Rect &r, love::image::ImageDataBase *imgd)
{
	love::image::ImageDataBase *oldd = slices.get(slice, level);

	// We can only replace the internal Data (used when reloading due to setMode)
	// if the dimensions match.
	if (imgd != nullptr && oldd != nullptr && oldd->getWidth() == imgd->getWidth()
		&& oldd->getHeight() == imgd->getHeight())
	{
		slices.set(slice, level, imgd);
	}

	OpenGL::TempDebugGroup debuggroup("Texture data upload");

	gl.bindTextureToUnit(this, 0, false);

	OpenGL::TextureFormat fmt = OpenGL::convertPixelFormat(pixelformat, false, sRGB);
	GLenum gltarget = OpenGL::getGLTextureType(texType);

	if (texType == TEXTURE_CUBE)
		gltarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + slice;

	if (isPixelFormatCompressed(pixelformat))
	{
		if (r.x != 0 || r.y != 0)
			throw love::Exception("x and y parameters must be 0 for compressed textures.");

		if (texType == TEXTURE_2D || texType == TEXTURE_CUBE)
			glCompressedTexImage2D(gltarget, level, fmt.internalformat, r.w, r.h, 0, size, data);
		else if (texType == TEXTURE_2D_ARRAY || texType == TEXTURE_VOLUME)
			glCompressedTexSubImage3D(gltarget, level, 0, 0, slice, r.w, r.h, 1, fmt.internalformat, size, data);
	}
	else
	{
		if (texType == TEXTURE_2D || texType == TEXTURE_CUBE)
			glTexSubImage2D(gltarget, level, r.x, r.y, r.w, r.h, fmt.externalformat, fmt.type, data);
		else if (texType == TEXTURE_2D_ARRAY || texType == TEXTURE_VOLUME)
			glTexSubImage3D(gltarget, level, r.x, r.y, slice, r.w, r.h, 1, fmt.externalformat, fmt.type, data);
	}
}

void Texture::generateMipmaps()
{
	if (getMipmapCount() == 1 || getMipmapsMode() == MIPMAPS_NONE)
		throw love::Exception("generateMipmaps can only be called on a Texture which was created with mipmaps enabled.");

	if (isPixelFormatCompressed(format))
		throw love::Exception("generateMipmaps cannot be called on a compressed Texture.");

	gl.bindTextureToUnit(this, 0, false);

	GLenum gltextype = OpenGL::getGLTextureType(texType);

	if (gl.bugs.generateMipmapsRequiresTexture2DEnable)
		glEnable(gltextype);

	glGenerateMipmap(gltextype);
}

love::image::ImageData *Texture::newImageData(love::image::Image *module, int slice, int mipmap, const Rect &r)
{
	// Base class does validation (only RTs allowed, etc) and creates ImageData.
	love::image::ImageData *data = love::graphics::Texture::newImageData(module, slice, mipmap, r);

	if (fbo == 0) // Should never be reached.
		return data;

	bool isSRGB = false;
	OpenGL::TextureFormat fmt = gl.convertPixelFormat(data->getFormat(), false, isSRGB);

	GLuint current_fbo = gl.getFramebuffer(OpenGL::FRAMEBUFFER_ALL);
	gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, getFBO());

	if (slice > 0 || mipmap > 0)
	{
		int layer = texType == TEXTURE_CUBE ? 0 : slice;
		int face = texType == TEXTURE_CUBE ? slice : 0;
		gl.framebufferTexture(GL_COLOR_ATTACHMENT0, texType, texture, mipmap, layer, face);
	}

	glReadPixels(r.x, r.y, r.w, r.h, fmt.externalformat, fmt.type, data->getData());

	if (slice > 0 || mipmap > 0)
		gl.framebufferTexture(GL_COLOR_ATTACHMENT0, texType, texture, 0, 0, 0);

	gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, current_fbo);

	return data;
}

void Texture::setSamplerState(const SamplerState &s)
{
	if (samplerState.depthSampleMode.hasValue && !gl.isDepthCompareSampleSupported())
		throw love::Exception("Depth comparison sampling in shaders is not supported on this system.");

	// Base class does common validation and assigns samplerState.
	love::graphics::Texture::setSamplerState(s);

	if (!OpenGL::hasTextureFilteringSupport(getPixelFormat()))
	{
		samplerState.magFilter = samplerState.minFilter = SamplerState::FILTER_NEAREST;

		if (samplerState.mipmapFilter == SamplerState::MIPMAP_FILTER_LINEAR)
			samplerState.mipmapFilter = SamplerState::MIPMAP_FILTER_NEAREST;
	}

	// We don't want filtering or (attempted) mipmaps on the default texture.
	if (usingDefaultTexture)
	{
		samplerState.mipmapFilter = SamplerState::MIPMAP_FILTER_NONE;
		samplerState.minFilter = samplerState.magFilter = SamplerState::FILTER_NEAREST;
	}

	// If we only have limited NPOT support then the wrap mode must be CLAMP.
	if ((GLAD_ES_VERSION_2_0 && !(GLAD_ES_VERSION_3_0 || GLAD_OES_texture_npot))
		&& (pixelWidth != nextP2(pixelWidth) || pixelHeight != nextP2(pixelHeight) || depth != nextP2(depth)))
	{
		samplerState.wrapU = samplerState.wrapV = samplerState.wrapW = SamplerState::WRAP_CLAMP;
	}

	gl.bindTextureToUnit(this, 0, false);
	gl.setSamplerState(texType, samplerState);
}

ptrdiff_t Texture::getHandle() const
{
	return texture;
}

ptrdiff_t Texture::getRenderTargetHandle() const
{
	return renderTarget ? (renderbuffer != 0 ? renderbuffer : texture) : 0;
}

} // opengl
} // graphics
} // love