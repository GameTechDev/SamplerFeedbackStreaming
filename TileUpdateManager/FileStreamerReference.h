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
#include "FileStreamer.h"
#include "Timer.h"

#include "UploadAllocator.h"

//=======================================================================================
//=======================================================================================
namespace Streaming
{
    class FileStreamerReference : public FileStreamer
    {
    public:
        FileStreamerReference(ID3D12Device* in_pDevice,
            UINT in_maxNumCopyBatches,               // maximum number of in-flight batches
            UINT in_maxTileCopiesPerBatch,           // batch size. a small number, like 32
            UINT in_maxTileCopiesInFlight);          // upload buffer size. 1024 would become a 64MB upload buffer
        virtual ~FileStreamerReference();

        virtual FileHandle* OpenFile(const std::wstring& in_path) override;

        virtual void StreamTexture(Streaming::UpdateList& in_updateList) override;
        virtual void StreamPackedMips(Streaming::UpdateList& in_updateList) override;

        virtual void Signal() override {} // reference auto-submits

        virtual bool GetCompleted(const UpdateList& in_updateList) const override;

        static HANDLE GetFileHandle(const FileHandle* in_pHandle) { return dynamic_cast<const FileHandleReference*>(in_pHandle)->GetHandle(); }

        static const UINT MEDIA_SECTOR_SIZE = 4096; // see https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering
    private:
        class FileHandleReference : public FileHandle
        {
        public:
            FileHandleReference(HANDLE in_handle) : m_handle(in_handle) {}
            virtual ~FileHandleReference() { CloseHandle(m_handle); }

            HANDLE GetHandle() const { return m_handle; }
        private:
            const HANDLE m_handle;
        };

        RawCpuTimer m_cpuTimer;

        ComPtr<ID3D12CommandQueue> m_copyCommandQueue;
        ComPtr<ID3D12GraphicsCommandList> m_copyCommandList;

        class CopyBatch
        {
        public:
            enum class State : UINT
            {
                FREE = 0,
                LOAD_PACKEDMIPS,
                LOAD_TILES,
                COPY_TILES,
                WAIT_COMPLETE,
            };

            std::atomic<State> m_state{ State::FREE };

            Streaming::UpdateList* m_pUpdateList{ nullptr };
            std::vector<UINT> m_uploadIndices; // indices into upload buffer
            UINT64 m_copyFenceValue{ 0 }; // tracked independently from UpdateList so CopyBatch lifetime can be independent

            void Reset() { m_lastSignaled = 0; m_numEvents = 0; }
            void ReadFile(const HANDLE in_fileHandle, void* in_pDst, UINT in_numBytes, UINT in_fileOffset);
            bool GetReadsComplete();

            ID3D12CommandAllocator* GetCommandAllocator()
            {
                m_commandAllocator->Reset();
                return m_commandAllocator.Get();
            }

            // call only once
            void Init(UINT in_maxNumCopies, ID3D12Device* in_pDevice);
        private:
            struct Request : public OVERLAPPED
            {
                Request() { hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr); }
                ~Request() { CloseHandle(hEvent); }
            };
            std::vector<Request> m_requests;
            UINT m_numEvents{ 0 };
            UINT m_lastSignaled{ 0 };

            ComPtr<ID3D12CommandAllocator> m_commandAllocator;
        };

        // close command list, execute on m_copyCommandQueue, signal fence, increment fence value
        void ExecuteCopyCommandList(ID3D12GraphicsCommandList* in_pCmdList);

        std::vector<CopyBatch> m_copyBatches;
        UINT m_batchAllocIndex{ 0 }; // allocation optimization

        // structure for finding space to upload tiles
        Streaming::UploadAllocator m_uploadAllocator;

        void CopyThread();
        std::atomic<bool> m_copyThreadRunning{ false };
        std::thread m_copyThread;

        CopyBatch& AllocateCopyBatch(Streaming::UpdateList& in_updateList);

        void LoadTexture(CopyBatch& in_copyBatch);
        void CopyTiles(ID3D12GraphicsCommandList* out_pCopyCmdList, ID3D12Resource* in_pSrcResource,
            const UpdateList* in_pUpdateList, const std::vector<UINT>& in_indices);
        void InitPackedMips(ID3D12GraphicsCommandList* out_pCmdList, const CopyBatch& in_copyBatch);
    };
}
