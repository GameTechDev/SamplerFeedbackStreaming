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

class SharedConstants
{
public:

    // rendering properties
    static const DXGI_FORMAT SWAP_CHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
    static const DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
    static const UINT SWAP_CHAIN_BUFFER_COUNT = 2;

    // scene properties
    static const UINT SPHERE_SCALE = 100;
    static const UINT UNIVERSE_SIZE = 35 * SPHERE_SCALE;
    static const UINT CAMERA_ANIMATION_RADIUS = UNIVERSE_SIZE / 4;
    static const UINT SPHERE_SPACING = 1; // percent, min gap size between planets
    static const UINT MAX_SPHERE_SCALE = 10; // spheres can be up to this * sphere_scale in size

    static const UINT NUM_SPHERE_LEVELS_OF_DETAIL = 5;
    static const UINT SPHERE_LOD_BIAS = 100;
};
