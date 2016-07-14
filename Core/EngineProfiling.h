//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#pragma once

#include <string>
#include "FreeTypeTextRenderer.h"

class CommandContext;

namespace EngineProfiling
{
	void Update();

	void BeginBlock(const std::wstring& name, CommandContext* Context = nullptr);
	void EndBlock(CommandContext* Context = nullptr);

	void DisplayFrameRate(FreeTypeTextContext& Text);
	void DisplayPerfGraph(GraphicsContext& Context, FreeTypeTextContext& Text);
	void Display(FreeTypeTextContext& Text, float x, float y, float w, float h);
	bool IsPaused();
}

#ifdef RELEASE
class ScopedTimer
{
public:
	ScopedTimer(const std::wstring& name) {}
	ScopedTimer(const std::wstring& name, CommandContext& Context) {}
};
#else
class ScopedTimer
{
public:
	ScopedTimer( const std::wstring& name ) : m_Context(nullptr)
	{
		EngineProfiling::BeginBlock(name);
	}
	ScopedTimer( const std::wstring& name, CommandContext& Context ) : m_Context(&Context)
	{
		EngineProfiling::BeginBlock(name, m_Context);
	}
	~ScopedTimer()
	{
		EngineProfiling::EndBlock(m_Context);
	}

private:
	CommandContext* m_Context;
};
#endif
