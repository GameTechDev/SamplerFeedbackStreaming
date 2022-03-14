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
        FileStreamerDS(ID3D12Device* in_pDevice);
        virtual ~FileStreamerDS();

        virtual FileHandle* OpenFile(const std::wstring& in_path) override;
        virtual void StreamTexture(Streaming::UpdateList& in_updateList) override;
        virtual void StreamPackedMips(Streaming::UpdateList& in_updateList) override;

        static IDStorageFile* GetFileHandle(const FileHandle* in_pHandle);

        virtual bool GetCompleted(const Streaming::UpdateList& in_updateList) const override;

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
        ComPtr<IDStorageFactory> m_factory;

        ComPtr<IDStorageQueue> m_fileQueue;

        // separate memory queue means needing a second fence - can't wait across DS queues
        ComPtr<IDStorageQueue> m_memoryQueue;
        ComPtr<ID3D12Fence> m_memoryFence;
        HANDLE m_memoryFenceEvent{ nullptr };
        UINT64 m_memoryFenceValue{ 1 };
        bool m_haveMemoryRequests{ false };
    };
};
