#include "pch.h"
#include "FreeTypeTextRenderer.h"
#include "FileUtility.h"
#include "DDSTextureLoader.h"
#include "TextureManager.h"
#include "SystemTime.h"
#include "GraphicsCore.h"
#include "CommandContext.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "BufferManager.h"
#include "CompiledShaders/FreeTypeTextVS.h"
#include "CompiledShaders/FreeTypeTextPS.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <fstream>
#include <sstream>
#include <iostream>

#if 0
struct TGAHeader
{
	uint8_t   idLength,           // Length of optional identification sequence.
		paletteType,        // Is a palette present? (1=yes)
		imageType;          // Image data type (0=none, 1=indexed, 2=rgb,
							// 3=grey, +8=rle packed).
	uint16_t  firstPaletteEntry,  // First palette index, if present.
		numPaletteEntries;  // Number of palette entries, if present.
	uint8_t   paletteBits;        // Number of bits per palette entry.
	uint16_t  x,                  // Horiz. pixel coord. of lower left of image.
		y,                  // Vert. pixel coord. of lower left of image.
		width,              // Image width in pixels.
		height;             // Image height in pixels.
	uint8_t   depth,              // Image color depth (bits per pixel).
		descriptor;         // Image attribute flags.
};


bool SaveTGA(const std::wstring& fileName, uint16_t width, uint16_t height, const byte* buffer)
{
	std::ofstream file(fileName.c_str(), std::ios::binary);
	if (file)
	{
		TGAHeader header;
		memset(&header, 0, sizeof(TGAHeader));
		header.imageType = 3;
		header.width = width;
		header.height = height;
		header.depth = 8;
		header.descriptor = 0x08;

		//byte* pixels = (byte*)malloc(width * height * 4);
		//for (uint16_t y = 0; y < height; ++y)
		//{
		//	for (uint16_t x = 0; x < width; ++x)
		//	{
		//		pixels[(y * width + x) * 4 + 0] = buffer[y * width + x];
		//		pixels[(y * width + x) * 4 + 1] = buffer[y * width + x];
		//		pixels[(y * width + x) * 4 + 2] = buffer[y * width + x];
		//		pixels[(y * width + x) * 4 + 3] = buffer[y * width + x];
		//	}
		//}

		file.write((const char*)&header.idLength, sizeof(header.idLength));
		file.write((const char*)&header.paletteType, sizeof(header.paletteType));
		file.write((const char*)&header.imageType, sizeof(header.imageType));
		file.write((const char*)&header.firstPaletteEntry, sizeof(header.firstPaletteEntry));
		file.write((const char*)&header.numPaletteEntries, sizeof(header.numPaletteEntries));
		file.write((const char*)&header.paletteBits, sizeof(header.paletteBits));
		file.write((const char*)&header.x, sizeof(header.x));
		file.write((const char*)&header.y, sizeof(header.y));
		file.write((const char*)&header.width, sizeof(header.width));
		file.write((const char*)&header.height, sizeof(header.height));
		file.write((const char*)&header.depth, sizeof(header.depth));
		file.write((const char*)&header.descriptor, sizeof(header.descriptor));

		for (int32_t y = height - 1; y >= 0; --y)
		{
			file.write((const char*)(buffer + y * width), sizeof(char) * width);
		}

		//free(pixels);

		return true;
	}
	return false;
}
#endif

static UINT BytesPerPixel(DXGI_FORMAT Format)
{
	return (UINT)BitsPerPixel(Format) / 8;
};

namespace FreeTypeTextRenderer
{
	FT_Library s_Library;

	class Font;
	class SheetTexture : public GpuResource
	{
		friend class CommandContext;

	public:
		SheetTexture()
			: m_Format{ DXGI_FORMAT_UNKNOWN }
			, m_Width{ 0 }
			, m_Height{ 0 }
			, m_ArraySize{ 0 }
		{
			m_CpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		}

