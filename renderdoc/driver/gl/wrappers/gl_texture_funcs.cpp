/******************************************************************************
 * The MIT License (MIT)
 * 
 * Copyright (c) 2014 Crytek
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "common/common.h"
#include "common/string_utils.h"
#include "../gl_driver.h"

bool WrappedOpenGL::Serialise_glGenTextures(GLsizei n, GLuint* textures)
{
	SERIALISE_ELEMENT(ResourceId, id, GetResourceManager()->GetID(TextureRes(*textures)));

	if(m_State == READING)
	{
		GLuint real = 0;
		m_Real.glGenTextures(1, &real);
		
		GLResource res = TextureRes(real);

		ResourceId live = m_ResourceManager->RegisterResource(res);
		GetResourceManager()->AddLiveResource(id, res);

		m_Textures[live].resource = res;
		m_Textures[live].curType = eGL_UNKNOWN_ENUM;
	}

	return true;
}

void WrappedOpenGL::glGenTextures(GLsizei n, GLuint* textures)
{
	m_Real.glGenTextures(n, textures);

	for(GLsizei i=0; i < n; i++)
	{
		GLResource res = TextureRes(textures[i]);
		ResourceId id = GetResourceManager()->RegisterResource(res);

		if(m_State >= WRITING)
		{
			Chunk *chunk = NULL;

			{
				SCOPED_SERIALISE_CONTEXT(GEN_TEXTURE);
				Serialise_glGenTextures(1, textures+i);

				chunk = scope.Get();
			}

			GLResourceRecord *record = GetResourceManager()->AddResourceRecord(id);
			RDCASSERT(record);

			record->AddChunk(chunk);
		}
		else
		{
			GetResourceManager()->AddLiveResource(id, res);
			m_Textures[id].resource = res;
			m_Textures[id].curType = eGL_UNKNOWN_ENUM;
		}
	}
}

void WrappedOpenGL::glDeleteTextures(GLsizei n, const GLuint *textures)
{
	m_Real.glDeleteTextures(n, textures);

	for(GLsizei i=0; i < n; i++)
		GetResourceManager()->UnregisterResource(TextureRes(textures[i]));
}

bool WrappedOpenGL::Serialise_glBindTexture(GLenum target, GLuint texture)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(ResourceId, Id, GetResourceManager()->GetID(TextureRes(texture)));
	
	if(m_State == WRITING_IDLE)
	{
		m_TextureRecord[m_TextureUnit]->datatype = Target;
	}
	else if(m_State < WRITING)
	{
		if(Id == ResourceId())
		{
			m_Real.glBindTexture(Target, 0);
		}
		else
		{
			GLResource res = GetResourceManager()->GetLiveResource(Id);
			m_Real.glBindTexture(Target, res.name);

			m_Textures[GetResourceManager()->GetLiveID(Id)].curType = Target;
		}
	}

	return true;
}

void WrappedOpenGL::glBindTexture(GLenum target, GLuint texture)
{
	m_Real.glBindTexture(target, texture);
	
	if(m_State == WRITING_CAPFRAME)
	{
		Chunk *chunk = NULL;

		{
			SCOPED_SERIALISE_CONTEXT(BIND_TEXTURE);
			Serialise_glBindTexture(target, texture);

			chunk = scope.Get();
		}
		
		m_ContextRecord->AddChunk(chunk);
	}
	else if(m_State < WRITING)
	{
		m_Textures[GetResourceManager()->GetID(TextureRes(texture))].curType = target;
	}

	if(texture == 0)
	{
		m_TextureRecord[m_TextureUnit] = NULL;
		return;
	}

	if(m_State >= WRITING)
	{
		GLResourceRecord *r = m_TextureRecord[m_TextureUnit] = GetResourceManager()->GetResourceRecord(TextureRes(texture));

		if(r->datatype)
		{
			// it's illegal to retype a texture
			RDCASSERT(r->datatype == target);
		}
		else
		{
			Chunk *chunk = NULL;

			{
				SCOPED_SERIALISE_CONTEXT(BIND_TEXTURE);
				Serialise_glBindTexture(target, texture);

				chunk = scope.Get();
			}

			r->AddChunk(chunk);
		}
	}
}


bool WrappedOpenGL::Serialise_glGenerateMipmap(GLenum target)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());

	if(m_State == READING)
	{
		m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		m_Real.glGenerateMipmap(Target);
	}

	return true;
}

void WrappedOpenGL::glGenerateMipmap(GLenum target)
{
	m_Real.glGenerateMipmap(target);
	
	RDCASSERT(m_TextureRecord[m_TextureUnit]);
	{
		SCOPED_SERIALISE_CONTEXT(GENERATE_MIPMAP);
		Serialise_glGenerateMipmap(target);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(GLenum, PName, pname);
	SERIALISE_ELEMENT(int32_t, Param, param);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());
	
	if(m_State < WRITING)
	{
		if(m_State == READING)
			m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		glTexParameteri(Target, PName, Param);
	}

	return true;
}

void WrappedOpenGL::glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	m_Real.glTexParameteri(target, pname, param);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXPARAMETERI);
		Serialise_glTexParameteri(target, pname, param);

		if(m_State == WRITING_IDLE)
			m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
		else
			m_ContextRecord->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(GLenum, PName, pname);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());
	const size_t nParams = (PName == eGL_TEXTURE_BORDER_COLOR ? 4U : 1U);
	SERIALISE_ELEMENT_ARR(int32_t, Params, params, nParams);

	if(m_State < WRITING)
	{
		if(m_State == READING)
			m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		glTexParameteriv(Target, PName, Params);
	}

	delete[] Params;

	return true;
}

void WrappedOpenGL::glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
	m_Real.glTexParameteriv(target, pname, params);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXPARAMETERIV);
		Serialise_glTexParameteriv(target, pname, params);

		if(m_State == WRITING_IDLE)
			m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
		else
			m_ContextRecord->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(GLenum, PName, pname);
	SERIALISE_ELEMENT(float, Param, param);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());
	
	if(m_State < WRITING)
	{
		if(m_State == READING)
			m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		glTexParameterf(Target, PName, Param);
	}

	return true;
}

void WrappedOpenGL::glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	m_Real.glTexParameterf(target, pname, param);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXPARAMETERF);
		Serialise_glTexParameterf(target, pname, param);

		if(m_State == WRITING_IDLE)
			m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
		else
			m_ContextRecord->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(GLenum, PName, pname);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());
	const size_t nParams = (PName == eGL_TEXTURE_BORDER_COLOR ? 4U : 1U);
	SERIALISE_ELEMENT_ARR(float, Params, params, nParams);

	if(m_State < WRITING)
	{
		if(m_State == READING)
			m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		glTexParameterfv(Target, PName, Params);
	}

	delete[] Params;

	return true;
}

void WrappedOpenGL::glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
	m_Real.glTexParameterfv(target, pname, params);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXPARAMETERFV);
		Serialise_glTexParameterfv(target, pname, params);

		if(m_State == WRITING_IDLE)
			m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
		else
			m_ContextRecord->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glPixelStorei(GLenum pname, GLint param)
{
	SERIALISE_ELEMENT(GLenum, PName, pname);
	SERIALISE_ELEMENT(int32_t, Param, param);

	if(m_State < WRITING)
		m_Real.glPixelStorei(PName, Param);

	return true;
}

void WrappedOpenGL::glPixelStorei(GLenum pname, GLint param)
{
	m_Real.glPixelStorei(pname, param);

	RDCASSERT(m_TextureRecord[m_TextureUnit]);
	{
		SCOPED_SERIALISE_CONTEXT(PIXELSTORE);
		Serialise_glPixelStorei(pname, param);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

void WrappedOpenGL::glPixelStoref(GLenum pname, GLfloat param)
{
	glPixelStorei(pname, (GLint)param);
}

bool WrappedOpenGL::Serialise_glActiveTexture(GLenum texture)
{
	SERIALISE_ELEMENT(GLenum, Texture, texture);

	if(m_State < WRITING)
		m_Real.glActiveTexture(Texture);

	return true;
}

void WrappedOpenGL::glActiveTexture(GLenum texture)
{
	m_Real.glActiveTexture(texture);

	m_TextureUnit = texture-eGL_TEXTURE0;
	
	if(m_State == WRITING_CAPFRAME)
	{
		Chunk *chunk = NULL;

		{
			SCOPED_SERIALISE_CONTEXT(ACTIVE_TEXTURE);
			Serialise_glActiveTexture(texture);

			chunk = scope.Get();
		}
		
		m_ContextRecord->AddChunk(chunk);
	}
}

#pragma region Texture Storage/Upload

void WrappedOpenGL::glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	m_Real.glTexImage1D(target, level, internalformat, width, border, format, type, pixels);

	RDCUNIMPLEMENTED("Old glTexImage1D API not implemented");
}

void WrappedOpenGL::glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid * pixels)
{
	m_Real.glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);

	RDCUNIMPLEMENTED("Old glTexImage2D API not implemented");
}

void WrappedOpenGL::glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid * pixels)
{
	m_Real.glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels);

	RDCUNIMPLEMENTED("Old glTexImage3D API not implemented");
}

void WrappedOpenGL::glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const GLvoid *pixels)
{
	m_Real.glCompressedTexImage1D(target, level, internalformat, width, border, imageSize, pixels);

	RDCUNIMPLEMENTED("Old glCompressedTexImage1D API not implemented");
}

void WrappedOpenGL::glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid * pixels)
{
	m_Real.glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, pixels);

	RDCUNIMPLEMENTED("Old glCompressedTexImage2D API not implemented");
}

void WrappedOpenGL::glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid * pixels)
{
	m_Real.glCompressedTexImage3D(target, level, internalformat, width, height, depth, border, imageSize, pixels);

	RDCUNIMPLEMENTED("Old glCompressedTexImage3D API not implemented");
}

bool WrappedOpenGL::Serialise_glTexStorage1D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(uint32_t, Levels, levels);
	SERIALISE_ELEMENT(GLenum, Format, internalformat);
	SERIALISE_ELEMENT(uint32_t, Width, width);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());

	if(m_State == READING)
	{
		ResourceId liveId = GetResourceManager()->GetLiveID(id);
		m_Textures[liveId].width = Width;
		m_Textures[liveId].height = 1;
		m_Textures[liveId].depth = 1;

		m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		m_Real.glTexStorage1D(Target, Levels, Format, Width);
	}

	return true;
}

void WrappedOpenGL::glTexStorage1D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width)
{
	m_Real.glTexStorage1D(target, levels, internalformat, width);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXSTORAGE1D);
		Serialise_glTexStorage1D(target, levels, internalformat, width);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(uint32_t, Levels, levels);
	SERIALISE_ELEMENT(GLenum, Format, internalformat);
	SERIALISE_ELEMENT(uint32_t, Width, width);
	SERIALISE_ELEMENT(uint32_t, Height, height);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());

	if(m_State == READING)
	{
		ResourceId liveId = GetResourceManager()->GetLiveID(id);
		m_Textures[liveId].width = Width;
		m_Textures[liveId].height = Height;
		m_Textures[liveId].depth = 1;

		m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		m_Real.glTexStorage2D(Target, Levels, Format, Width, Height);
	}

	return true;
}

void WrappedOpenGL::glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
{
	m_Real.glTexStorage2D(target, levels, internalformat, width, height);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXSTORAGE2D);
		Serialise_glTexStorage2D(target, levels, internalformat, width, height);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(uint32_t, Levels, levels);
	SERIALISE_ELEMENT(GLenum, Format, internalformat);
	SERIALISE_ELEMENT(uint32_t, Width, width);
	SERIALISE_ELEMENT(uint32_t, Height, height);
	SERIALISE_ELEMENT(uint32_t, Depth, depth);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());

	if(m_State == READING)
	{
		ResourceId liveId = GetResourceManager()->GetLiveID(id);
		m_Textures[liveId].width = Width;
		m_Textures[liveId].height = Height;
		m_Textures[liveId].depth = Depth;

		m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		m_Real.glTexStorage3D(Target, Levels, Format, Width, Height, Depth);
	}

	return true;
}

void WrappedOpenGL::glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth)
{
	m_Real.glTexStorage3D(target, levels, internalformat, width, height, depth);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXSTORAGE3D);
		Serialise_glTexStorage3D(target, levels, internalformat, width, height, depth);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void *pixels)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(int32_t, Level, level);
	SERIALISE_ELEMENT(int32_t, xoff, xoffset);
	SERIALISE_ELEMENT(uint32_t, Width, width);
	SERIALISE_ELEMENT(GLenum, Format, format);
	SERIALISE_ELEMENT(GLenum, Type, type);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());

	GLint align = 1;
	m_Real.glGetIntegerv(eGL_UNPACK_ALIGNMENT, &align);

	size_t subimageSize = GetByteSize(Width, 1, 1, Format, Type, Level, align);

	SERIALISE_ELEMENT_BUF(byte *, buf, pixels, subimageSize);
	
	if(m_State == READING)
	{
		m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		m_Real.glTexSubImage1D(Target, Level, xoff, Width, Format, Type, buf);

		delete[] buf;
	}

	return true;
}

void WrappedOpenGL::glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void *pixels)
{
	m_Real.glTexSubImage1D(target, level, xoffset, width, format, type, pixels);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXSUBIMAGE1D);
		Serialise_glTexSubImage1D(target, level, xoffset, width, format, type, pixels);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(int32_t, Level, level);
	SERIALISE_ELEMENT(int32_t, xoff, xoffset);
	SERIALISE_ELEMENT(int32_t, yoff, yoffset);
	SERIALISE_ELEMENT(uint32_t, Width, width);
	SERIALISE_ELEMENT(uint32_t, Height, height);
	SERIALISE_ELEMENT(GLenum, Format, format);
	SERIALISE_ELEMENT(GLenum, Type, type);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());

	GLint align = 1;
	m_Real.glGetIntegerv(eGL_UNPACK_ALIGNMENT, &align);

	size_t subimageSize = GetByteSize(Width, Height, 1, Format, Type, Level, align);

	SERIALISE_ELEMENT_BUF(byte *, buf, pixels, subimageSize);
	
	if(m_State == READING)
	{
		m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		m_Real.glTexSubImage2D(Target, Level, xoff, yoff, Width, Height, Format, Type, buf);

		delete[] buf;
	}

	return true;
}

void WrappedOpenGL::glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
	m_Real.glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXSUBIMAGE2D);
		Serialise_glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(int32_t, Level, level);
	SERIALISE_ELEMENT(int32_t, xoff, xoffset);
	SERIALISE_ELEMENT(int32_t, yoff, yoffset);
	SERIALISE_ELEMENT(int32_t, zoff, zoffset);
	SERIALISE_ELEMENT(uint32_t, Width, width);
	SERIALISE_ELEMENT(uint32_t, Height, height);
	SERIALISE_ELEMENT(uint32_t, Depth, depth);
	SERIALISE_ELEMENT(GLenum, Format, format);
	SERIALISE_ELEMENT(GLenum, Type, type);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());

	GLint align = 1;
	m_Real.glGetIntegerv(eGL_UNPACK_ALIGNMENT, &align);

	size_t subimageSize = GetByteSize(Width, Height, Depth, Format, Type, Level, align);

	SERIALISE_ELEMENT_BUF(byte *, buf, pixels, subimageSize);
	
	if(m_State == READING)
	{
		m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		m_Real.glTexSubImage3D(Target, Level, xoff, yoff, zoff, Width, Height, Depth, Format, Type, buf);

		delete[] buf;
	}

	return true;
}

void WrappedOpenGL::glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels)
{
	m_Real.glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXSUBIMAGE3D);
		Serialise_glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glCompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *pixels)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(int32_t, Level, level);
	SERIALISE_ELEMENT(int32_t, xoff, xoffset);
	SERIALISE_ELEMENT(uint32_t, Width, width);
	SERIALISE_ELEMENT(GLenum, fmt, format);
	SERIALISE_ELEMENT(uint32_t, byteSize, imageSize);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());

	SERIALISE_ELEMENT_BUF(byte *, buf, pixels, byteSize);
	
	if(m_State == READING)
	{
		m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		m_Real.glCompressedTexSubImage1D(Target, Level, xoff, Width, fmt, byteSize, buf);

		delete[] buf;
	}

	return true;
}

void WrappedOpenGL::glCompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *pixels)
{
	m_Real.glCompressedTexSubImage1D(target, level, xoffset, width, format, imageSize, pixels);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXSUBIMAGE1D_COMPRESSED);
		Serialise_glCompressedTexSubImage1D(target, level, xoffset, width, format, imageSize, pixels);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *pixels)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(int32_t, Level, level);
	SERIALISE_ELEMENT(int32_t, xoff, xoffset);
	SERIALISE_ELEMENT(int32_t, yoff, yoffset);
	SERIALISE_ELEMENT(uint32_t, Width, width);
	SERIALISE_ELEMENT(uint32_t, Height, height);
	SERIALISE_ELEMENT(GLenum, fmt, format);
	SERIALISE_ELEMENT(uint32_t, byteSize, imageSize);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());

	SERIALISE_ELEMENT_BUF(byte *, buf, pixels, byteSize);
	
	if(m_State == READING)
	{
		m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		m_Real.glCompressedTexSubImage2D(Target, Level, xoff, yoff, Width, Height, fmt, byteSize, buf);

		delete[] buf;
	}

	return true;
}

void WrappedOpenGL::glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *pixels)
{
	m_Real.glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, pixels);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXSUBIMAGE2D_COMPRESSED);
		Serialise_glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, pixels);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

bool WrappedOpenGL::Serialise_glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *pixels)
{
	SERIALISE_ELEMENT(GLenum, Target, target);
	SERIALISE_ELEMENT(int32_t, Level, level);
	SERIALISE_ELEMENT(int32_t, xoff, xoffset);
	SERIALISE_ELEMENT(int32_t, yoff, yoffset);
	SERIALISE_ELEMENT(int32_t, zoff, zoffset);
	SERIALISE_ELEMENT(uint32_t, Width, width);
	SERIALISE_ELEMENT(uint32_t, Height, height);
	SERIALISE_ELEMENT(uint32_t, Depth, depth);
	SERIALISE_ELEMENT(GLenum, fmt, format);
	SERIALISE_ELEMENT(uint32_t, byteSize, imageSize);
	SERIALISE_ELEMENT(ResourceId, id, m_TextureRecord[m_TextureUnit]->GetResourceID());

	SERIALISE_ELEMENT_BUF(byte *, buf, pixels, byteSize);
	
	if(m_State == READING)
	{
		m_Real.glBindTexture(Target, GetResourceManager()->GetLiveResource(id).name);
		m_Real.glCompressedTexSubImage3D(Target, Level, xoff, yoff, zoff, Width, Height, Depth, fmt, byteSize, buf);

		delete[] buf;
	}

	return true;
}

void WrappedOpenGL::glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *pixels)
{
	m_Real.glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, pixels);
	
	if(m_State >= WRITING)
	{
		RDCASSERT(m_TextureRecord[m_TextureUnit]);

		SCOPED_SERIALISE_CONTEXT(TEXSUBIMAGE3D_COMPRESSED);
		Serialise_glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, pixels);

		m_TextureRecord[m_TextureUnit]->AddChunk(scope.Get());
	}
}

#pragma endregion
