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
#include <dstorage.h>

#include "FileStreamer.h"

//=======================================================================================
//=======================================================================================
namespace Streaming
{
    class FileStreamerDS : public FileStreamer
    {
    public:
        FileStreamerDS(ID3D12Device* in_pDevice, IDStorageFactory* in_pFactory);
        virtual ~FileStreamerDS();

        virtual FileHandle* OpenFile(const std::wstring& in_path) override;
        virtual void StreamTexture(Streaming::UpdateList& in_updateList) override;

        // for DS, we don't have a way to batch batches
        // this allows the calling thread to periodically request Submit() vs. every enqueue
        virtual void Signal() override;

    private:
        class FileHandleDS : public FileHandle
        {
        public:
            FileHandleDS(IDStorageFactory* in_pFactory, const std::wstring& in_path);
            virtual ~FileHandleDS() { m_file->Close(); }

            IDStorageFile* GetHandle() const { return m_file.Get(); }
        private:
            ComPtr<IDStorageFile> m_file;
        };
        IDStorageFactory* m_pFactory{ nullptr };

        ComPtr<IDStorageQueue> m_fileQueue;

        // memory queue when for visualization modes, which copy from cpu memory
        ComPtr<IDStorageQueue> m_memoryQueue;

        static IDStorageFile* GetFileHandle(const FileHandle* in_pHandle);
    };
};