		void Create(UINT nWidth, UINT nHeight, DXGI_FORMAT Format, UINT nArraySize = 1)
		{
			GpuResource::Destroy();

			m_UsageState = D3D12_RESOURCE_STATE_COMMON;
			
			m_Width = nWidth;
			m_Height = nHeight;
			m_Format = Format;
			m_ArraySize = nArraySize;

			D3D12_RESOURCE_DESC TextureDesc = {};
			TextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			TextureDesc.Width = m_Width;
			TextureDesc.Height = m_Height;
			TextureDesc.DepthOrArraySize = m_ArraySize;
			TextureDesc.MipLevels = 1;
			TextureDesc.Format = m_Format;
			TextureDesc.SampleDesc.Count = 1;
			TextureDesc.SampleDesc.Quality = 0;
			TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			TextureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			D3D12_HEAP_PROPERTIES HeapProps = {};
			HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
			HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			HeapProps.CreationNodeMask = 1;
			HeapProps.VisibleNodeMask = 1;

			ASSERT_SUCCEEDED(Graphics::g_Device->CreateCommittedResource(&HeapProps, 
				D3D12_HEAP_FLAG_NONE, 
				&TextureDesc,
				m_UsageState, 
				nullptr, 
				MY_IID_PPV_ARGS(m_pResource.ReleaseAndGetAddressOf())));

			m_pResource->SetName(L"DynamicArrayTexture");

			if (m_CpuDescriptorHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
				m_CpuDescriptorHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			Graphics::g_Device->CreateShaderResourceView(m_pResource.Get(), nullptr, m_CpuDescriptorHandle);
		}

		void Destroy()
		{
			GpuResource::Destroy();
			m_CpuDescriptorHandle.ptr = 0;
		}

		void Update(UINT nSheetIndex, const byte* pData)
		{
			D3D12_SUBRESOURCE_DATA SubResourceData;
			SubResourceData.pData = pData;
			SubResourceData.RowPitch = m_Width * BytesPerPixel(m_Format);
			SubResourceData.SlicePitch = SubResourceData.RowPitch * m_Height;
			CommandContext::UpdateTexture(*this, nSheetIndex, 1, &SubResourceData);
		}

		const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_CpuDescriptorHandle; }

		UINT GetArraySize() const { return m_ArraySize; }

	private:
		D3D12_CPU_DESCRIPTOR_HANDLE	m_CpuDescriptorHandle;
		DXGI_FORMAT m_Format;
		UINT m_Width, m_Height;
		UINT m_ArraySize;
	};

	class Font
	{
		friend class SheetTexture;

	public:
		Font(uint16_t nSheetSize = 1024) : m_SheetSize{ nSheetSize }
			, m_MaxHeight{ 0 }
			, m_LineHeight{ 0 }
		{

		}

		~Font()
		{
			if (m_Face != nullptr)
			{
				FT_Done_Face(m_Face);
			}

			m_Dictionary.clear();
		}

		// 폰트 문자중에 제일 큰 높이 반환
		uint16_t GetMaxHeight() const { return m_MaxHeight; }
		// 폰트 줄 간격 반환
		uint16_t GetLineHeight() const { return m_LineHeight; }
		// 시트(텍스쳐) 사이즈
		uint16_t GetSheetSize() const { return m_SheetSize; }

		// 텍스쳐
		const SheetTexture& GetTexture() const { return m_Texture; }

		class Sheet
		{
		public:
			Sheet(uint16_t nIndex, uint16_t nSize, uint16_t nRowHeight)
				: m_Index{ nIndex }
				, m_Size{ nSize }
				, m_RowSize{ nRowHeight }
				, m_PackX{ 0 }
				, m_PackY{ 0 }
				, m_Dirty{ false }
			{
				m_Data = std::make_shared<std::vector<byte>>(std::vector<byte>(m_Size * m_Size, 0));
			}

			uint16_t GetIndex() const { return m_Index; }

