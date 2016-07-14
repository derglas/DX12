#pragma once

#include "Color.h"
#include "Math/Vector.h"
#include <string>

namespace FreeTypeTextRenderer
{
	bool Initialize(void);
	void Shutdown(void);

	class Font;
}

#if 1
class FreeTypeTextContext
{
public:
	FreeTypeTextContext(GraphicsContext& Context, float ViewWidth, float ViewHeight);

	GraphicsContext& GetCommandContext() const { return m_Context; }

	void ResetSettings();

	void SetFont(const std::wstring& fontName, uint16_t fontSize);

	// 화면 사이즈
	void SetViewSize(float viewWidth, float viewHeight);

	// 텍스트 위치 관련
	void ResetCursor(float CursorX, float CursorY);
	void SetLeftMargin(float LeftMargin) { m_LeftMargin = LeftMargin; }
	void SetCursorX(float CursorX) { m_CursorX = CursorX; }
	void SetCursorY(float CursorY) { m_CursorY = CursorY; }
	float GetLeftMargin() const { return m_LeftMargin; }
	float GetCursorX() const { return m_CursorX; }
	float GetCursorY() const { return m_CursorY; }
	float GetLineHeight() const;
	void NewLine();

	void SetColor(Color cColor);

	// 렌더링 관련
	void Begin(bool EnableHDR = false);
	void End();

	// 텍스트 렌더링
	void DrawString(const std::wstring& Str);
	void DrawString(const std::string& Str);
	void DrawFormattedString(const wchar_t* Format, ...);
	void DrawFormattedString(const char* Format, ...);

private:
	__declspec(align(16)) struct VertexShaderParams
	{
		Math::Vector4 ViewportTransform;
		float InvTexSize, TextHeight;
	};

	__declspec(align(16)) struct PixelShaderParams
	{
		Color TextColor;
	};

	void SetRenderState();

	// 버텍스 포맷
	__declspec(align(16)) struct TextVert
	{
		float X, Y;
		uint16_t U, V, W, Index;
	};

	UINT FillVertexBuffer(TextVert volatile* Vertices, const wchar_t* wstr, size_t len);

	GraphicsContext& m_Context;
	FreeTypeTextRenderer::Font* m_CurFont;
	VertexShaderParams m_VSParams;
	PixelShaderParams m_PSParams;
	bool m_DirtyVSParams;
	bool m_DirtyPSParams;
	bool m_DirtyTexture;
	float m_LeftMargin;
	float m_CursorX;
	float m_CursorY;
	float m_ViewWidth;
	float m_ViewHeight;
	bool m_EnableHDR;
};
#endif