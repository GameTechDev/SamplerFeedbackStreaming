//*********************************************************
//
// Copyright 2020 Intel Corporation 
//
// Permission is hereby granted, free of charge, to any 
// person obtaining a copy of this software and associated 
// documentation files(the "Software"), to deal in the Software 
// without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to 
// whom the Software is furnished to do so, subject to the 
// following conditions :
// The above copyright notice and this permission notice shall 
// be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
// DEALINGS IN THE SOFTWARE.
//
//*********************************************************

#pragma once

#include <windows.h>
#pragma warning(push, 0)
#include <gdiplus.h>
#pragma warning(pop)
#undef max
#undef min

#include <d3d12.h>
#include <dxgi1_6.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

#include <dxcapi.h>         // Be sure to link with dxcompiler.lib.
#include <d3d12shader.h>    // Shader reflection.

#include <string>
#include <vector>
#include <list>
#include <wrl.h>
#include <shellapi.h>
#include <stdexcept>
#include <sstream>
#include <crtdbg.h>
#include <ppl.h>
#include <random>
#include <fstream>
#include <functional>
#include <algorithm>
#include <memory>

#ifdef _DEBUG
#include <assert.h>
#define ASSERT(X) assert(X)
#else
#define ASSERT(X)
#endif

// DirectXTK12
#include <wincodec.h>
#include <array>
#include <map>
#include "d3dx12.h"

// Imgui
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "Timer.h"