			bool PackGlyph(uint16_t nWidth, uint16_t nHeight, 
				const byte* pBuffer, 
				OUT uint16_t& PackX, OUT uint16_t& PackY)
			{
				ASSERT(m_RowSize >= nHeight);

				const uint16_t nPadding = 1;

				if (m_PackX + (nWidth + nPadding) > m_Size)
				{
					if (m_PackY + (m_RowSize + nPadding) > m_Size)
					{
						return false;
					}
					else
					{
						m_PackX = 0;
						m_PackY += (m_RowSize + nPadding);
					}
				}

				PackX = m_PackX;
				PackY = m_PackY;

				for (uint16_t y = m_PackY, yy = 0; y < (m_PackY + nHeight); ++y, ++yy)
				{
					for (uint16_t x = m_PackX, xx = 0; x < (m_PackX + nWidth); ++x, ++xx)
					{
						m_Data->data()[y * m_Size + x] = pBuffer[yy * nWidth + xx];
					}
				}

				m_PackX += (nWidth + nPadding);
				m_Dirty = true;

				return true;
			}

			bool Update(SheetTexture& Texture, bool bForce)
			{
				if (bForce || m_Dirty)
				{
					Texture.Update((UINT)m_Index, m_Data->data());
					m_Dirty = false;
					return true;
				}
				else
				{
					return false;
				}
			}
			
		private:
			uint16_t			m_Index;
			uint16_t			m_Size;
			uint16_t			m_RowSize;
			Utility::ByteArray	m_Data;
			uint16_t			m_PackX, m_PackY;
			Texture				m_Texture;
			bool				m_Dirty;
		};

		class Glyph
		{
		public:
			Glyph(uint16_t nSheetIndex,
				int16_t nOffsetX,
				int16_t nOffsetY,
				uint16_t nSheetX,
				uint16_t nSheetY,
				uint16_t nSizeX,
				uint16_t nSizeY,
				uint16_t nAdvance)
				: m_SheetIndex{ nSheetIndex }
				, m_OffsetX{ nOffsetX }
				, m_OffsetY{ nOffsetY }
				, m_SheetX{ nSheetX }
				, m_SheetY{ nSheetY }
				, m_SizeX{ nSizeX }
				, m_SizeY{ nSizeY }
				, m_Advance{ nAdvance }
			{

			}

			uint16_t GetSheetIndex() const { return m_SheetIndex; }
			int16_t GetOffsetX() const { return m_OffsetX; }
			int16_t GetOffsetY() const { return m_OffsetY; }
			uint16_t GetSheetX() const { return m_SheetX; }
			uint16_t GetSheetY() const { return m_SheetY; }
			uint16_t GetSizeX() const { return m_SizeX; }
			uint16_t GetSizeY() const { return m_SizeY; }
			uint16_t GetAdvance() const { return m_Advance; }
			
		private:
			uint16_t		m_SheetIndex;
			int16_t			m_OffsetX, m_OffsetY;
			uint16_t		m_SheetX, m_SheetY;
			uint16_t		m_SizeX, m_SizeY;
			uint16_t		m_Advance;
		};

		bool Load(const std::wstring& FileName, uint16_t nFontSize)
		{
			// 파일 로드
			m_Buffer = Utility::ReadFileSync(FileName);
			if (m_Buffer->size() == 0)
			{
				ERROR("Failed to open file %ls", FileName.c_str());
				return false;
			}

			// 메모리로 페이스 생성
			FT_Error Err = FT_New_Memory_Face(s_Library, 
				m_Buffer->data(), 
				(FT_Long)m_Buffer->size(), 
				0, 
				&m_Face);
			if (Err != 0)
				return false;

			// 사이즈 세팅
			FT_UInt nResolution = 100;
			Err = FT_Set_Char_Size(m_Face, (uint32_t)nFontSize << 6, 0, nResolution, 0);
			if (Err != 0)
				return false;

			m_MaxHeight = (uint16_t)(m_Face->size->metrics.height >> 6);
			m_LineHeight = (uint16_t)((m_Face->size->metrics.ascender - m_Face->size->metrics.descender) >> 6);

			// 기본 문자 미리 추가
			for (wchar_t c = 32; c < 127; ++c)
				GetGlyph(c);

			UpdateSheets();

			return true;
		}

		Glyph* GetGlyph(const wchar_t c)
		{
			auto Found = m_Dictionary.find(c);
			if (Found != m_Dictionary.end())
				return &Found->second;

			FT_Error Err = FT_Load_Char(m_Face, c, FT_LOAD_RENDER);
			if (Err != 0)
				return nullptr;

			FT_GlyphSlot Slot = m_Face->glyph;
			FT_Bitmap* pBitmap = &Slot->bitmap;

#if 0
			std::wostringstream FileName;
			FileName << L"C:\\Users\\PHANTOMERS\\Desktop\\Work\\MiniEngine\\ModelViewer\\Fonts\\" << c << L".tga";
			SaveTGA(FileName.str(), pBitmap->width, pBitmap->rows, pBitmap->buffer);
#endif

			uint16_t nSheetX, nSheetY;
			uint16_t nSheetIndex = PackGlyphToSheet(pBitmap->width, 
				pBitmap->rows, 
				pBitmap->buffer, 
				nSheetX, nSheetY);
			if (nSheetIndex == (uint16_t)-1)
				return nullptr;

			int16_t nOffsetX = Slot->bitmap_left;
			int16_t nOffsetY = m_LineHeight - Slot->bitmap_top;
			uint16_t nSizeX = pBitmap->width;
			uint16_t nSizeY = pBitmap->rows;
			uint16_t nAdvance = (uint16_t)(Slot->metrics.horiAdvance >> 6);

			auto Inserted = m_Dictionary.insert(std::make_pair(c, Glyph(nSheetIndex,
				nOffsetX,
				nOffsetY,
				nSheetX,
				nSheetY,
				nSizeX,
				nSizeY,
				nAdvance)));
			return &Inserted.first->second;
		}

		bool UpdateSheets()
		{
			if (m_Texture.GetArraySize() != (UINT)m_Sheets.size())
			{
				m_Texture.Create(m_SheetSize, m_SheetSize, DXGI_FORMAT_R8_UNORM, (UINT)m_Sheets.size());
				for (auto& iSheet : m_Sheets)
				{
					iSheet.Update(m_Texture, true);
				}
				return true;
			}
			else
			{
				bool Dirty = false;
				for (auto& iSheet : m_Sheets)
				{
					Dirty = iSheet.Update(m_Texture, false);
				}
				return Dirty;
			}
		}

	private:
		Sheet* AddSheet()
		{
			uint16_t nIndex = (uint16_t)m_Sheets.size();
			m_Sheets.push_back(Sheet(nIndex, m_SheetSize, m_MaxHeight));
			return &m_Sheets.back();
		}

		uint16_t PackGlyphToSheet(uint16_t nWidth, uint16_t nHeight,
			const byte* pBuffer,
			OUT uint16_t& nSheetX, OUT uint16_t& nSheetY)
		{
			Sheet* sheet = (m_Sheets.empty()) ? AddSheet() : &m_Sheets.back();

			if (sheet->PackGlyph(nWidth, nHeight, pBuffer, nSheetX, nSheetY) == false)
			{
				sheet = AddSheet();
				
				if (sheet->PackGlyph(nWidth, nHeight, pBuffer, nSheetX, nSheetY) == false)
				{
					return (uint16_t)-1;
				}
			}

			return sheet->GetIndex();
		}

		uint16_t			m_MaxHeight;
		uint16_t			m_LineHeight;
		FT_Face				m_Face = nullptr;
		Utility::ByteArray	m_Buffer;
		uint16_t			m_SheetSize;
		std::vector<Sheet>	m_Sheets;
		std::map<wchar_t, Glyph>	m_Dictionary;
		SheetTexture		m_Texture;
	};

	// 로드된 폰트
	std::map<std::wstring, std::unique_ptr<Font>> s_LoadedFonts;

	Font* GetOrLoadFont(const std::wstring& FileName, uint16_t FontSize)
	{
		auto Found = s_LoadedFonts.find(FileName);
		if (Found != s_LoadedFonts.end())
			return Found->second.get();

		const uint16_t nSheetSize = 128;
		Font* pFont = new Font(nSheetSize);
		if (pFont->Load(FileName, FontSize) == false)
		{
			delete pFont;
			return nullptr;
		}

		s_LoadedFonts[FileName].reset(pFont);
		return pFont;
	}

	RootSignature	s_RootSignature;
	GraphicsPSO		s_TextPSO[2];	// 0: R8G8B8A8_UNORM   1: R11G11B10_FLOAT

	bool Initialize(void)
	{
		// 프리 타입 초기화
		FT_Error Err = FT_Init_FreeType(&s_Library);
		if (Err != 0)
			return false;

		s_RootSignature.Reset(3, 1);
		s_RootSignature.InitStaticSampler(0, Graphics::SamplerLinearClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSignature[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
		s_RootSignature[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSignature[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSignature.Finalize(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		D3D12_INPUT_ELEMENT_DESC vertElem[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT     , 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
		};

		s_TextPSO[0].SetRootSignature(s_RootSignature);
		s_TextPSO[0].SetRasterizerState(Graphics::RasterizerTwoSided);
		s_TextPSO[0].SetBlendState(Graphics::BlendPreMultiplied);
		s_TextPSO[0].SetDepthStencilState(Graphics::DepthStateDisabled);
		s_TextPSO[0].SetInputLayout(_countof(vertElem), vertElem);
		s_TextPSO[0].SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		s_TextPSO[0].SetVertexShader(g_pFreeTypeTextVS, sizeof(g_pFreeTypeTextVS));
		s_TextPSO[0].SetPixelShader(g_pFreeTypeTextPS, sizeof(g_pFreeTypeTextPS));
		s_TextPSO[0].SetRenderTargetFormats(1, &Graphics::g_OverlayBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);
		s_TextPSO[0].Finalize();

		s_TextPSO[1] = s_TextPSO[0];
		s_TextPSO[1].SetRenderTargetFormats(1, &Graphics::g_SceneColorBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);
		s_TextPSO[1].Finalize();

		return true;
	}

	void Shutdown(void)
	{
		FT_Done_FreeType(s_Library);
		s_LoadedFonts.clear();
	}
}

#if 1
FreeTypeTextContext::FreeTypeTextContext(GraphicsContext& Context, float ViewWidth, float ViewHeight)
	: m_Context{ Context }
	, m_CurFont{ nullptr }
	, m_EnableHDR{ false }
{
	SetViewSize(ViewWidth, ViewHeight);
	ResetSettings();
}

void FreeTypeTextContext::ResetSettings()
{
	ResetCursor(0.0f, 0.0f);

	m_PSParams.TextColor = Color(1.0f, 1.0f, 1.0f, 1.0f);

	m_DirtyVSParams = true;
	m_DirtyPSParams = true;
	m_DirtyTexture = true;

	SetFont(L"Fonts/NanumGothicBold.ttf", 16);
}

void FreeTypeTextContext::NewLine()
{ 
	m_CursorX = m_LeftMargin;
	m_CursorY += GetLineHeight();
}

void FreeTypeTextContext::ResetCursor(float x, float y)
{
	m_LeftMargin = x;
	m_CursorX = x;
	m_CursorY = y;
}

float FreeTypeTextContext::GetLineHeight() const
{
	return m_CurFont->GetLineHeight();
}

void FreeTypeTextContext::SetColor(Color cColor)
{
	m_PSParams.TextColor = cColor;
	m_DirtyPSParams = true;
}

void FreeTypeTextContext::Begin(bool EnableHDR)
{
	ResetSettings();

	m_EnableHDR = EnableHDR;

	m_Context.SetRootSignature(FreeTypeTextRenderer::s_RootSignature);
	m_Context.SetPipelineState(FreeTypeTextRenderer::s_TextPSO[m_EnableHDR]);
	m_Context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
}

void FreeTypeTextContext::SetFont(const std::wstring& FontName, uint16_t FontSize)
{
	FreeTypeTextRenderer::Font* NextFont = 
		FreeTypeTextRenderer::GetOrLoadFont(FontName, FontSize);
	if (NextFont == m_CurFont || NextFont == nullptr)
	{
		return;
	}

	float TextSize = (float)FontSize;

	m_CurFont = NextFont;

	m_VSParams.InvTexSize = 1.0f / m_CurFont->GetSheetSize();
	m_VSParams.TextHeight = m_CurFont->GetMaxHeight();
	m_DirtyVSParams = true;
	m_DirtyTexture = true;
}

void FreeTypeTextContext::SetViewSize(float ViewWidth, float ViewHeight)
{
	m_ViewWidth = ViewWidth;
	m_ViewHeight = ViewHeight;

	const float vpX = 0.0f;
	const float vpY = 0.0f;
	const float twoDivW = 2.0f / ViewWidth;
	const float twoDivH = 2.0f / ViewHeight;

	m_VSParams.ViewportTransform = Math::Vector4(twoDivW, -twoDivH, -vpX * twoDivW - 1.0f, vpY * twoDivH + 1.0f);
	m_DirtyVSParams = true;
}

void FreeTypeTextContext::End()
{
	m_DirtyVSParams = true;
	m_DirtyPSParams = true;
	m_DirtyTexture = true;
}

void FreeTypeTextContext::SetRenderState()
{
	WARN_ONCE_IF(nullptr == m_CurFont, "Attempted to draw text without a font");

	m_DirtyTexture |= m_CurFont->UpdateSheets();

	if (m_DirtyVSParams)
	{
		m_Context.SetDynamicConstantBufferView(0, sizeof(m_VSParams), &m_VSParams);
		m_DirtyVSParams = false;
	}

	if (m_DirtyPSParams)
	{
		m_Context.SetDynamicConstantBufferView(1, sizeof(m_PSParams), &m_PSParams);
		m_DirtyPSParams = false;
	}

	if (m_DirtyTexture)
	{
		m_Context.SetDynamicDescriptors(2, 0, 1, &m_CurFont->GetTexture().GetSRV());
		m_DirtyTexture = false;
	}
}

UINT FreeTypeTextContext::FillVertexBuffer(TextVert volatile* Vertices, const wchar_t* Str, size_t nLen)
{
	UINT nCharsDrawn = 0;

	float PosX = m_CursorX;
	float PosY = m_CursorY;

	for (size_t Index = 0; Index < nLen; ++Index)
	{
		wchar_t c = Str[Index];
		if (c == L'\0')
			break;
		// 개행 문자 처리
		if (c == L'\n')
		{
			PosX = m_LeftMargin;
			PosY += m_CurFont->GetLineHeight();
			continue;
		}

		FreeTypeTextRenderer::Font::Glyph* pGlyph = m_CurFont->GetGlyph(c);
		if (pGlyph == nullptr)
			continue;

		// 화면 좌표
		Vertices->X = PosX + (float)pGlyph->GetOffsetX();
		Vertices->Y = PosY + (float)pGlyph->GetOffsetY();
		// 텍스쳐 좌표
		Vertices->U = pGlyph->GetSheetX();
		Vertices->V = pGlyph->GetSheetY();
		// 너비
		Vertices->W = pGlyph->GetSizeX();
		// 텍스쳐 인덱스
		Vertices->Index = pGlyph->GetSheetIndex();
		++Vertices;

		PosX += (float)pGlyph->GetAdvance();
		++nCharsDrawn;
	}

	m_CursorX = PosX;
	m_CursorY = PosY;

	return nCharsDrawn;
}

#include <locale>

void FreeTypeTextContext::DrawString(const std::wstring& Str)
{
	void* pStackMem = _malloca((Str.size() + 1) * 16);
	TextVert* Vertices = Math::AlignUp((TextVert*)pStackMem, 16);
	UINT nPrimCount = FillVertexBuffer(Vertices, Str.c_str(), Str.size());

	SetRenderState();

	if (nPrimCount > 0)
	{
		m_Context.SetDynamicVB(0, nPrimCount, sizeof(TextVert), Vertices);
		m_Context.DrawInstanced(4, nPrimCount);
	}

	_freea(pStackMem);
}

void FreeTypeTextContext::DrawFormattedString(const wchar_t* Format, ...)
{
	wchar_t Buffer[512];
	va_list Args;
	va_start(Args, Format);
	vswprintf(Buffer, 256, Format, Args);
	DrawString(std::wstring(Buffer));
}
#endif